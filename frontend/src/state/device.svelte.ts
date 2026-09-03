import { api } from "../api/client.ts";
import type { ChayaStatus, DeviceInfo, MqttStatus, OtaStatus, WifiStatus } from "../api/types.ts";
import { pushToast } from "../components/toastStack.ts";
import type { ShowToast, ToastItem, ToastVariant } from "../components/toastStack.ts";
import { applyDeviceUiPrefs } from "../prefs/uiPrefs.ts";

export type LiveState = "connecting" | "live" | "reconnecting";

const emptyChaya = (): ChayaStatus => ({
  rx: 0,
  tx: 0,
  connected: false,
  configured: false,
  paired: false,
});
const emptyWifi = (): WifiStatus => ({ connected: false });
const emptyMqtt = (): MqttStatus => ({ connected: false });

export class DeviceStore {
  device = $state<DeviceInfo | null>(null);
  chaya = $state<ChayaStatus>(emptyChaya());
  wifi = $state<WifiStatus>(emptyWifi());
  mqtt = $state<MqttStatus>(emptyMqtt());
  ota = $state<OtaStatus | null>(null);
  live = $state<LiveState>("connecting");
  toasts = $state<ToastItem[]>([]);
  bootError = $state(false);
  booting = $state(true);
  refreshSeq = $state(0);
  private refreshGeneration = 0;

  readonly sseKey = $derived(
    this.device ? `${this.device.deviceId}:${this.device.mode}:${this.refreshSeq}` : "",
  );

  showToast: ShowToast = (text, variant: ToastVariant = "success") => {
    this.toasts = pushToast(this.toasts, text, variant);
  };

  dismissToast = (id: string) => {
    this.toasts = this.toasts.filter((item) => item.id !== id);
  };

  reset = () => {
    this.refreshGeneration += 1;
    this.device = null;
    this.chaya = emptyChaya();
    this.wifi = emptyWifi();
    this.mqtt = emptyMqtt();
    this.ota = null;
    this.live = "connecting";
    this.toasts = [];
    this.bootError = false;
    this.booting = true;
    this.refreshSeq = 0;
  };

  refreshDevice = async () => {
    const gen = ++this.refreshGeneration;
    const boot = await api.getBootstrap();
    if (gen !== this.refreshGeneration) return;
    this.device = boot.device;
    this.wifi = boot.wifi;
    this.chaya = boot.chaya ?? emptyChaya();
    this.mqtt = boot.mqtt ?? emptyMqtt();
    this.ota = boot.update;
    if (boot.settings) {
      applyDeviceUiPrefs(boot.settings.lang, boot.settings.theme);
    }
    this.bootError = false;
    this.refreshSeq += 1;
  };

  boot = async () => {
    this.booting = true;
    this.bootError = false;
    this.live = "connecting";
    try {
      await this.refreshDevice();
    } catch {
      this.bootError = true;
      this.device = null;
    } finally {
      this.booting = false;
    }
  };

  reload = async () => {
    if (this.bootError || !this.device) {
      await this.boot();
      return;
    }
    try {
      await this.refreshDevice();
    } catch {
      this.bootError = true;
      this.device = null;
    }
  };
}

export const device = new DeviceStore();

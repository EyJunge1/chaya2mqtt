import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { api, refreshCsrf } from "../api/client";
import { connectEvents } from "../api/sse";
import type {
  ChayaStatus,
  DeviceInfo,
  DeviceMode,
  MqttStatus,
  OtaStatus,
  WifiStatus,
} from "../api/types";
import { ErrorBlock, LoadingBlock } from "../components/StateBlock";
import { ToastStack, type ShowToast, type ToastItem, type ToastVariant } from "../components/Toast";
import { pushToast } from "../components/toastStack";
import { useI18n } from "../i18n/useI18n";
import { DeviceContext, type LiveState } from "./deviceContext";

export type DeviceChromeApi = {
  mode?: DeviceMode;
  ready: boolean;
  booting: boolean;
  bootError: boolean;
  reload: () => Promise<void>;
};

export function DeviceProvider({
  children,
  chrome,
}: {
  children: ReactNode;
  chrome?: (api: DeviceChromeApi) => ReactNode;
}) {
  const { t } = useI18n();
  const [device, setDevice] = useState<DeviceInfo | null>(null);
  const [chaya, setChaya] = useState<ChayaStatus>({ rx: 0, tx: 0, connected: false });
  const [wifi, setWifi] = useState<WifiStatus>({ connected: false });
  const [mqtt, setMqtt] = useState<MqttStatus>({ connected: false });
  const [ota, setOta] = useState<OtaStatus | null>(null);
  const [live, setLive] = useState<LiveState>("connecting");
  const [toasts, setToasts] = useState<ToastItem[]>([]);
  const [bootError, setBootError] = useState(false);
  const [booting, setBooting] = useState(true);
  const [refreshSeq, setRefreshSeq] = useState(0);

  const showToast: ShowToast = useCallback((text, variant: ToastVariant = "success") => {
    setToasts((prev) => pushToast(prev, text, variant));
  }, []);

  const dismissToast = useCallback((id: string) => {
    setToasts((prev) => prev.filter((item) => item.id !== id));
  }, []);

  const refreshDevice = useCallback(async () => {
    await refreshCsrf();
    const d = await api.getDevice();
    setDevice(d);
    const [c, w, m, updateStatus] = await Promise.all([
      d.mode === "sta" ? api.getChaya() : Promise.resolve({ rx: 0, tx: 0, connected: false }),
      api.getWifiStatus(),
      d.mode === "sta" ? api.getMqttStatus() : Promise.resolve({ connected: false }),
      d.mode === "sta" ? api.getUpdateStatus().catch(() => null) : Promise.resolve(null),
    ]);
    setChaya(c);
    setWifi(w);
    setMqtt(m);
    setOta(updateStatus);
    setBootError(false);
    setRefreshSeq((n) => n + 1);
  }, []);

  const boot = useCallback(async () => {
    setBooting(true);
    setBootError(false);
    setLive("connecting");
    try {
      await refreshDevice();
    } catch {
      setBootError(true);
      setDevice(null);
    } finally {
      setBooting(false);
    }
  }, [refreshDevice]);

  const reload = useCallback(async () => {
    if (bootError || !device) {
      await boot();
      return;
    }
    try {
      await refreshDevice();
    } catch {
      setBootError(true);
      setDevice(null);
    }
  }, [boot, bootError, device, refreshDevice]);

  useEffect(() => {
    void boot();
  }, [boot]);

  const sseKey = device ? `${device.deviceId}:${device.mode}` : "";

  useEffect(() => {
    if (!sseKey) return;
    setLive("connecting");
    return connectEvents({
      chaya: (data) => {
        setLive("live");
        setChaya(data);
      },
      wifi: (data) => {
        setLive("live");
        setWifi(data);
      },
      mqtt: (data) => {
        setLive("live");
        setMqtt(data);
      },
      ota: (data) => {
        setLive("live");
        setOta(data);
      },
      error: () => setLive("reconnecting"),
    });
  }, [sseKey]);

  const value = useMemo(
    () =>
      device
        ? {
            device,
            chaya,
            wifi,
            mqtt,
            ota,
            live,
            refreshSeq,
            showToast,
            refreshDevice,
          }
        : null,
    [device, chaya, wifi, mqtt, ota, live, refreshSeq, showToast, refreshDevice],
  );

  const chromeNode = chrome?.({
    mode: device?.mode,
    ready: Boolean(device) && !bootError,
    booting,
    bootError,
    reload,
  });

  if (bootError) {
    return (
      <>
        {chromeNode}
        <div className="mx-auto max-w-140 px-4 py-10">
          <h1 className="mb-4 text-center text-xl font-bold text-text-bright">{t("app.title")}</h1>
          <ErrorBlock
            title={t("app.boot-error-title")}
            message={t("app.boot-error")}
            retryLabel={t("common.retry")}
            onRetry={() => void boot()}
          />
        </div>
      </>
    );
  }

  if (booting || !device || !value) {
    return (
      <>
        {chromeNode}
        <div className="mx-auto max-w-140 px-4 py-10">
          <LoadingBlock label={t("app.connecting")} />
        </div>
      </>
    );
  }

  return (
    <DeviceContext.Provider value={value}>
      {chromeNode}
      {children}
      <ToastStack toasts={toasts} onDismiss={dismissToast} />
    </DeviceContext.Provider>
  );
}

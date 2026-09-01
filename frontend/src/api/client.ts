import type {
  ApiResult,
  BootstrapPayload,
  MqttConfigView,
  OtaChannel,
  SettingsInfo,
  WifiConfig,
  WifiConnectFields,
  WifiConnectStatus,
  WifiScanSnapshot,
} from "./types";
import { parseOtaStatus, parseWifiScanSnapshot } from "./validate";

async function parseJson<T>(res: Response): Promise<T> {
  const text = await res.text();
  if (!text) {
    throw new Error(`Empty response (${res.status})`);
  }
  return JSON.parse(text) as T;
}

function jsonBody(fields: Record<string, string | number | boolean | undefined>): string {
  const body: Record<string, string | number | boolean> = {};
  for (const [key, value] of Object.entries(fields)) {
    if (value === undefined) continue;
    body[key] = value;
  }
  return JSON.stringify(body);
}

async function apiGet<T>(path: string): Promise<T> {
  const res = await fetch(path);
  if (!res.ok) {
    throw new Error(`${path} failed (${res.status})`);
  }
  return parseJson<T>(res);
}

async function apiPost(
  path: string,
  fields: Record<string, string | number | boolean | undefined> = {},
): Promise<ApiResult> {
  const res = await fetch(path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: jsonBody(fields),
  });
  const data = await parseJson<ApiResult>(res);
  if (!res.ok && data && typeof data === "object" && "ok" in data && data.ok === false) {
    return data;
  }
  if (!res.ok) {
    return { ok: false, error: `request_failed_${res.status}` };
  }
  return data;
}

export const api = {
  getBootstrap: async (): Promise<BootstrapPayload> => {
    const raw = await apiGet<BootstrapPayload>("/api/bootstrap");
    return {
      ...raw,
      update: raw.update ? parseOtaStatus(raw.update) : null,
    };
  },
  sendChaya: () => apiPost("/api/chaya/send"),
  getWifiConfig: () => apiGet<WifiConfig>("/api/wifi/config"),
  startWifiScan: () => apiPost("/api/wifi/scan"),
  scanWifi: async (): Promise<WifiScanSnapshot> => {
    const res = await fetch("/api/wifi/scan");
    if (!res.ok) throw new Error(`wifi scan failed (${res.status})`);
    return parseWifiScanSnapshot(await parseJson<unknown>(res));
  },
  connectWifi: (fields: WifiConnectFields) =>
    apiPost("/api/wifi/connect", {
      ssid: fields.ssid,
      password: fields.password,
      mode: fields.mode,
      ip: fields.mode === "static" ? fields.ip : undefined,
      gateway: fields.mode === "static" ? fields.gateway : undefined,
      netmask: fields.mode === "static" ? fields.netmask : undefined,
      dns1: fields.dns1,
      dns2: fields.dns2,
      ntp1: fields.ntp1,
      ntp2: fields.ntp2,
    }),
  getWifiConnectStatus: () => apiGet<WifiConnectStatus>("/api/wifi/connect-status"),
  commitWifiConnect: () => apiPost("/api/wifi/connect-commit"),
  abortWifiConnect: () => apiPost("/api/wifi/connect-abort"),
  retryWifiConnect: () => apiPost("/api/wifi/connect-retry"),
  getMqttConfig: () => apiGet<MqttConfigView>("/api/mqtt"),
  saveMqtt: (fields: {
    mqtt_server: string;
    mqtt_port: number;
    mqtt_tls: boolean;
    mqtt_user?: string;
    mqtt_pass?: string;
    partner_id?: string;
  }) => apiPost("/api/mqtt", fields),
  getSettings: () => apiGet<SettingsInfo>("/api/settings"),
  saveSettings: (fields: {
    reset_days?: number;
    lang?: string;
    theme?: string;
    led_enabled?: boolean;
    audio_tx_enabled?: boolean;
    audio_rx_enabled?: boolean;
    audio_tx_volume?: number;
    audio_rx_volume?: number;
    quiet_hour_start?: number;
    quiet_hour_end?: number;
    tx_hz?: number;
    tx_ms?: number;
    rx_hz?: number;
    rx_ms?: number;
  }) => apiPost("/api/settings", fields),
  reboot: () => apiPost("/api/reboot"),
  factoryReset: () => apiPost("/api/factory-reset"),
  getUpdateStatus: async () => parseOtaStatus(await apiGet<unknown>("/api/update/status")),
  checkUpdate: (channel?: OtaChannel) => apiPost("/api/update/check", channel ? { channel } : {}),
  installUpdate: () => apiPost("/api/update/install"),
};

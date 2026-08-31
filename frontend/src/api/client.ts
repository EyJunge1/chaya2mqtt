import type {
  ApiResult,
  BootstrapPayload,
  ChayaStatus,
  DeviceInfo,
  MqttConfigView,
  MqttStatus,
  OtaChannel,
  SettingsInfo,
  WifiConfig,
  WifiConnectFields,
  WifiConnectStatus,
  WifiScanSnapshot,
  WifiStatus,
} from "./types";
import { parseOtaStatus, parseWifiScanSnapshot } from "./validate";

let csrfToken = "";
let csrfRefreshPromise: Promise<string> | null = null;

const csrfRetryablePosts = new Set([
  "/api/wifi/scan",
  "/api/wifi/connect",
  "/api/wifi/connect-retry",
  "/api/wifi/connect-abort",
  "/api/mqtt",
  "/api/settings",
  "/api/update/check",
]);

export function setCsrfToken(token: string): void {
  csrfToken = token;
}

async function parseJson<T>(res: Response): Promise<T> {
  const text = await res.text();
  if (!text) {
    throw new Error(`Empty response (${res.status})`);
  }
  return JSON.parse(text) as T;
}

function formBody(fields: Record<string, string | number | boolean | undefined>): string {
  const body = new URLSearchParams();
  body.set("csrf_token", csrfToken);
  for (const [key, value] of Object.entries(fields)) {
    if (value === undefined) continue;
    if (typeof value === "boolean") {
      body.set(key, value ? "1" : "0");
      continue;
    }
    body.set(key, String(value));
  }
  return body.toString();
}

async function apiGet<T>(path: string): Promise<T> {
  const res = await fetch(path, { credentials: "same-origin" });
  if (!res.ok) {
    throw new Error(`${path} failed (${res.status})`);
  }
  return parseJson<T>(res);
}

async function apiPost(
  path: string,
  fields: Record<string, string | number | boolean | undefined> = {},
  csrfRetried = false,
): Promise<ApiResult> {
  const res = await fetch(path, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
    body: formBody(fields),
  });
  const data = await parseJson<ApiResult>(res);
  if (
    res.status === 403 &&
    data &&
    typeof data === "object" &&
    "ok" in data &&
    data.ok === false &&
    data.error === "csrf" &&
    !csrfRetried &&
    csrfRetryablePosts.has(path)
  ) {
    await refreshCsrf();
    return apiPost(path, fields, true);
  }
  if (!res.ok && data && typeof data === "object" && "ok" in data && data.ok === false) {
    return data;
  }
  if (!res.ok) {
    return { ok: false, error: `request_failed_${res.status}` };
  }
  return data;
}

export async function refreshCsrf(): Promise<string> {
  if (csrfRefreshPromise === null) {
    csrfRefreshPromise = apiGet<{ token: string; expiresInSeconds: number }>("/api/csrf")
      .then((data) => {
        csrfToken = data.token;
        return csrfToken;
      })
      .finally(() => {
        csrfRefreshPromise = null;
      });
  }
  return csrfRefreshPromise;
}

export const api = {
  getBootstrap: async (): Promise<BootstrapPayload> => {
    const raw = await apiGet<BootstrapPayload>("/api/bootstrap");
    if (raw.csrf?.token) {
      setCsrfToken(raw.csrf.token);
    }
    return {
      ...raw,
      update: raw.update ? parseOtaStatus(raw.update) : null,
    };
  },
  getDevice: () => apiGet<DeviceInfo>("/api/device"),
  getChaya: () => apiGet<ChayaStatus>("/api/chaya"),
  sendChaya: () => apiPost("/api/chaya/send"),
  getWifiStatus: () => apiGet<WifiStatus>("/api/wifi/status"),
  getWifiConfig: () => apiGet<WifiConfig>("/api/wifi/config"),
  startWifiScan: () => apiPost("/api/wifi/scan"),
  scanWifi: async (): Promise<WifiScanSnapshot> => {
    const res = await fetch("/api/wifi/scan", { credentials: "same-origin" });
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
  getMqttStatus: () => apiGet<MqttStatus>("/api/mqtt/status"),
  getMqttConfig: () => apiGet<MqttConfigView>("/api/mqtt"),
  saveMqtt: (fields: {
    mqtt_server: string;
    mqtt_port: number;
    mqtt_tls: boolean | number;
    mqtt_user?: string;
    mqtt_pass?: string;
    partner_id?: string;
  }) => apiPost("/api/mqtt", fields),
  getSettings: () => apiGet<SettingsInfo>("/api/settings"),
  saveSettings: (fields: {
    reset_days?: number;
    lang?: string;
    theme?: string;
    led_enabled?: boolean | number;
    audio_tx_enabled?: boolean | number;
    audio_rx_enabled?: boolean | number;
    audio_tx_volume?: number;
    audio_rx_volume?: number;
    quiet_hour_start?: number;
    quiet_hour_end?: number;
    tx_hz?: number;
    tx_ms?: number;
    rx_hz?: number;
    rx_ms?: number;
  }) => apiPost("/api/settings", fields),
  reboot: async () => {
    await refreshCsrf();
    return apiPost("/api/reboot");
  },
  factoryReset: async () => {
    await refreshCsrf();
    return apiPost("/api/factory-reset");
  },
  getUpdateStatus: async () => parseOtaStatus(await apiGet<unknown>("/api/update/status")),
  checkUpdate: (channel?: OtaChannel) => apiPost("/api/update/check", channel ? { channel } : {}),
  installUpdate: () => apiPost("/api/update/install"),
};

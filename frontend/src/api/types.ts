export type DeviceMode = "ap" | "sta";

export interface DeviceInfo {
  hostname: string;
  version: string;
  mode: DeviceMode;
  deviceId: string;
  /** Pack millivolts (GPIO4 × 2). Always present. */
  batteryMv: number;
  /** Coarse LiPo estimate 0–100. Always present. */
  batteryPct: number;
  /** SoftAP SSID — only present in AP/setup mode. */
  apSsid?: string;
  /** SoftAP IPv4 — only present in AP/setup mode. */
  apIp?: string;
}

export interface DeviceBatteryEvent {
  batteryMv: number;
  batteryPct: number;
}

export interface ChayaStatus {
  rx: number;
  tx: number;
  connected: boolean;
  configured: boolean;
  /** Partner device ID is set (required for heart send / E-Ink heart). */
  paired: boolean;
}

export type WifiIpMode = "dhcp" | "static";

export interface WifiStatusDisconnected {
  connected: false;
}

export interface WifiStatusConnected {
  connected: true;
  ssid: string;
  ip: string;
  gateway: string;
  netmask: string;
  dns1: string;
  dns2: string;
  rssi: number;
}

export type WifiStatus = WifiStatusDisconnected | WifiStatusConnected;

export interface WifiConfig {
  ssid: string;
  mode: WifiIpMode;
  ip: string;
  gateway: string;
  netmask: string;
  dns1: string;
  dns2: string;
  ntp1: string;
  ntp2: string;
}

export interface WifiConnectFields {
  ssid: string;
  password: string;
  mode: WifiIpMode;
  ip?: string;
  gateway?: string;
  netmask?: string;
  dns1?: string;
  dns2?: string;
  ntp1?: string;
  ntp2?: string;
}

export interface WifiScanAp {
  ssid: string;
  rssi: number;
  open: boolean;
}

export type WifiConnectState = "idle" | "testing" | "ok" | "fail";

export interface WifiConnectStatus {
  state: WifiConnectState;
  ssid: string;
}

export interface MqttStatus {
  connected: boolean;
}

export interface MqttConfigView {
  deviceId: string;
  server: string;
  port: number;
  /** true = mqtts (TLS), false = mqtt (plain TCP) */
  tls: boolean;
  username: string;
  hasPassword: boolean;
  topicPub: string;
  topicSub: string;
  partnerId: string;
  /** Present when firmware reports deferred MQTT NVS apply status (QUAL-01). */
  nvsOk?: boolean;
  /** True while a changing POST is queued or the network task is still applying (QUAL-04). */
  applyPending?: boolean;
}

export type UiLang = "de" | "en";
export type UiTheme = "dark" | "light" | "system";

export interface SettingsInfo {
  resetDays: number;
  lang: UiLang;
  theme: UiTheme;
  ledEnabled: boolean;
  audioTxEnabled: boolean;
  audioRxEnabled: boolean;
  audioTxVolume: number;
  audioRxVolume: number;
  quietHourStart: number;
  quietHourEnd: number;
  txHz: number;
  txMs: number;
  rxHz: number;
  rxMs: number;
  /** Present when firmware reports deferred-apply NVS status (QUAL-01). */
  nvsOk?: boolean;
  applyPending?: boolean;
}

export interface ApiOk {
  ok: true;
  message?: string;
  queued?: boolean;
  next?: string;
}

export interface ApiErr {
  ok: false;
  error: string;
}

export type ApiResult = ApiOk | ApiErr;

export type OtaChannel = "stable" | "beta";

export type OtaPhase =
  "idle" | "checking" | "available" | "downloading" | "verifying" | "rebooting" | "error";

export interface OtaStatus {
  phase: OtaPhase;
  channel: OtaChannel;
  localVersion: string;
  availableVersion: string;
  bytesDone: number;
  bytesTotal: number;
  error: string;
  generation: number;
}

/** Cold-boot snapshot from `/api/bootstrap` (PERF-07). */
export interface BootstrapPayload {
  csrf: { token: string; expiresInSeconds: number };
  device: DeviceInfo;
  wifi: WifiStatus;
  chaya: ChayaStatus | null;
  mqtt: MqttStatus | null;
  update: OtaStatus | null;
  settings: SettingsInfo | null;
}

import type {
  ChayaStatus,
  DeviceBatteryEvent,
  MqttStatus,
  OtaChannel,
  OtaPhase,
  OtaStatus,
  WifiScanAp,
  WifiScanSnapshot,
  WifiStatus,
} from "./types";

const OTA_PHASES = new Set<OtaPhase>([
  "idle",
  "checking",
  "available",
  "downloading",
  "verifying",
  "rebooting",
  "error",
]);

const OTA_CHANNELS = new Set<OtaChannel>(["stable", "beta"]);

function asFiniteNumber(value: unknown, fallback = 0): number {
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

function asBool(value: unknown, fallback = false): boolean {
  return typeof value === "boolean" ? value : fallback;
}

function asString(value: unknown, fallback = ""): string {
  return typeof value === "string" ? value : fallback;
}

/** Light runtime check for critical OTA JSON fields (FE-13). */
export function parseOtaStatus(data: unknown): OtaStatus {
  if (!data || typeof data !== "object") {
    throw new Error("invalid ota status");
  }
  const raw = data as Record<string, unknown>;
  const phase = raw.phase;
  if (typeof phase !== "string" || !OTA_PHASES.has(phase as OtaPhase)) {
    throw new Error(`invalid ota phase: ${String(phase)}`);
  }
  const channel = raw.channel === "beta" ? "beta" : "stable";
  if (typeof raw.channel === "string" && !OTA_CHANNELS.has(raw.channel as OtaChannel)) {
    throw new Error(`invalid ota channel: ${raw.channel}`);
  }
  return {
    phase: phase as OtaPhase,
    channel,
    localVersion: asString(raw.localVersion),
    availableVersion: asString(raw.availableVersion),
    bytesDone: asFiniteNumber(raw.bytesDone),
    bytesTotal: asFiniteNumber(raw.bytesTotal),
    error: asString(raw.error),
    generation: asFiniteNumber(raw.generation),
  };
}

export function parseChayaStatus(data: unknown): ChayaStatus {
  if (!data || typeof data !== "object") {
    throw new Error("invalid chaya status");
  }
  const raw = data as Record<string, unknown>;
  return {
    rx: asFiniteNumber(raw.rx),
    tx: asFiniteNumber(raw.tx),
    connected: asBool(raw.connected),
    configured: asBool(raw.configured),
    paired: asBool(raw.paired),
  };
}

export function parseWifiScanSnapshot(data: unknown): WifiScanSnapshot {
  if (!data || typeof data !== "object") {
    throw new Error("invalid wifi scan");
  }
  const raw = data as Record<string, unknown>;
  const status = raw.status;
  if (status === "ready") {
    if (!Array.isArray(raw.aps)) {
      throw new Error("invalid wifi scan aps");
    }
    const aps: WifiScanAp[] = raw.aps.map((row) => {
      if (!row || typeof row !== "object") {
        throw new Error("invalid wifi scan ap");
      }
      const ap = row as Record<string, unknown>;
      return {
        ssid: asString(ap.ssid),
        rssi: asFiniteNumber(ap.rssi),
        open: asBool(ap.open),
      };
    });
    return { status: "ready", aps };
  }
  if (status === "idle" || status === "pending" || status === "failed") {
    return { status };
  }
  throw new Error(`invalid wifi scan status: ${String(status)}`);
}

export function parseWifiStatus(data: unknown): WifiStatus {
  if (!data || typeof data !== "object") {
    throw new Error("invalid wifi status");
  }
  const raw = data as Record<string, unknown>;
  if (!asBool(raw.connected)) {
    return { connected: false };
  }
  return {
    connected: true,
    ssid: asString(raw.ssid),
    ip: asString(raw.ip),
    gateway: asString(raw.gateway),
    netmask: asString(raw.netmask),
    dns1: asString(raw.dns1),
    dns2: asString(raw.dns2),
    rssi: asFiniteNumber(raw.rssi),
  };
}

export function parseMqttStatus(data: unknown): MqttStatus {
  if (!data || typeof data !== "object") {
    throw new Error("invalid mqtt status");
  }
  const raw = data as Record<string, unknown>;
  return { connected: asBool(raw.connected) };
}

export function parseDeviceBattery(data: unknown): DeviceBatteryEvent {
  if (!data || typeof data !== "object") {
    throw new Error("invalid device battery");
  }
  const raw = data as Record<string, unknown>;
  return {
    batteryMv: asFiniteNumber(raw.batteryMv),
    batteryPct: asFiniteNumber(raw.batteryPct),
  };
}

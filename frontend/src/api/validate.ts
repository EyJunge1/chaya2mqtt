import type { OtaChannel, OtaPhase, OtaStatus } from "./types";

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
    localVersion: typeof raw.localVersion === "string" ? raw.localVersion : "",
    availableVersion: typeof raw.availableVersion === "string" ? raw.availableVersion : "",
    bytesDone: typeof raw.bytesDone === "number" && Number.isFinite(raw.bytesDone) ? raw.bytesDone : 0,
    bytesTotal:
      typeof raw.bytesTotal === "number" && Number.isFinite(raw.bytesTotal) ? raw.bytesTotal : 0,
    error: typeof raw.error === "string" ? raw.error : "",
    generation:
      typeof raw.generation === "number" && Number.isFinite(raw.generation) ? raw.generation : 0,
  };
}

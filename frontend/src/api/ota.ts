import type { OtaStatus } from "./types";

/** Normalize CalVer / tag strings for equality checks (strips leading `v`). */
export function normalizeOtaVersion(version: string): string {
  return version.trim().replace(/^v/i, "").toLowerCase();
}

/** True when firmware reports a pending update that is actually newer than local. */
export function otaHasPendingUpdate(ota: OtaStatus | null | undefined): boolean {
  if (!ota || ota.phase !== "available" || !ota.availableVersion) {
    return false;
  }
  const local = normalizeOtaVersion(ota.localVersion);
  const available = normalizeOtaVersion(ota.availableVersion);
  if (!local || !available) {
    return Boolean(available);
  }
  return available !== local;
}

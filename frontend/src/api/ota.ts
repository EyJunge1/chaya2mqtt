import type { OtaStatus } from "./types";

/** Normalize CalVer / tag strings for equality checks (strips leading `v`). */
export function normalizeOtaVersion(version: string): string {
  return version.trim().replace(/^v/i, "").toLowerCase();
}

/**
 * Same-boot stale snapshots (late GET) stay dropped.
 * A smaller generation is a new boot only after `rebooting` or a new `localVersion`.
 */
export function otaStatusIsStale(prev: OtaStatus | null | undefined, next: OtaStatus): boolean {
  if (prev == null) {
    return false;
  }
  if (next.generation > prev.generation) {
    return false;
  }
  if (next.generation === prev.generation) {
    return true;
  }
  return !(prev.phase === "rebooting" || next.localVersion !== prev.localVersion);
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

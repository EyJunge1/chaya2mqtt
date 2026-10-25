import { describe, expect, it } from "vitest";
import type { OtaStatus } from "./types";
import { normalizeOtaVersion, otaHasPendingUpdate, otaStatusIsStale } from "./ota";

function status(partial: Partial<OtaStatus>): OtaStatus {
  return {
    phase: "idle",
    channel: "stable",
    localVersion: "1.0.0",
    availableVersion: "",
    bytesDone: 0,
    bytesTotal: 0,
    error: "",
    generation: 1,
    ...partial,
  };
}

describe("ota helpers", () => {
  it("normalizes version tags", () => {
    expect(normalizeOtaVersion("v1.1.0-rc.1")).toBe("1.1.0-rc.1");
  });

  it("detects pending updates only when versions differ", () => {
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "1.1.0-rc.1",
          availableVersion: "1.1.0-rc.1",
        }),
      ),
    ).toBe(false);
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "1.0.0",
          availableVersion: "1.1.0-rc.1",
        }),
      ),
    ).toBe(true);
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "v1.0.0",
          availableVersion: "1.0.0",
        }),
      ),
    ).toBe(false);
  });

  it("keeps same-boot stale snapshots stale", () => {
    const prev = status({ phase: "downloading", generation: 5, localVersion: "1.0.0" });
    expect(
      otaStatusIsStale(prev, status({ phase: "idle", generation: 1, localVersion: "1.0.0" })),
    ).toBe(true);
    expect(otaStatusIsStale(prev, status({ phase: "downloading", generation: 5 }))).toBe(true);
    expect(otaStatusIsStale(prev, status({ phase: "verifying", generation: 6 }))).toBe(false);
  });

  it("accepts a post-reboot idle snapshot after rebooting", () => {
    const prev = status({
      phase: "rebooting",
      generation: 15,
      localVersion: "1.0.0",
      availableVersion: "1.0.1",
    });
    expect(
      otaStatusIsStale(
        prev,
        status({
          phase: "idle",
          generation: 1,
          localVersion: "1.0.1",
        }),
      ),
    ).toBe(false);
  });
});

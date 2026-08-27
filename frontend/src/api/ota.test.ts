import { describe, expect, it } from "vitest";
import type { OtaStatus } from "./types";
import { normalizeOtaVersion, otaHasPendingUpdate } from "./ota";

function status(partial: Partial<OtaStatus>): OtaStatus {
  return {
    phase: "idle",
    channel: "stable",
    localVersion: "2026.8.1",
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
    expect(normalizeOtaVersion("v2026.8.2-rc.1")).toBe("2026.8.2-rc.1");
  });

  it("detects pending updates only when versions differ", () => {
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "2026.8.2-rc.1",
          availableVersion: "2026.8.2-rc.1",
        }),
      ),
    ).toBe(false);
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "2026.8.1",
          availableVersion: "2026.8.2-rc.1",
        }),
      ),
    ).toBe(true);
    expect(
      otaHasPendingUpdate(
        status({
          phase: "available",
          localVersion: "v2026.8.1",
          availableVersion: "2026.8.1",
        }),
      ),
    ).toBe(false);
  });
});

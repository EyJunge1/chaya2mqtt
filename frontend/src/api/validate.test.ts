import { describe, expect, it } from "vitest";
import { parseOtaStatus, parseWifiScanSnapshot } from "./validate";

describe("parseOtaStatus", () => {
  it("accepts a valid payload", () => {
    expect(
      parseOtaStatus({
        phase: "available",
        channel: "beta",
        localVersion: "1",
        availableVersion: "2",
        bytesDone: 1,
        bytesTotal: 2,
        error: "",
        generation: 3,
      }),
    ).toEqual({
      phase: "available",
      channel: "beta",
      localVersion: "1",
      availableVersion: "2",
      bytesDone: 1,
      bytesTotal: 2,
      error: "",
      generation: 3,
    });
  });

  it("rejects unknown phases", () => {
    expect(() => parseOtaStatus({ phase: "flying", channel: "stable" })).toThrow(/phase/);
  });
});

describe("parseWifiScanSnapshot", () => {
  it("accepts idle pending and failed", () => {
    expect(parseWifiScanSnapshot({ status: "idle" })).toEqual({ status: "idle" });
    expect(parseWifiScanSnapshot({ status: "pending" })).toEqual({ status: "pending" });
    expect(parseWifiScanSnapshot({ status: "failed" })).toEqual({ status: "failed" });
  });

  it("accepts ready with aps", () => {
    expect(
      parseWifiScanSnapshot({
        status: "ready",
        aps: [{ ssid: "Home", rssi: -40, open: false }],
      }),
    ).toEqual({ status: "ready", aps: [{ ssid: "Home", rssi: -40, open: false }] });
  });

  it("rejects ready without aps and unknown status", () => {
    expect(() => parseWifiScanSnapshot({ status: "ready" })).toThrow(/aps/);
    expect(() => parseWifiScanSnapshot({ status: "scanning" })).toThrow(/status/);
  });
});

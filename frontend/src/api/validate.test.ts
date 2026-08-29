import { describe, expect, it } from "vitest";
import { parseOtaStatus } from "./validate";

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

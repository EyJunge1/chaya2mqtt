import { describe, expect, it } from "vitest";
import {
  isSha256Hex,
  parseSha256SidecarText,
  resolvePartUrl,
  sidecarUrlForPart,
} from "./flashVerify";

describe("flashVerify", () => {
  it("accepts 64-char hex digests", () => {
    expect(isSha256Hex("a".repeat(64))).toBe(true);
    expect(isSha256Hex("A1".repeat(32))).toBe(true);
    expect(isSha256Hex("short")).toBe(false);
    expect(isSha256Hex(undefined)).toBe(false);
  });

  it("maps bin parts to sha256 sidecars", () => {
    expect(sidecarUrlForPart("https://x/firmware.factory.bin")).toBe(
      "https://x/firmware.factory.sha256",
    );
    expect(sidecarUrlForPart("https://x/part")).toBe("https://x/part.sha256");
  });

  it("parses sidecar files", () => {
    expect(parseSha256SidecarText(`${"ab".repeat(32)}  firmware.bin\n`)).toBe("ab".repeat(32));
    expect(parseSha256SidecarText("not-a-hash")).toBeNull();
  });

  it("resolves relative same-origin part paths (SEC-10)", () => {
    expect(resolvePartUrl("firmware.factory.bin", "https://host/flash/manifest.json")).toBe(
      "https://host/flash/firmware.factory.bin",
    );
    expect(resolvePartUrl("../firmware.factory.bin", "https://host/dev/manifest.json")).toBe(
      "https://host/firmware.factory.bin",
    );
  });

  it("rejects absolute and cross-origin part paths (SEC-10)", () => {
    expect(() =>
      resolvePartUrl("https://evil.example/x.bin", "https://host/flash/manifest.json"),
    ).toThrow(/Absolute/);
    expect(() =>
      resolvePartUrl("//evil.example/x.bin", "https://host/flash/manifest.json"),
    ).toThrow(/Absolute/);
    expect(() => resolvePartUrl("http://other/x.bin", "https://host/flash/manifest.json")).toThrow(
      /Absolute/,
    );
  });
});

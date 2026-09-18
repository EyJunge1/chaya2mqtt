import { describe, expect, it } from "vitest";
import {
  isSha512Hex,
  parseSha512SidecarText,
  resolvePartUrl,
  sidecarUrlForPart,
} from "./flashVerify";

describe("flashVerify", () => {
  it("accepts 128-char hex digests", () => {
    expect(isSha512Hex("a".repeat(128))).toBe(true);
    expect(isSha512Hex("A1".repeat(64))).toBe(true);
    expect(isSha512Hex("a".repeat(64))).toBe(false);
    expect(isSha512Hex("short")).toBe(false);
    expect(isSha512Hex(undefined)).toBe(false);
  });

  it("maps bin parts to sha512 sidecars", () => {
    expect(sidecarUrlForPart("https://x/firmware.factory.bin")).toBe(
      "https://x/firmware.factory.sha512",
    );
    expect(() => sidecarUrlForPart("https://x/part")).toThrow(/\.bin/);
  });

  it("parses sidecar files", () => {
    expect(parseSha512SidecarText(`${"ab".repeat(64)}  firmware.bin\n`)).toBe("ab".repeat(64));
    expect(parseSha512SidecarText("not-a-hash")).toBeNull();
    expect(parseSha512SidecarText("a".repeat(64))).toBeNull();
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

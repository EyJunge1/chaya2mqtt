import { afterEach, describe, expect, it, vi } from "vitest";
import { copyText } from "./clipboard.ts";

function stubExecCommand(ok: boolean) {
  Object.defineProperty(document, "execCommand", {
    configurable: true,
    value: vi.fn().mockReturnValue(ok),
  });
  return document.execCommand as unknown as ReturnType<typeof vi.fn>;
}

describe("copyText", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
    Reflect.deleteProperty(document, "execCommand");
  });

  it("uses the clipboard API in a secure context", async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    vi.stubGlobal("isSecureContext", true);
    vi.stubGlobal("navigator", { clipboard: { writeText } });

    await expect(copyText("210b4a")).resolves.toBe(true);
    expect(writeText).toHaveBeenCalledWith("210b4a");
  });

  it("falls back when clipboard.writeText is rejected", async () => {
    vi.stubGlobal("isSecureContext", true);
    vi.stubGlobal("navigator", {
      clipboard: { writeText: vi.fn().mockRejectedValue(new Error("denied")) },
    });
    const execCommand = stubExecCommand(true);

    await expect(copyText("210b4a")).resolves.toBe(true);
    expect(execCommand).toHaveBeenCalledWith("copy");
  });

  it("falls back on an insecure HTTP page", async () => {
    const writeText = vi.fn();
    vi.stubGlobal("isSecureContext", false);
    vi.stubGlobal("navigator", { clipboard: { writeText } });
    const execCommand = stubExecCommand(true);

    await expect(copyText("210b4a")).resolves.toBe(true);
    expect(writeText).not.toHaveBeenCalled();
    expect(execCommand).toHaveBeenCalledWith("copy");
  });

  it("returns false when both APIs fail", async () => {
    vi.stubGlobal("isSecureContext", false);
    vi.stubGlobal("navigator", {});
    stubExecCommand(false);

    await expect(copyText("210b4a")).resolves.toBe(false);
  });

  it("returns false for empty text", async () => {
    await expect(copyText("")).resolves.toBe(false);
  });
});

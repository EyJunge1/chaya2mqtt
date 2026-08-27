import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { OtaStatus } from "../api/types.ts";
import UpdatePage from "./UpdatePage.svelte";

const { checkUpdate, installUpdate, getUpdateStatus, connectEvents } = vi.hoisted(() => ({
  checkUpdate: vi.fn(),
  installUpdate: vi.fn(),
  getUpdateStatus: vi.fn(),
  connectEvents: vi.fn(() => () => undefined),
}));

vi.mock("../api/client", () => ({
  api: {
    getUpdateStatus: () => getUpdateStatus(),
    checkUpdate: (channel?: string) => checkUpdate(channel),
    installUpdate: () => installUpdate(),
  },
}));

vi.mock("../api/sse", () => ({
  connectEvents: () => connectEvents(),
}));

vi.mock("../i18n/i18n.svelte.ts", () => ({
  i18n: {
    t: (key: string, params: Record<string, string | number> = {}) => {
      let text = key;
      for (const [k, v] of Object.entries(params)) {
        text = text.replaceAll(`{${k}}`, String(v));
      }
      return text;
    },
    language: "en",
    setLanguage: () => undefined,
  },
  useI18n: () => ({ t: (key: string) => key }),
}));

function status(partial: Partial<OtaStatus> = {}): OtaStatus {
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

describe("UpdatePage", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    getUpdateStatus.mockResolvedValue(status());
    checkUpdate.mockResolvedValue({ ok: true, message: "checking" });
    installUpdate.mockResolvedValue({ ok: true, message: "installing" });
  });

  it("loads status and checks with selected channel", async () => {
    const onToast = vi.fn();
    render(UpdatePage, { props: { onToast } });

    await screen.findByText("2026.8.1");
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "beta" } });
    fireEvent.click(screen.getByRole("button", { name: "update.check" }));

    await waitFor(() => {
      expect(checkUpdate).toHaveBeenCalledWith("beta");
    });
    expect(onToast).toHaveBeenCalledWith("toast.update-checking", "info");
  });

  it("does not overwrite a completed SSE check with the HTTP response", async () => {
    let resolveCheck!: (value: { ok: boolean; message: string }) => void;
    checkUpdate.mockImplementation(
      () =>
        new Promise((resolve) => {
          resolveCheck = resolve;
        }),
    );
    const onToast = vi.fn();
    const { rerender } = render(UpdatePage, {
      props: { onToast, otaStatus: status() },
    });

    await screen.findByText("2026.8.1");
    fireEvent.click(screen.getByRole("button", { name: "update.check" }));
    await waitFor(() => {
      expect(checkUpdate).toHaveBeenCalledWith("stable");
    });

    await rerender({
      onToast,
      otaStatus: status({
        phase: "available",
        availableVersion: "2026.8.2",
        generation: 3,
      }),
    });
    resolveCheck({ ok: true, message: "checking" });

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.update-checking", "info");
    });
    expect(screen.getByText("update.phase.available")).toBeInTheDocument();
    expect(screen.queryByText("update.phase.checking")).not.toBeInTheDocument();
  });

  it("confirms install when update is available", async () => {
    getUpdateStatus.mockResolvedValue(status({ phase: "available", availableVersion: "2026.8.2" }));
    const onToast = vi.fn();
    render(UpdatePage, { props: { onToast } });

    await screen.findByText("2026.8.2");
    const installButtons = screen.getAllByRole("button", { name: "update.install" });
    fireEvent.click(installButtons[0]!);
    const confirmButtons = screen.getAllByRole("button", { name: "update.install" });
    fireEvent.click(confirmButtons[confirmButtons.length - 1]!);

    await waitFor(() => {
      expect(installUpdate).toHaveBeenCalled();
    });
    expect(onToast).toHaveBeenCalledWith("toast.update-installing", "info");
  });
});

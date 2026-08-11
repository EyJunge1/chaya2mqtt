import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { UpdatePage } from "./UpdatePage";
import type { OtaStatus } from "../api/types";

const checkUpdate = vi.fn();
const installUpdate = vi.fn();
const getUpdateStatus = vi.fn();
const connectEvents = vi.fn(() => () => undefined);

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

vi.mock("../i18n/useI18n", () => ({
  useI18n: () => ({
    t: (key: string, params: Record<string, string | number> = {}) => {
      let text = key;
      for (const [k, v] of Object.entries(params)) {
        text = text.replaceAll(`{${k}}`, String(v));
      }
      return text;
    },
  }),
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
    render(<UpdatePage onToast={onToast} />);

    await screen.findByText("2026.8.1");
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "beta" } });
    fireEvent.click(screen.getByRole("button", { name: "update.check" }));

    await waitFor(() => {
      expect(checkUpdate).toHaveBeenCalledWith("beta");
    });
    expect(onToast).toHaveBeenCalledWith("toast.update-checking", "info");
  });

  it("confirms install when update is available", async () => {
    getUpdateStatus.mockResolvedValue(status({ phase: "available", availableVersion: "2026.8.2" }));
    const onToast = vi.fn();
    render(<UpdatePage onToast={onToast} />);

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

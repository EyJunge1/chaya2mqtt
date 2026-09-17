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

  it("toasts once when status is in error phase", async () => {
    getUpdateStatus.mockResolvedValue(status({ phase: "error", error: "install_failed" }));
    const onToast = vi.fn();
    const { rerender } = render(UpdatePage, { props: { onToast } });

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("update.error-title", "error");
    });
    expect(onToast).toHaveBeenCalledTimes(1);
    expect(screen.getByText("update.phase.error")).toBeInTheDocument();

    await rerender({
      onToast,
      otaStatus: status({ phase: "error", error: "install_failed", generation: 2 }),
    });
    expect(onToast).toHaveBeenCalledTimes(1);

    await rerender({
      onToast,
      otaStatus: status({ phase: "error", error: "download_failed", generation: 3 }),
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledTimes(2);
    });
    expect(onToast).toHaveBeenLastCalledWith("update.error-title", "error");
  });

  it("does not clobber checking with a same-generation snapshot", async () => {
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
    expect(screen.getByText("update.phase.checking")).toBeInTheDocument();

    await rerender({
      onToast,
      otaStatus: status({ phase: "idle", generation: 1 }),
    });

    expect(screen.getByText("update.phase.checking")).toBeInTheDocument();
    expect(screen.queryByText("update.phase.idle")).not.toBeInTheDocument();

    resolveCheck({ ok: true, message: "checking" });
  });

  it("skips check error restore if SSE generation rose", async () => {
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
    expect(screen.getByText("update.phase.available")).toBeInTheDocument();

    resolveCheck({ ok: false, message: "failed" });

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.update-failed", "error");
    });
    expect(screen.getByText("update.phase.available")).toBeInTheDocument();
    expect(screen.getByText("2026.8.2")).toBeInTheDocument();
    expect(screen.queryByText("update.phase.checking")).not.toBeInTheDocument();
    expect(screen.queryByText("update.phase.idle")).not.toBeInTheDocument();
  });

  it("keeps a dirty channel when a newer snapshot arrives", async () => {
    const onToast = vi.fn();
    const { rerender } = render(UpdatePage, {
      props: { onToast, otaStatus: status() },
    });

    await screen.findByText("2026.8.1");
    const select = screen.getByRole("combobox");
    fireEvent.change(select, { target: { value: "beta" } });
    expect(select).toHaveValue("beta");

    await rerender({
      onToast,
      otaStatus: status({
        phase: "idle",
        channel: "stable",
        generation: 2,
      }),
    });

    expect(screen.getByRole("combobox")).toHaveValue("beta");
  });

  it("ignores a late GET that is older than SSE status", async () => {
    let resolveGet!: (value: OtaStatus) => void;
    getUpdateStatus.mockImplementation(
      () =>
        new Promise<OtaStatus>((resolve) => {
          resolveGet = resolve;
        }),
    );
    const onToast = vi.fn();
    const { rerender } = render(UpdatePage, { props: { onToast } });

    await rerender({
      onToast,
      otaStatus: status({
        phase: "downloading",
        availableVersion: "2026.8.2",
        bytesDone: 500000,
        bytesTotal: 1000000,
        generation: 5,
      }),
    });
    await waitFor(() => {
      expect(screen.getByText("update.phase.downloading")).toBeInTheDocument();
    });

    resolveGet(
      status({
        phase: "idle",
        generation: 1,
      }),
    );

    await waitFor(() => {
      expect(getUpdateStatus).toHaveBeenCalled();
    });
    expect(screen.getByText("update.phase.downloading")).toBeInTheDocument();
    expect(screen.queryByText("update.phase.idle")).not.toBeInTheDocument();
  });

  it("leaves rebooting when SSE reports idle with a smaller generation after reboot", async () => {
    getUpdateStatus.mockImplementation(() => new Promise<OtaStatus>(() => undefined));
    const onToast = vi.fn();
    const { rerender } = render(UpdatePage, {
      props: {
        onToast,
        otaStatus: status({
          phase: "rebooting",
          localVersion: "2026.8.1",
          availableVersion: "2026.8.2",
          generation: 15,
        }),
      },
    });

    await waitFor(() => {
      expect(screen.getByText("update.phase.rebooting")).toBeInTheDocument();
    });

    await rerender({
      onToast,
      otaStatus: status({
        phase: "idle",
        localVersion: "2026.8.2",
        generation: 1,
      }),
    });

    expect(screen.getByText("update.phase.idle")).toBeInTheDocument();
    expect(screen.queryByText("update.phase.rebooting")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "update.check" })).toBeEnabled();
  });
});

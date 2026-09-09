import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ApiHttpError } from "../api/client.ts";
import { getLanguage, setLanguage } from "../i18n/store.ts";
import { getThemePreference, setTheme } from "../theme/store.ts";
// <script module> exports are available at runtime; generated Svelte types omit them.
// @ts-ignore svelte component types omit named module exports
import SettingsPage, { resetSettingsApplySession } from "./SettingsPage.svelte";

const getSettings = vi.fn();
const saveSettings = vi.fn();
const reboot = vi.fn();
const factoryReset = vi.fn();

vi.mock("../api/client", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../api/client.ts")>();
  return {
    ...actual,
    api: {
      getSettings: (...args: unknown[]) => getSettings(...args),
      saveSettings: (...args: unknown[]) => saveSettings(...args),
      reboot: (...args: unknown[]) => reboot(...args),
      factoryReset: (...args: unknown[]) => factoryReset(...args),
    },
  };
});

describe("SettingsPage", () => {
  afterEach(() => {
    cleanup();
    resetSettingsApplySession();
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    setLanguage("en");
    setTheme("light");
    getSettings.mockResolvedValue({
      resetDays: 7,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: false,
    });
    saveSettings.mockResolvedValue({ ok: true, message: "accepted" });
  });

  it("loads settings and saves the reset period", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    fireEvent.input(screen.getByDisplayValue("7"), { target: { value: "14" } });

    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() =>
      expect(saveSettings).toHaveBeenCalledWith({
        reset_days: 14,
        lang: "en",
        theme: "light",
        led_enabled: true,
        audio_tx_enabled: false,
        audio_rx_enabled: false,
        audio_tx_volume: 70,
        audio_rx_volume: 70,
        quiet_hour_start: 0,
        quiet_hour_end: 0,
        tx_hz: 880,
        tx_ms: 80,
        rx_hz: 660,
        rx_ms: 140,
      }),
    );
    await waitFor(() => expect(onToast).toHaveBeenCalledWith("Saved", "success"));
    expect(onDeviceRefresh).toHaveBeenCalled();
  });

  it("retries GET 503 busy during apply poll instead of save-failed", async () => {
    const loaded = {
      resetDays: 7,
      lang: "en" as const,
      theme: "light" as const,
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: false,
    };
    getSettings
      .mockResolvedValueOnce(loaded)
      .mockRejectedValueOnce(new ApiHttpError("/api/settings", 503, "busy"))
      .mockResolvedValueOnce({ ...loaded, applyPending: false });
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() => expect(onToast).toHaveBeenCalledWith("Saved", "success"));
    expect(onToast).not.toHaveBeenCalledWith("Save failed", "error");
    expect(onDeviceRefresh).toHaveBeenCalled();
  });

  it("shows save-failed when nvsOk is false after accept", async () => {
    getSettings
      .mockResolvedValueOnce({
        resetDays: 7,
        lang: "en",
        theme: "light",
        ledEnabled: true,
        audioTxEnabled: false,
        audioRxEnabled: false,
        audioTxVolume: 70,
        audioRxVolume: 70,
        quietHourStart: 0,
        quietHourEnd: 0,
        txHz: 880,
        txMs: 80,
        rxHz: 660,
        rxMs: 140,
        nvsOk: true,
        applyPending: false,
      })
      .mockResolvedValue({
        resetDays: 7,
        lang: "en",
        theme: "light",
        ledEnabled: true,
        audioTxEnabled: false,
        audioRxEnabled: false,
        audioTxVolume: 70,
        audioRxVolume: 70,
        quietHourStart: 0,
        quietHourEnd: 0,
        txHz: 880,
        txMs: 80,
        rxHz: 660,
        rxMs: 140,
        nvsOk: false,
        applyPending: false,
      });
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() => expect(onToast).toHaveBeenCalledWith("Save failed", "error"));
    expect(onDeviceRefresh).not.toHaveBeenCalled();
  });

  it("saves the status LED switch", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    await waitFor(() =>
      expect(screen.getByRole("switch", { name: "Status LED" })).toBeInTheDocument(),
    );
    const toggle = screen.getByRole("switch", { name: "Status LED" });
    expect(toggle).toHaveAttribute("aria-checked", "true");

    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute("aria-checked", "false");

    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[1]!);
    await waitFor(() =>
      expect(saveSettings).toHaveBeenCalledWith(
        expect.objectContaining({
          led_enabled: false,
          lang: "en",
          theme: "light",
        }),
      ),
    );
  });

  it("saves current store lang and theme, not the loaded settings snapshot", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());

    setLanguage("de");
    setTheme("dark");
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() =>
      expect(saveSettings).toHaveBeenCalledWith(
        expect.objectContaining({
          lang: "de",
          theme: "dark",
          reset_days: 7,
        }),
      ),
    );
  });

  it("shows save-failed when applyPending never clears", async () => {
    const pending = {
      resetDays: 7,
      lang: "en" as const,
      theme: "light" as const,
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    };
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());

    getSettings.mockResolvedValue(pending);
    vi.useFakeTimers();
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await Promise.resolve();
    await Promise.resolve();
    expect(saveSettings).toHaveBeenCalled();
    await vi.advanceTimersByTimeAsync(30_200);
    expect(onToast).toHaveBeenCalledWith("Save failed", "error");
    expect(onToast).not.toHaveBeenCalledWith("Saved", "success");
    expect(onDeviceRefresh).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue("7")).toBeInTheDocument();
  });

  it("refreshes after factory reset and ignores a failed refresh", async () => {
    factoryReset.mockResolvedValue({ ok: true, message: "factory_reset" });
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockRejectedValue(new Error("offline"));
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());

    fireEvent.click(screen.getByRole("button", { name: "Delete everything" }));
    await waitFor(() => expect(screen.getByText("Reset all device data?")).toBeInTheDocument());
    const confirms = screen.getAllByRole("button", { name: "Delete everything" });
    fireEvent.click(confirms[confirms.length - 1]!);

    await waitFor(() => expect(factoryReset).toHaveBeenCalled());
    await waitFor(() => expect(onDeviceRefresh).toHaveBeenCalled());
    await waitFor(() =>
      expect(onToast).toHaveBeenCalledWith(expect.stringMatching(/Factory reset started/), "info"),
    );
    expect(onToast).not.toHaveBeenCalledWith("Factory reset could not be started", "error");
  });

  it("keeps submitted reset days when remounting during applyPending", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    getSettings.mockResolvedValue({
      resetDays: 7,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    });
    fireEvent.input(screen.getByDisplayValue("7"), { target: { value: "14" } });
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(1));
    cleanup();
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    expect(await screen.findByDisplayValue("14")).toBeInTheDocument();
    expect(screen.queryByDisplayValue("7")).not.toBeInTheDocument();

    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await new Promise((r) => setTimeout(r, 30));
    expect(saveSettings).toHaveBeenCalledTimes(1);
    expect(saveSettings).not.toHaveBeenCalledWith(expect.objectContaining({ reset_days: 7 }));
  });

  it("does not hydrate live GET fields on a cold load while applyPending", async () => {
    resetSettingsApplySession();
    const pending = {
      resetDays: 7,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    };
    getSettings.mockReset();
    getSettings
      .mockResolvedValueOnce(pending)
      .mockResolvedValue({ ...pending, resetDays: 14, applyPending: false });

    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    expect(await screen.findByText("Loading settings…")).toBeTruthy();
    expect(screen.queryByDisplayValue("7")).not.toBeInTheDocument();
    expect(await screen.findByDisplayValue("14")).toBeInTheDocument();
  });

  it("makes save clickable again after remount when applyPending is already false", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    getSettings.mockResolvedValue({
      resetDays: 7,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    });
    fireEvent.input(screen.getByDisplayValue("7"), { target: { value: "14" } });
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(1));

    cleanup();
    getSettings.mockResolvedValue({
      resetDays: 14,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: false,
    });
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    await waitFor(() => expect(screen.getByDisplayValue("14")).toBeInTheDocument());
    const save = screen.getAllByRole("button", { name: "Save" })[0]!;
    await waitFor(() => expect(save).not.toHaveAttribute("aria-busy", "true"));
    expect(save).toBeEnabled();
    fireEvent.click(save);
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(2));
  });

  it("keeps submitted form when remount GET fails", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    getSettings.mockResolvedValue({
      resetDays: 7,
      lang: "en",
      theme: "light",
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    });
    fireEvent.input(screen.getByDisplayValue("7"), { target: { value: "14" } });
    fireEvent.click(screen.getAllByRole("button", { name: "Save" })[0]!);
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(1));

    cleanup();
    getSettings.mockRejectedValue(new Error("offline"));
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    expect(await screen.findByDisplayValue("14")).toBeInTheDocument();
    expect(screen.queryByText("Could not load settings.")).not.toBeInTheDocument();
    const save = screen.getAllByRole("button", { name: "Save" })[0]!;
    await waitFor(() => expect(save).not.toHaveAttribute("aria-busy", "true"));
    expect(save).toBeEnabled();
  });

  it("shows load-error when apply poll GET fails on a cold load", async () => {
    resetSettingsApplySession();
    getSettings.mockReset();
    getSettings
      .mockResolvedValueOnce({
        resetDays: 7,
        lang: "en",
        theme: "light",
        ledEnabled: true,
        audioTxEnabled: false,
        audioRxEnabled: false,
        audioTxVolume: 70,
        audioRxVolume: 70,
        quietHourStart: 0,
        quietHourEnd: 0,
        txHz: 880,
        txMs: 80,
        rxHz: 660,
        rxMs: 140,
        nvsOk: true,
        applyPending: true,
      })
      .mockRejectedValue(new Error("offline"));
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    expect(await screen.findByText("Could not load settings.")).toBeTruthy();
    expect(screen.queryByText("Loading settings…")).toBeNull();
  });

  it("shows load-error instead of a spinner when cold-load applyPending never clears", async () => {
    resetSettingsApplySession();
    vi.useFakeTimers();
    try {
      getSettings.mockReset();
      getSettings.mockResolvedValue({
        resetDays: 7,
        lang: "en",
        theme: "light",
        ledEnabled: true,
        audioTxEnabled: false,
        audioRxEnabled: false,
        audioTxVolume: 70,
        audioRxVolume: 70,
        quietHourStart: 0,
        quietHourEnd: 0,
        txHz: 880,
        txMs: 80,
        rxHz: 660,
        rxMs: 140,
        nvsOk: true,
        applyPending: true,
      });
      const onToast = vi.fn();
      const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
      render(SettingsPage, { props: { onToast, onDeviceRefresh } });

      await Promise.resolve();
      await Promise.resolve();
      expect(screen.getByText("Loading settings…")).toBeTruthy();

      await vi.advanceTimersByTimeAsync(30_200);
      expect(screen.getByText("Could not load settings.")).toBeTruthy();
      expect(screen.queryByText("Loading settings…")).toBeNull();
    } finally {
      vi.useRealTimers();
    }
  });

  it("toasts save-failed when reboot is busy", async () => {
    reboot.mockResolvedValue({ ok: false, error: "busy" });
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());

    fireEvent.click(screen.getByRole("button", { name: "Reboot device" }));
    await waitFor(() => expect(screen.getByText("Confirm reboot")).toBeInTheDocument());
    fireEvent.click(screen.getByRole("button", { name: "Reboot" }));

    await waitFor(() => expect(onToast).toHaveBeenCalledWith("Save failed", "error"));
    expect(onToast).not.toHaveBeenCalledWith("Reboot failed", "error");
  });

  it("applies device lang and theme after apply-pending resume", async () => {
    resetSettingsApplySession();
    setLanguage("en");
    setTheme("light");
    const pending = {
      resetDays: 7,
      lang: "de" as const,
      theme: "dark" as const,
      ledEnabled: true,
      audioTxEnabled: false,
      audioRxEnabled: false,
      audioTxVolume: 70,
      audioRxVolume: 70,
      quietHourStart: 0,
      quietHourEnd: 0,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
      nvsOk: true,
      applyPending: true,
    };
    getSettings.mockReset();
    getSettings
      .mockResolvedValueOnce(pending)
      .mockResolvedValue({ ...pending, applyPending: false });

    render(SettingsPage, { props: { onToast: vi.fn(), onDeviceRefresh: vi.fn() } });
    await waitFor(() => expect(screen.getByDisplayValue("7")).toBeInTheDocument());
    expect(getLanguage()).toBe("de");
    expect(getThemePreference()).toBe("dark");
  });

  it("starts apply poll instead of load-error when cold-load GET is 503 busy", async () => {
    resetSettingsApplySession();
    getSettings.mockReset();
    getSettings
      .mockRejectedValueOnce(new ApiHttpError("/api/settings", 503, "busy"))
      .mockResolvedValue({
        resetDays: 7,
        lang: "en",
        theme: "light",
        ledEnabled: true,
        audioTxEnabled: false,
        audioRxEnabled: false,
        audioTxVolume: 70,
        audioRxVolume: 70,
        quietHourStart: 0,
        quietHourEnd: 0,
        txHz: 880,
        txMs: 80,
        rxHz: 660,
        rxMs: 140,
        nvsOk: true,
        applyPending: false,
      });

    render(SettingsPage, { props: { onToast: vi.fn(), onDeviceRefresh: vi.fn() } });
    expect(await screen.findByDisplayValue("7")).toBeInTheDocument();
    expect(screen.queryByText("Could not load settings.")).not.toBeInTheDocument();
  });
});

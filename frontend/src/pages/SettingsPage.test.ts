import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import SettingsPage from "./SettingsPage.svelte";

const getSettings = vi.fn();
const saveSettings = vi.fn();
const reboot = vi.fn();
const factoryReset = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    getSettings: (...args: unknown[]) => getSettings(...args),
    saveSettings: (...args: unknown[]) => saveSettings(...args),
    reboot: (...args: unknown[]) => reboot(...args),
    factoryReset: (...args: unknown[]) => factoryReset(...args),
  },
}));

describe("SettingsPage", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    setLanguage("en");
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
        led_enabled: 1,
        audio_tx_enabled: 0,
        audio_rx_enabled: 0,
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
          led_enabled: 0,
        }),
      ),
    );
  });
});

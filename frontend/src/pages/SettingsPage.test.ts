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
      audioMuted: false,
      audioVolume: 70,
      quietHourStart: 23,
      quietHourEnd: 8,
      audioCustom: false,
      txHz: 880,
      txMs: 80,
      rxHz: 660,
      rxMs: 140,
    });
    saveSettings.mockResolvedValue({ ok: true, message: "saved" });
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
        audio_muted: 0,
        audio_volume: 70,
        quiet_hour_start: 23,
        quiet_hour_end: 8,
        audio_custom: 0,
        tx_hz: 95,
        tx_ms: 80,
        rx_hz: 88,
        rx_ms: 140,
      }),
    );
    expect(onToast).toHaveBeenCalledWith("Saved", "success");
    expect(onDeviceRefresh).toHaveBeenCalled();
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

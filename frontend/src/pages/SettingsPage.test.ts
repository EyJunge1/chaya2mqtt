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
      displayDark: false,
      audioMuted: false,
      audioVolume: 70,
      quietHourStart: 23,
      quietHourEnd: 8,
    });
    saveSettings.mockResolvedValue({ ok: true, message: "saved" });
  });

  it("loads settings and saves display dark mode with reset days", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(SettingsPage, { props: { onToast, onDeviceRefresh } });

    await waitFor(() =>
      expect(screen.getByRole("switch", { name: "E-Ink dark mode" })).toBeInTheDocument(),
    );
    const toggle = screen.getByRole("switch", { name: "E-Ink dark mode" });
    expect(toggle).toHaveAttribute("aria-checked", "false");

    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute("aria-checked", "true");

    fireEvent.click(screen.getByRole("button", { name: "Save" }));
    await waitFor(() =>
      expect(saveSettings).toHaveBeenCalledWith({
        reset_days: 7,
        display_dark: 1,
        audio_muted: 0,
        audio_volume: 70,
        quiet_hour_start: 23,
        quiet_hour_end: 8,
      }),
    );
    expect(onToast).toHaveBeenCalledWith("Saved", "success");
    expect(onDeviceRefresh).toHaveBeenCalled();
  });
});

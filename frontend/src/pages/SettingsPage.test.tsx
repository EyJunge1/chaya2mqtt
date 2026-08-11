import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { SettingsPage } from "./SettingsPage";

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
    });
    saveSettings.mockResolvedValue({ ok: true, message: "saved" });
  });

  it("loads settings and saves display dark mode with reset days", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(
      <I18nProvider>
        <SettingsPage onToast={onToast} onDeviceRefresh={onDeviceRefresh} />
      </I18nProvider>,
    );

    await waitFor(() => expect(screen.getByRole("switch")).toBeInTheDocument());
    const toggle = screen.getByRole("switch", { name: "E-Ink dark mode" });
    expect(toggle).toHaveAttribute("aria-checked", "false");

    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute("aria-checked", "true");

    fireEvent.click(screen.getByRole("button", { name: "Save" }));
    await waitFor(() =>
      expect(saveSettings).toHaveBeenCalledWith({
        reset_days: 7,
        display_dark: 1,
      }),
    );
    expect(onToast).toHaveBeenCalledWith("Saved", "success");
    expect(onDeviceRefresh).toHaveBeenCalled();
  });
});

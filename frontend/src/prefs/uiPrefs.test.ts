import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import { setTheme } from "../theme/store.ts";

const { getSettings, saveSettings } = vi.hoisted(() => ({
  getSettings: vi.fn(),
  saveSettings: vi.fn(),
}));

vi.mock("../api/client.ts", () => ({
  api: {
    getSettings: () => getSettings(),
    saveSettings: (fields: unknown) => saveSettings(fields),
  },
}));

import { enqueueSettingsWrite, flushUiPrefsPersist, persistUiPrefsDebounced } from "./uiPrefs.ts";

describe("uiPrefs", () => {
  afterEach(async () => {
    await flushUiPrefsPersist();
    vi.clearAllMocks();
    setLanguage("en");
    setTheme("light");
  });

  beforeEach(() => {
    setLanguage("en");
    setTheme("light");
    getSettings.mockResolvedValue({ applyPending: false });
    saveSettings.mockResolvedValue({ ok: true });
  });

  it("flush executes the pending prefs write instead of dropping it", async () => {
    persistUiPrefsDebounced(10_000);
    expect(saveSettings).not.toHaveBeenCalled();
    await flushUiPrefsPersist();
    expect(saveSettings).toHaveBeenCalledWith({ lang: "en", theme: "light" });
    expect(saveSettings).toHaveBeenCalledTimes(1);
  });

  it("skips prefs POST when applyPending stays true", async () => {
    getSettings.mockResolvedValue({ applyPending: true });
    persistUiPrefsDebounced(0);
    await vi.waitFor(() => expect(getSettings).toHaveBeenCalled());
    await enqueueSettingsWrite(async () => undefined);
    expect(saveSettings).not.toHaveBeenCalled();
  });

  it("retries prefs persist after applyPending clears", async () => {
    getSettings.mockResolvedValue({ applyPending: true });
    persistUiPrefsDebounced(0);
    await vi.waitFor(() => expect(getSettings).toHaveBeenCalled());
    await enqueueSettingsWrite(async () => undefined);
    expect(saveSettings).not.toHaveBeenCalled();

    getSettings.mockResolvedValue({ applyPending: false });
    await vi.waitFor(() => expect(saveSettings).toHaveBeenCalledWith({ lang: "en", theme: "light" }), {
      timeout: 2000,
    });
  });

  it("waits until applyPending is false before prefs POST", async () => {
    getSettings
      .mockResolvedValueOnce({ applyPending: true })
      .mockResolvedValueOnce({ applyPending: false });
    persistUiPrefsDebounced(0);
    await vi.waitFor(() => expect(saveSettings).toHaveBeenCalledWith({ lang: "en", theme: "light" }));
    expect(getSettings).toHaveBeenCalledTimes(2);
  });

  it("does not run prefs persist and another settings write in parallel", async () => {
    let inFlight = 0;
    let maxInFlight = 0;
    saveSettings.mockImplementation(async () => {
      inFlight += 1;
      maxInFlight = Math.max(maxInFlight, inFlight);
      await new Promise((r) => setTimeout(r, 30));
      inFlight -= 1;
      return { ok: true };
    });

    const deviceSave = enqueueSettingsWrite(() => saveSettings({ reset_days: 14 }));
    persistUiPrefsDebounced(0);
    await deviceSave;
    await vi.waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(2));
    expect(maxInFlight).toBe(1);
  });
});

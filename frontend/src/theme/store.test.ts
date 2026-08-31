import { afterEach, describe, expect, it, vi } from "vitest";
import { getTheme, getThemePreference, setTheme, subscribeTheme, toggleTheme } from "./store";

afterEach(() => {
  vi.unstubAllGlobals();
  window.localStorage.clear();
  setTheme("system");
});

describe("theme store", () => {
  it("defaults to system preference and resolves via matchMedia", () => {
    const matchMedia = vi.fn().mockReturnValue({
      matches: true,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    });
    vi.stubGlobal("matchMedia", matchMedia);

    setTheme("system");
    expect(getThemePreference()).toBe("system");
    expect(getTheme()).toBe("dark");
    expect(document.documentElement.dataset.theme).toBe("dark");
  });

  it("cycles system → light → dark → system and persists preference", () => {
    setTheme("system");
    expect(toggleTheme()).toBe("light");
    expect(getTheme()).toBe("light");
    expect(window.localStorage.getItem("chaya2mqtt.theme")).toBe("light");
    expect(document.documentElement.dataset.theme).toBe("light");

    expect(toggleTheme()).toBe("dark");
    expect(getTheme()).toBe("dark");
    expect(window.localStorage.getItem("chaya2mqtt.theme")).toBe("dark");

    expect(toggleTheme()).toBe("system");
    expect(getThemePreference()).toBe("system");
  });

  it("falls back to light when matchMedia is unavailable", () => {
    vi.stubGlobal("matchMedia", undefined);
    setTheme("system");
    expect(getTheme()).toBe("light");
  });

  it("notifies subscribers", () => {
    let calls = 0;
    const unsub = subscribeTheme(() => {
      calls += 1;
    });
    setTheme("dark");
    expect(calls).toBe(1);
    unsub();
    setTheme("light");
    expect(calls).toBe(1);
  });
});

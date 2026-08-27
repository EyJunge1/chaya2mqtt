import { afterEach, describe, expect, it } from "vitest";
import { getTheme, setTheme, subscribeTheme, toggleTheme } from "./store";

afterEach(() => {
  setTheme("light");
  window.localStorage.clear();
});

describe("theme store", () => {
  it("toggles and persists theme", () => {
    expect(getTheme()).toBe("light");
    expect(toggleTheme()).toBe("dark");
    expect(window.localStorage.getItem("chaya2mqtt.theme")).toBe("dark");
    expect(document.documentElement.dataset.theme).toBe("dark");
    setTheme("light");
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

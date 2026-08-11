import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { InfoTip } from "./InfoTip";

function mockMatchMedia(matches: boolean) {
  Object.defineProperty(window, "matchMedia", {
    writable: true,
    configurable: true,
    value: vi.fn().mockImplementation((query: string) => ({
      matches,
      media: query,
      onchange: null,
      addListener: vi.fn(),
      removeListener: vi.fn(),
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      dispatchEvent: vi.fn(),
    })),
  });
}

function renderInfoTip(text = "Port details") {
  setLanguage("en");
  return render(
    <I18nProvider>
      <div>
        <button type="button">Outside</button>
        <InfoTip text={text} />
      </div>
    </I18nProvider>,
  );
}

beforeEach(() => {
  mockMatchMedia(true);
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe("InfoTip", () => {
  it("shows tooltip only while hovering the icon on hover devices", () => {
    mockMatchMedia(true);
    renderInfoTip();

    const trigger = screen.getByRole("button", { name: "More information" });
    const tooltip = screen.getByRole("tooltip");

    expect(trigger).not.toHaveAttribute("aria-expanded");
    expect(trigger).toHaveAttribute("aria-describedby");
    expect(trigger.className).toContain("cursor-not-allowed");
    expect(trigger.className).not.toContain("bg-accent/15");
    expect(tooltip).toHaveTextContent("Port details");
    expect(tooltip.className).toContain("invisible");
    expect(tooltip.className).not.toMatch(/(?:^|\s)opacity-100(?:\s|$)/);

    fireEvent.mouseEnter(trigger);
    expect(trigger.className).toContain("bg-accent/15");
    expect(trigger.className).toContain("text-accent");
    expect(tooltip.className).toMatch(/(?:^|\s)opacity-100(?:\s|$)/);
    expect(tooltip.className).toContain("visible");

    fireEvent.click(trigger);
    expect(trigger).not.toHaveAttribute("aria-expanded");
    expect(tooltip.className).toMatch(/(?:^|\s)opacity-100(?:\s|$)/);

    fireEvent.mouseLeave(trigger);
    expect(trigger.className).not.toContain("bg-accent/15");
    expect(tooltip.className).toContain("invisible");
    expect(tooltip.className).not.toMatch(/(?:^|\s)opacity-100(?:\s|$)/);
  });

  it("toggles tooltip visibility on click for touch devices", () => {
    mockMatchMedia(false);
    renderInfoTip();

    const trigger = screen.getByRole("button", { name: "More information" });
    const tooltip = screen.getByRole("tooltip");

    expect(trigger).toHaveAttribute("aria-expanded", "false");
    expect(trigger.className).toContain("cursor-pointer");
    expect(tooltip.className).not.toMatch(/(?:^|\s)opacity-100(?:\s|$)/);

    fireEvent.click(trigger);
    expect(trigger).toHaveAttribute("aria-expanded", "true");
    expect(tooltip.className).toMatch(/(?:^|\s)opacity-100(?:\s|$)/);

    fireEvent.click(trigger);
    expect(trigger).toHaveAttribute("aria-expanded", "false");
    expect(tooltip.className).not.toMatch(/(?:^|\s)opacity-100(?:\s|$)/);
  });

  it("closes open tooltip on outside click for touch devices", () => {
    mockMatchMedia(false);
    renderInfoTip();

    const trigger = screen.getByRole("button", { name: "More information" });
    fireEvent.click(trigger);
    expect(trigger).toHaveAttribute("aria-expanded", "true");

    fireEvent.pointerDown(screen.getByRole("button", { name: "Outside" }));
    expect(trigger).toHaveAttribute("aria-expanded", "false");
  });
});

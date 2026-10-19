import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import { afterEach, describe, expect, it, vi } from "vitest";
import FormHarness from "./Form.test.svelte";

afterEach(() => {
  cleanup();
});

describe("PrimaryButton", () => {
  it("exposes busy state while loading", () => {
    render(FormHarness, { props: { kind: "button", loading: true } });
    const btn = screen.getByRole("button", { name: /Speichern/i });
    expect(btn).toBeDisabled();
    expect(btn).toHaveAttribute("aria-busy", "true");
  });
});

describe("Switch", () => {
  it("toggles checked state via role=switch", () => {
    const onChange = vi.fn();
    render(FormHarness, { props: { kind: "switch", checked: false, onChange } });
    const sw = screen.getByRole("switch", { name: "Dark" });
    expect(sw).toHaveAttribute("aria-checked", "false");
    fireEvent.click(sw);
    expect(onChange).toHaveBeenCalledWith(true);
  });
});

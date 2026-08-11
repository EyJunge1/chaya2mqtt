import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { PrimaryButton, Switch } from "./Form";

afterEach(() => {
  cleanup();
});

describe("PrimaryButton", () => {
  it("exposes busy state while loading", () => {
    render(
      <PrimaryButton loading type="button">
        Speichern
      </PrimaryButton>,
    );
    const btn = screen.getByRole("button", { name: /Speichern/i });
    expect(btn).toBeDisabled();
    expect(btn).toHaveAttribute("aria-busy", "true");
  });
});

describe("Switch", () => {
  it("toggles checked state via role=switch", () => {
    const onChange = vi.fn();
    render(<Switch label="Dark" checked={false} onChange={onChange} />);
    const sw = screen.getByRole("switch", { name: "Dark" });
    expect(sw).toHaveAttribute("aria-checked", "false");
    fireEvent.click(sw);
    expect(onChange).toHaveBeenCalledWith(true);
  });
});

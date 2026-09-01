import { cleanup, render, screen } from "@testing-library/svelte";
import userEvent from "@testing-library/user-event";
import { afterEach, describe, expect, it, vi } from "vitest";
import SegmentedControl from "./SegmentedControl.svelte";

const options = [
  { value: "dhcp", label: "DHCP" },
  { value: "static", label: "Statisch" },
] as const;

afterEach(() => {
  cleanup();
});

describe("SegmentedControl", () => {
  it("keeps a single tab stop on the checked radio", () => {
    render(SegmentedControl, {
      props: {
        value: "static",
        onChange: () => undefined,
        options: [...options],
        label: "IP",
      },
    });

    const dhcp = screen.getByRole("radio", { name: "DHCP" });
    const statisch = screen.getByRole("radio", { name: "Statisch" });
    expect(statisch).toHaveAttribute("tabindex", "0");
    expect(dhcp).toHaveAttribute("tabindex", "-1");
    expect(statisch).toHaveAttribute("aria-checked", "true");
    expect(dhcp).toHaveAttribute("aria-checked", "false");
  });

  it("moves DOM focus with the selection on arrow keys", async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    render(SegmentedControl, {
      props: {
        value: "dhcp",
        onChange,
        options: [...options],
        label: "IP",
      },
    });

    const dhcp = screen.getByRole("radio", { name: "DHCP" });
    const statisch = screen.getByRole("radio", { name: "Statisch" });
    dhcp.focus();
    expect(dhcp).toHaveFocus();

    await user.keyboard("{ArrowRight}");
    expect(onChange).toHaveBeenCalledWith("static");
    expect(statisch).toHaveFocus();

    onChange.mockClear();
    await user.keyboard(" ");
    expect(onChange).toHaveBeenCalledWith("static");
    expect(onChange).not.toHaveBeenCalledWith("dhcp");
  });

  it("moves focus to first and last radio on Home and End", async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    render(SegmentedControl, {
      props: {
        value: "dhcp",
        onChange,
        options: [...options],
        label: "IP",
      },
    });

    screen.getByRole("radio", { name: "DHCP" }).focus();
    await user.keyboard("{End}");
    expect(onChange).toHaveBeenCalledWith("static");
    expect(screen.getByRole("radio", { name: "Statisch" })).toHaveFocus();

    onChange.mockClear();
    await user.keyboard("{Home}");
    expect(onChange).toHaveBeenCalledWith("dhcp");
    expect(screen.getByRole("radio", { name: "DHCP" })).toHaveFocus();
  });
});

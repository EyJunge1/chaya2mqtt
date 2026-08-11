import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { ConfirmDialog } from "./ConfirmDialog";

afterEach(() => {
  cleanup();
});

describe("ConfirmDialog", () => {
  it("calls confirm and cancel handlers", () => {
    const onConfirm = vi.fn();
    const onCancel = vi.fn();
    render(
      <ConfirmDialog
        open
        title="Neustart"
        description="Wirklich?"
        confirmLabel="Neu starten"
        cancelLabel="Abbrechen"
        onConfirm={onConfirm}
        onCancel={onCancel}
      />,
    );
    expect(screen.getByRole("dialog")).toHaveAttribute("aria-labelledby");
    expect(screen.getByRole("dialog")).toHaveAttribute("aria-describedby");
    fireEvent.click(screen.getByRole("button", { name: "Neu starten" }));
    fireEvent.click(screen.getByRole("button", { name: "Abbrechen" }));
    expect(onConfirm).toHaveBeenCalledTimes(1);
    expect(onCancel).toHaveBeenCalledTimes(1);
  });

  it("supports a primary confirm variant", () => {
    render(
      <ConfirmDialog
        open
        title="Install"
        description="Go ahead?"
        confirmLabel="Install"
        cancelLabel="Cancel"
        confirmVariant="primary"
        onConfirm={() => undefined}
        onCancel={() => undefined}
      />,
    );
    expect(screen.getByRole("button", { name: "Install" })).toBeInTheDocument();
  });
});

import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { Toast } from "./Toast";

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("Toast", () => {
  it("renders success toast", () => {
    render(<Toast message={{ text: "Gespeichert", variant: "success" }} onClose={() => {}} />);
    expect(screen.getByRole("status")).toHaveTextContent("Gespeichert");
  });

  it("renders error toast as alert", () => {
    render(<Toast message={{ text: "Fehler", variant: "error" }} onClose={() => {}} />);
    expect(screen.getByRole("alert")).toHaveTextContent("Fehler");
  });

  it("auto-closes after timeout", () => {
    vi.useFakeTimers();
    const onClose = vi.fn();
    render(<Toast message={{ text: "Info", variant: "info" }} onClose={onClose} />);
    vi.advanceTimersByTime(3200);
    expect(onClose).toHaveBeenCalledTimes(1);
  });
});

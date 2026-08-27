import { cleanup, render, screen } from "@testing-library/svelte";
import { afterEach, describe, expect, it, vi } from "vitest";
import Toast from "./Toast.svelte";
import { pushToast, type ToastItem } from "./toastStack.ts";

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("ToastStack", () => {
  it("renders success toast", () => {
    const toasts: ToastItem[] = [{ id: "1", text: "Gespeichert", variant: "success" }];
    render(Toast, { props: { toasts, onDismiss: () => {} } });
    expect(screen.getByRole("status")).toHaveTextContent("Gespeichert");
  });

  it("renders error toast as alert", () => {
    const toasts: ToastItem[] = [{ id: "1", text: "Fehler", variant: "error" }];
    render(Toast, { props: { toasts, onDismiss: () => {} } });
    expect(screen.getByRole("alert")).toHaveTextContent("Fehler");
  });

  it("stacks multiple toasts", () => {
    const toasts: ToastItem[] = [
      { id: "1", text: "Eins", variant: "info" },
      { id: "2", text: "Zwei", variant: "success" },
    ];
    render(Toast, { props: { toasts, onDismiss: () => {} } });
    expect(screen.getByText("Eins")).toBeInTheDocument();
    expect(screen.getByText("Zwei")).toBeInTheDocument();
  });

  it("auto-closes after timeout", () => {
    vi.useFakeTimers();
    const onDismiss = vi.fn();
    const toasts: ToastItem[] = [{ id: "1", text: "Info", variant: "info" }];
    render(Toast, { props: { toasts, onDismiss } });
    vi.advanceTimersByTime(3200);
    expect(onDismiss).toHaveBeenCalledWith("1");
  });

  it("pushToast appends and caps the stack", () => {
    let stack: ToastItem[] = [];
    for (let i = 0; i < 7; i++) {
      stack = pushToast(stack, `t${i}`, "info");
    }
    expect(stack).toHaveLength(5);
    expect(stack[0]?.text).toBe("t2");
    expect(stack[4]?.text).toBe("t6");
  });
});

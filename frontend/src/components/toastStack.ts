export type ToastVariant = "success" | "error" | "warning" | "info";

export type ToastItem = {
  id: string;
  text: string;
  variant: ToastVariant;
};

export type ShowToast = (text: string, variant?: ToastVariant) => void;

export const TOAST_MS = 3200;
export const MAX_TOASTS = 5;

function randomId(): string {
  const words = new Uint32Array(4);
  crypto.getRandomValues(words);
  return Array.from(words, (word) => word.toString(16).padStart(8, "0")).join("");
}

/** Append a toast to the stack (newest at the bottom). */
export function pushToast(
  toasts: ToastItem[],
  text: string,
  variant: ToastVariant = "success",
): ToastItem[] {
  const next: ToastItem = {
    id: randomId(),
    text,
    variant,
  };
  return [...toasts, next].slice(-MAX_TOASTS);
}

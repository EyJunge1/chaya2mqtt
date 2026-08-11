/** Shared Tailwind class fragments used across components. */
export const FOCUS_RING = "focus-ring";

/** Selected / active accent surface (sidebar, InfoTip hover). */
export const ACTIVE_ACCENT = "bg-accent/15 text-accent";

/** Default hover for buttons, nav items, icon buttons. */
export const HOVER_SURFACE = "hover:bg-surface-hover hover:text-text-bright";

/** Hover for bordered interactive rows/cards. */
export const HOVER_ROW = "hover:border-accent/40 hover:bg-surface-hover";

export const ICON_WELL = `inline-flex items-center justify-center rounded-lg ${ACTIVE_ACCENT}`;

export const SURFACE_CARD = "rounded-xl border border-border bg-surface";

export const MUTED_TEXT = "text-sm text-muted";

export const EMPTY_STATE = "text-sm text-muted";

/** Empty/missing status values render as a simple dash. */
export function dash(value: string | number | null | undefined): string {
  if (value == null) return "-";
  const text = String(value).trim();
  return text || "-";
}

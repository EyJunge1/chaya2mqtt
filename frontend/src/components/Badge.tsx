import type { HTMLAttributes, ReactNode } from "react";
import { cn } from "../ui/cn";
import { ACTIVE_ACCENT } from "../ui/styles";

export type BadgeTone = "neutral" | "muted" | "ok" | "danger" | "warning" | "accent";

const toneClass: Record<BadgeTone, string> = {
  neutral: "border-border bg-surface text-text-bright",
  muted: "border-border bg-bg text-muted",
  ok: "border-status-ok/35 bg-status-ok/15 text-status-ok",
  danger: "border-danger/35 bg-danger/15 text-danger",
  warning: "border-warning/35 bg-warning/15 text-warning",
  accent: cn("border-accent/35", ACTIVE_ACCENT),
};

const dotClass: Record<BadgeTone, string> = {
  neutral: "bg-text-bright",
  muted: "bg-border",
  ok: "bg-status-ok",
  danger: "bg-danger",
  warning: "bg-warning",
  accent: "bg-accent",
};

export function Badge({
  children,
  tone = "neutral",
  dot,
  as: Comp = "span",
  className,
  ...props
}: {
  children: ReactNode;
  tone?: BadgeTone;
  /** When true, show a status dot. When a string, use that color class. */
  dot?: boolean | string;
  as?: "span" | "button";
  className?: string;
} & HTMLAttributes<HTMLElement>) {
  return (
    <Comp
      className={cn(
        "inline-flex select-none items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-semibold transition",
        toneClass[tone],
        className,
      )}
      {...props}
    >
      {dot ? (
        <span
          className={cn("size-2 rounded-full", typeof dot === "string" ? dot : dotClass[tone])}
          aria-hidden
        />
      ) : null}
      {children}
    </Comp>
  );
}

import { CircleAlert, Info, OctagonX } from "lucide-react";
import type { ReactNode } from "react";
import { cn } from "../ui/cn";

export type AlertVariant = "info" | "warning" | "error";

const styles: Record<AlertVariant, { wrap: string; icon: string; Icon: typeof Info }> = {
  info: {
    wrap: "border-border bg-surface",
    icon: "text-muted",
    Icon: Info,
  },
  warning: {
    wrap: "border-warning/35 bg-warning/10",
    icon: "text-warning",
    Icon: CircleAlert,
  },
  error: {
    wrap: "border-danger/35 bg-danger/10",
    icon: "text-danger",
    Icon: OctagonX,
  },
};

export function Alert({
  variant = "info",
  title,
  children,
  className,
}: {
  variant?: AlertVariant;
  title?: string;
  children: ReactNode;
  className?: string;
}) {
  const style = styles[variant];
  const Icon = style.Icon;
  return (
    <div
      role={variant === "error" ? "alert" : "status"}
      className={cn("flex gap-3 rounded-xl border px-3.5 py-3", style.wrap, className)}
    >
      <Icon size={18} className={cn("mt-0.5 shrink-0", style.icon)} aria-hidden />
      <div className="min-w-0 space-y-1">
        {title ? <p className="text-sm font-semibold text-text-bright">{title}</p> : null}
        <div className="text-sm text-muted">{children}</div>
      </div>
    </div>
  );
}

import type { LucideIcon } from "lucide-react";
import type { ReactNode } from "react";
import { Link } from "react-router-dom";
import { cn } from "../ui/cn";
import { HOVER_ROW, ICON_WELL, SURFACE_CARD } from "../ui/styles";
import { InfoTip } from "./InfoTip";

export function NavCard({
  to,
  title,
  subtitle,
  icon: Icon,
}: {
  to: string;
  title: string;
  subtitle?: string;
  icon: LucideIcon;
}) {
  return (
    <Link
      to={to}
      className={cn(
        "flex items-center gap-3 px-4 py-4 text-left focus-ring",
        HOVER_ROW,
        SURFACE_CARD,
      )}
    >
      <span className={cn(ICON_WELL, "h-10 w-10")}>
        <Icon size={20} aria-hidden />
      </span>
      <span className="min-w-0">
        <span className="block font-semibold text-text-bright">{title}</span>
        {subtitle ? <span className="block truncate text-sm text-muted">{subtitle}</span> : null}
      </span>
    </Link>
  );
}

export function Panel({
  title,
  hint,
  children,
  action,
}: {
  title?: ReactNode;
  hint?: string;
  children: ReactNode;
  action?: ReactNode;
}) {
  return (
    <section className={cn(SURFACE_CARD, "p-4")}>
      {(title || action || hint) && (
        <div className="mb-3 flex items-center justify-between gap-3">
          {title || hint ? (
            <h2 className="inline-flex items-center gap-1.5 text-sm font-semibold text-text-bright">
              {title}
              {hint ? <InfoTip text={hint} /> : null}
            </h2>
          ) : (
            <span />
          )}
          {action}
        </div>
      )}
      {children}
    </section>
  );
}

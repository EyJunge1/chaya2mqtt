import type { LucideIcon } from "lucide-react";
import type { ReactNode } from "react";
import { Link } from "react-router-dom";
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
      className="flex items-center gap-3 rounded-xl border border-border bg-surface px-4 py-4 text-left transition hover:border-accent/40 hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
    >
      <span className="flex h-10 w-10 items-center justify-center rounded-lg bg-accent/15 text-accent">
        <Icon size={20} />
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
  title?: string;
  hint?: string;
  children: ReactNode;
  action?: ReactNode;
}) {
  return (
    <section className="rounded-xl border border-border bg-surface p-4">
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

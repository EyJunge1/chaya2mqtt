import { Link } from "react-router-dom";
import { useI18n } from "../i18n/useI18n";
import { cn } from "../ui/cn";
import { HOVER_ROW } from "../ui/styles";
import { Badge } from "./Badge";

export function StatusBadge({
  ok,
  label,
  detailOk,
  detailBad,
  to,
}: {
  ok: boolean;
  label: string;
  detailOk?: string;
  detailBad?: string;
  /** When set, the badge navigates to this page. */
  to?: string;
}) {
  const { t } = useI18n();
  const detail = ok ? (detailOk ?? t("status.connected")) : (detailBad ?? t("status.disconnected"));
  const ariaLabel = `${label}: ${detail}`;

  if (to) {
    return (
      <Link
        to={to}
        aria-label={ariaLabel}
        title={detail}
        className={cn(
          "inline-flex select-none items-center gap-1.5 rounded-full border border-border bg-surface px-2.5 py-1 text-xs font-semibold text-text-bright transition focus-ring",
          "cursor-pointer",
          HOVER_ROW,
        )}
      >
        <span
          className={cn("size-2 rounded-full", ok ? "bg-status-ok" : "bg-danger")}
          aria-hidden
        />
        {label}
      </Link>
    );
  }

  return (
    <Badge
      tone="neutral"
      dot={ok ? "bg-status-ok" : "bg-danger"}
      aria-label={ariaLabel}
      title={detail}
    >
      {label}
    </Badge>
  );
}

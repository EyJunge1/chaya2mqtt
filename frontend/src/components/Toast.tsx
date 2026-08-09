import { useEffect, useState } from "react";
import { CircleAlert, CircleCheck, Info, OctagonX, X } from "lucide-react";
import { useI18n } from "../i18n/useI18n";
import { TOAST_MS, type ToastItem, type ToastVariant } from "./toastStack";

export type { ShowToast, ToastItem, ToastVariant } from "./toastStack";

const STACK_PEEK = 12;
const STACK_SCALE = 0.05;
const FRONT_HEIGHT_REM = 3.75;

const styles: Record<
  ToastVariant,
  { iconBg: string; iconColor: string; bar: string; Icon: typeof CircleCheck }
> = {
  success: {
    iconBg: "bg-accent/15",
    iconColor: "text-accent",
    bar: "bg-accent/80",
    Icon: CircleCheck,
  },
  error: {
    iconBg: "bg-danger/15",
    iconColor: "text-danger",
    bar: "bg-danger/80",
    Icon: OctagonX,
  },
  warning: {
    iconBg: "bg-warning/15",
    iconColor: "text-warning",
    bar: "bg-warning/80",
    Icon: CircleAlert,
  },
  info: {
    iconBg: "bg-muted/20",
    iconColor: "text-muted",
    bar: "bg-muted/80",
    Icon: Info,
  },
};

function ToastCard({
  item,
  onDismiss,
  dimmed,
}: {
  item: ToastItem;
  onDismiss: (id: string) => void;
  dimmed?: boolean;
}) {
  const { t } = useI18n();

  useEffect(() => {
    const timer = window.setTimeout(() => onDismiss(item.id), TOAST_MS);
    return () => window.clearTimeout(timer);
  }, [item.id, onDismiss]);

  const style = styles[item.variant];
  const Icon = style.Icon;
  const assertive = item.variant === "error" || item.variant === "warning";

  return (
    <div
      role={assertive ? "alert" : "status"}
      aria-live={assertive ? "assertive" : "polite"}
      className="overflow-hidden rounded-xl border border-border bg-surface/95 shadow-[0_12px_40px_rgba(0,0,0,0.45)] backdrop-blur-sm"
    >
      <div
        className={`flex items-center gap-3 px-3.5 py-3 transition-opacity duration-200 ${dimmed ? "opacity-0" : "opacity-100"}`}
      >
        <span
          className={`flex size-8 shrink-0 items-center justify-center rounded-lg ${style.iconBg} ${style.iconColor}`}
        >
          <Icon size={17} strokeWidth={2.25} />
        </span>
        <p className="min-w-0 flex-1 text-sm leading-snug font-medium text-text-bright">
          {item.text}
        </p>
        <button
          type="button"
          aria-label={t("common.close")}
          onClick={() => onDismiss(item.id)}
          tabIndex={dimmed ? -1 : 0}
          className="flex size-7 shrink-0 items-center justify-center rounded-md text-muted transition hover:bg-surface-hover hover:text-text-bright focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
        >
          <X size={15} />
        </button>
      </div>
      <div
        className={`h-0.5 origin-left animate-[toast-progress_3200ms_linear_forwards] ${style.bar} ${dimmed ? "opacity-0" : "opacity-100"}`}
      />
    </div>
  );
}

export function ToastStack({
  toasts,
  onDismiss,
}: {
  toasts: ToastItem[];
  onDismiss: (id: string) => void;
}) {
  const [expanded, setExpanded] = useState(false);

  if (toasts.length === 0) return null;

  // Newest first → front of the pile / bottom when expanded.
  const ordered = [...toasts].reverse();
  const visibleBehind = Math.min(Math.max(ordered.length - 1, 0), 2);

  return (
    <div
      className="pointer-events-none fixed right-4 bottom-[max(1rem,env(safe-area-inset-bottom))] left-4 z-50 sm:left-auto sm:w-88"
      onMouseEnter={() => setExpanded(true)}
      onMouseLeave={() => setExpanded(false)}
      aria-relevant="additions"
    >
      <div
        className={`pointer-events-auto relative w-full transition-[height] duration-300 ease-out ${
          expanded ? "flex flex-col-reverse gap-2" : ""
        }`}
        style={
          expanded
            ? undefined
            : { height: `calc(${FRONT_HEIGHT_REM}rem + ${visibleBehind * STACK_PEEK}px)` }
        }
      >
        {ordered.map((item, index) => {
          const front = index === 0;
          const behind = Math.min(index, 2);
          const hiddenBehind = !expanded && index > 2;

          if (expanded) {
            return (
              <div
                key={item.id}
                className="w-full transition-[transform,opacity] duration-300 ease-out"
              >
                <ToastCard item={item} onDismiss={onDismiss} />
              </div>
            );
          }

          return (
            <div
              key={item.id}
              aria-hidden={!front}
              className={`absolute right-0 bottom-0 left-0 origin-bottom transition-[transform,opacity] duration-300 ease-out ${
                front ? "animate-[toast-in_180ms_ease-out]" : ""
              } ${hiddenBehind ? "pointer-events-none opacity-0" : ""}`}
              style={{
                zIndex: ordered.length - index,
                transform: `translateY(-${behind * STACK_PEEK}px) scale(${1 - behind * STACK_SCALE})`,
              }}
            >
              <ToastCard item={item} onDismiss={onDismiss} dimmed={!front} />
            </div>
          );
        })}
      </div>
    </div>
  );
}

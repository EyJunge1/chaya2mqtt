import { Info } from "lucide-react";
import { useEffect, useId, useRef, useState, useSyncExternalStore } from "react";
import { useI18n } from "../i18n/useI18n";
import { cn } from "../ui/cn";
import { ACTIVE_ACCENT } from "../ui/styles";

const HOVER_QUERY = "(hover: hover) and (pointer: fine)";

function subscribeHoverCapability(onStoreChange: () => void) {
  if (typeof window.matchMedia !== "function") {
    return () => {};
  }
  const mql = window.matchMedia(HOVER_QUERY);
  mql.addEventListener("change", onStoreChange);
  return () => mql.removeEventListener("change", onStoreChange);
}

function getHoverCapability() {
  if (typeof window.matchMedia !== "function") {
    return true;
  }
  return window.matchMedia(HOVER_QUERY).matches;
}

function useCanHover() {
  return useSyncExternalStore(subscribeHoverCapability, getHoverCapability, () => true);
}

export function InfoTip({ text, className = "" }: { text: string; className?: string }) {
  const { t } = useI18n();
  const tipId = useId();
  const canHover = useCanHover();
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    if (canHover) {
      setOpen(false);
    }
  }, [canHover]);

  useEffect(() => {
    if (canHover || !open) {
      return;
    }
    const onPointerDown = (event: PointerEvent) => {
      if (rootRef.current && !rootRef.current.contains(event.target as Node)) {
        setOpen(false);
      }
    };
    document.addEventListener("pointerdown", onPointerDown);
    return () => document.removeEventListener("pointerdown", onPointerDown);
  }, [canHover, open]);

  return (
    <span ref={rootRef} className={cn("relative inline-flex w-fit shrink-0", className)}>
      <button
        type="button"
        tabIndex={0}
        aria-label={t("common.info")}
        aria-describedby={tipId}
        aria-expanded={canHover ? undefined : open}
        className={cn(
          "inline-flex size-5 items-center justify-center rounded-full text-muted transition focus-ring",
          canHover ? "cursor-not-allowed" : "cursor-pointer",
          open && ACTIVE_ACCENT,
        )}
        onMouseEnter={() => {
          if (canHover) {
            setOpen(true);
          }
        }}
        onMouseLeave={() => {
          if (canHover) {
            setOpen(false);
          }
        }}
        onFocus={(event) => {
          if (canHover && event.currentTarget.matches(":focus-visible")) {
            setOpen(true);
          }
        }}
        onBlur={() => {
          if (canHover) {
            setOpen(false);
          }
        }}
        onClick={(e) => {
          e.preventDefault();
          e.stopPropagation();
          if (!canHover) {
            setOpen((value) => !value);
          }
        }}
      >
        <Info size={14} strokeWidth={2.25} className="pointer-events-none" aria-hidden />
      </button>
      <span
        id={tipId}
        role="tooltip"
        className={cn(
          "pointer-events-none absolute bottom-full left-1/2 z-20 mb-2 w-max max-w-[16rem] -translate-x-1/2 rounded-lg border border-border bg-surface px-2.5 py-1.5 text-left text-xs font-normal text-text-bright shadow-elevated transition",
          open ? "visible opacity-100" : "invisible opacity-0",
        )}
      >
        {text}
      </span>
    </span>
  );
}

<script lang="ts">
  import { CircleAlert, CircleCheck, Info, OctagonX, X } from "@lucide/svelte";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { HOVER_SURFACE } from "../ui/styles.ts";
  import { TOAST_MS, type ToastItem, type ToastVariant } from "./toastStack.ts";

  const styles: Record<ToastVariant, { iconBg: string; iconColor: string; bar: string }> = {
    success: { iconBg: "bg-accent/15", iconColor: "text-accent", bar: "bg-accent/80" },
    error: { iconBg: "bg-danger/15", iconColor: "text-danger", bar: "bg-danger/80" },
    warning: { iconBg: "bg-warning/15", iconColor: "text-warning", bar: "bg-warning/80" },
    info: { iconBg: "bg-muted/20", iconColor: "text-muted", bar: "bg-muted/80" },
  };

  let {
    item,
    onDismiss,
    dimmed = false,
  }: {
    item: ToastItem;
    onDismiss: (id: string) => void;
    dimmed?: boolean;
  } = $props();

  const style = $derived(styles[item.variant]);
  const assertive = $derived(item.variant === "error" || item.variant === "warning");

  $effect(() => {
    const timer = window.setTimeout(() => onDismiss(item.id), TOAST_MS);
    return () => window.clearTimeout(timer);
  });
</script>

<div
  role={assertive ? "alert" : "status"}
  aria-live={assertive ? "assertive" : "polite"}
  class="overflow-hidden rounded-xl border border-border bg-surface/95 shadow-elevated backdrop-blur-sm"
>
  <div
    class={`flex items-center gap-3 px-3.5 py-3 transition-opacity duration-200 ${dimmed ? "opacity-0" : "opacity-100"}`}
  >
    <span
      class={`flex size-8 shrink-0 items-center justify-center rounded-lg ${style.iconBg} ${style.iconColor}`}
    >
      {#if item.variant === "success"}
        <CircleCheck size={17} strokeWidth={2.25} />
      {:else if item.variant === "error"}
        <OctagonX size={17} strokeWidth={2.25} />
      {:else if item.variant === "warning"}
        <CircleAlert size={17} strokeWidth={2.25} />
      {:else}
        <Info size={17} strokeWidth={2.25} />
      {/if}
    </span>
    <p class="min-w-0 flex-1 text-sm leading-snug font-medium text-text-bright">
      {item.text}
    </p>
    <button
      type="button"
      aria-label={i18n.t("common.close")}
      onclick={() => onDismiss(item.id)}
      tabindex={dimmed ? -1 : 0}
      class={cn(
        "flex size-7 shrink-0 items-center justify-center rounded-md text-muted transition focus-ring",
        HOVER_SURFACE,
      )}
    >
      <X size={15} />
    </button>
  </div>
  <div
    class={`h-0.5 origin-left animate-[toast-progress_3200ms_linear_forwards] ${style.bar} ${dimmed ? "opacity-0" : "opacity-100"}`}
  ></div>
</div>

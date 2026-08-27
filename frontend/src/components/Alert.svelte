<script lang="ts">
  import { CircleAlert, Info, OctagonX } from "@lucide/svelte";
  import { cn } from "../ui/cn.ts";

  export type AlertVariant = "info" | "warning" | "error";

  const styles: Record<AlertVariant, { wrap: string; icon: string }> = {
    info: { wrap: "border-border bg-surface", icon: "text-muted" },
    warning: { wrap: "border-warning/35 bg-warning/10", icon: "text-warning" },
    error: { wrap: "border-danger/35 bg-danger/10", icon: "text-danger" },
  };

  let {
    variant = "info",
    title,
    class: className = "",
    children,
  }: {
    variant?: AlertVariant;
    title?: string;
    class?: string;
    children: import("svelte").Snippet;
  } = $props();

  const style = $derived(styles[variant]);
</script>

<div
  role={variant === "error" ? "alert" : "status"}
  class={cn("flex gap-3 rounded-xl border px-3.5 py-3", style.wrap, className)}
>
  {#if variant === "warning"}
    <CircleAlert size={18} class={cn("mt-0.5 shrink-0", style.icon)} aria-hidden="true" />
  {:else if variant === "error"}
    <OctagonX size={18} class={cn("mt-0.5 shrink-0", style.icon)} aria-hidden="true" />
  {:else}
    <Info size={18} class={cn("mt-0.5 shrink-0", style.icon)} aria-hidden="true" />
  {/if}
  <div class="min-w-0 space-y-1">
    {#if title}
      <p class="text-sm font-semibold text-text-bright">{title}</p>
    {/if}
    <div class="text-sm text-muted">
      {@render children()}
    </div>
  </div>
</div>

<script lang="ts">
  import type { HTMLButtonAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";
  import { ACTIVE_ACCENT } from "../ui/styles.ts";

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

  let {
    children,
    tone = "neutral",
    dot = false,
    as = "span",
    class: className = "",
    ...rest
  }: {
    children: import("svelte").Snippet;
    tone?: BadgeTone;
    dot?: boolean | string;
    as?: "span" | "button";
    class?: string;
  } & HTMLButtonAttributes = $props();

  const classes = $derived(
    cn(
      "inline-flex select-none items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-semibold transition",
      toneClass[tone],
      className,
    ),
  );
</script>

<svelte:element this={as} class={classes} {...rest}>
  {#if dot}
    <span
      class={cn("size-2 rounded-full", typeof dot === "string" ? dot : dotClass[tone])}
      aria-hidden="true"
    ></span>
  {/if}
  {@render children()}
</svelte:element>

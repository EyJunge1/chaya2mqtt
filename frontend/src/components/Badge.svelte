<script lang="ts">
  import type { HTMLAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";

  export type BadgeTone = "neutral" | "muted";

  const toneClass: Record<BadgeTone, string> = {
    neutral: "border-border bg-surface text-text-bright",
    muted: "border-border bg-bg text-muted",
  };

  const dotClass: Record<BadgeTone, string> = {
    neutral: "bg-text-bright",
    muted: "bg-border",
  };

  let {
    children,
    tone = "neutral",
    dot = false,
    class: className = "",
    ...rest
  }: {
    children: import("svelte").Snippet;
    tone?: BadgeTone;
    dot?: boolean | string;
    class?: string;
  } & HTMLAttributes<HTMLSpanElement> = $props();

  const classes = $derived(
    cn(
      "inline-flex select-none items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-semibold transition",
      toneClass[tone],
      className,
    ),
  );
</script>

<span class={classes} {...rest}>
  {#if dot}
    <span
      class={cn("size-2 rounded-full", typeof dot === "string" ? dot : dotClass[tone])}
      aria-hidden="true"
    ></span>
  {/if}
  {@render children()}
</span>

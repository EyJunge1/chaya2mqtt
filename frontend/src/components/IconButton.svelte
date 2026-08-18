<script lang="ts">
  import type { HTMLButtonAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";
  import { HOVER_SURFACE } from "../ui/styles.ts";

  const sizeClass = {
    sm: "size-8",
    md: "size-9",
  } as const;

  const variantClass = {
    ghost: cn("text-muted", HOVER_SURFACE),
    bordered: cn("border border-border bg-surface text-muted", HOVER_SURFACE),
  } as const;

  let {
    children,
    variant = "ghost",
    size = "md",
    class: className = "",
    type = "button",
    ...rest
  }: {
    children: import("svelte").Snippet;
    variant?: "ghost" | "bordered";
    size?: "sm" | "md";
    class?: string;
  } & HTMLButtonAttributes = $props();
</script>

<button
  {type}
  class={cn(
    "inline-flex shrink-0 items-center justify-center rounded-lg transition focus-ring",
    sizeClass[size],
    variantClass[variant],
    className,
  )}
  {...rest}
>
  {@render children()}
</button>

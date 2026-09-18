<script lang="ts">
  import type { HTMLButtonAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";
  import { HOVER_SURFACE } from "../ui/styles.ts";

  const sizeClass = {
    sm: "size-8",
    md: "size-9",
  } as const;

  let {
    children,
    size = "md",
    class: className = "",
    type = "button",
    ...rest
  }: {
    children: import("svelte").Snippet;
    size?: "sm" | "md";
    class?: string;
  } & HTMLButtonAttributes = $props();
</script>

<button
  {type}
  class={cn(
    "inline-flex shrink-0 items-center justify-center rounded-lg text-muted transition focus-ring",
    HOVER_SURFACE,
    sizeClass[size],
    className,
  )}
  {...rest}
>
  {@render children()}
</button>

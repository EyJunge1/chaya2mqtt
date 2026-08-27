<script lang="ts">
  import { cn } from "../ui/cn.ts";
  import Link from "./Link.svelte";

  let {
    href,
    variant = "secondary",
    class: className = "",
    children,
  }: {
    href: string;
    variant?: "primary" | "secondary" | "warning";
    class?: string;
    children: import("svelte").Snippet;
  } = $props();

  const variantClass = $derived(
    variant === "primary"
      ? "bg-accent text-bg enabled:hover:opacity-90"
      : variant === "warning"
        ? "border border-warning/35 bg-surface text-text-bright hover:bg-surface-hover"
        : "border border-border bg-surface text-text-bright hover:bg-surface-hover",
  );
</script>

<Link
  {href}
  class={cn(
    "inline-flex items-center justify-center rounded-lg px-3 py-1.5 text-sm font-semibold transition focus-ring",
    variantClass,
    className,
  )}
>
  {@render children()}
</Link>

<script lang="ts">
  import { cn } from "../ui/cn.ts";
  import Link from "./Link.svelte";

  let {
    href,
    class: className = "",
    children,
  }: {
    href: string;
    class?: string;
    children: import("svelte").Snippet;
  } = $props();
</script>

<Link
  {href}
  class={cn(
    "inline-flex items-center justify-center rounded-lg border border-warning/35 bg-surface px-3 py-1.5 text-sm font-semibold text-text-bright transition hover:bg-surface-hover focus-ring",
    className,
  )}
>
  {@render children()}
</Link>

<script lang="ts">
  import type { HTMLButtonAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";
  import Spinner from "./Spinner.svelte";

  let {
    children,
    loading = false,
    disabled = false,
    class: className = "",
    ...rest
  }: {
    children: import("svelte").Snippet;
    loading?: boolean;
    class?: string;
  } & HTMLButtonAttributes = $props();
</script>

<button
  disabled={disabled || loading}
  aria-busy={loading || undefined}
  class={cn(
    "inline-flex items-center justify-center gap-1 rounded-lg px-2 py-1.5 text-sm font-semibold text-accent transition enabled:hover:bg-surface-hover disabled:opacity-50 focus-ring",
    className,
  )}
  {...rest}
>
  <span class="inline-flex items-center justify-center gap-2">
    {#if loading}
      <Spinner size={18} class="text-current" />
    {/if}
    {@render children()}
  </span>
</button>

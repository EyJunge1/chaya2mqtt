<script lang="ts">
  import type { HTMLButtonAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";
  import { widthClass, type ButtonWidth } from "./buttonStyles.ts";
  import Spinner from "./Spinner.svelte";

  let {
    children,
    loading = false,
    disabled = false,
    class: className = "",
    width = "full",
    ...rest
  }: {
    children: import("svelte").Snippet;
    loading?: boolean;
    width?: ButtonWidth;
    class?: string;
  } & HTMLButtonAttributes = $props();
</script>

<button
  disabled={disabled || loading}
  aria-busy={loading || undefined}
  class={cn(
    "rounded-xl bg-accent px-4 py-3.5 text-base font-semibold text-bg transition enabled:hover:opacity-90 disabled:opacity-50 focus-ring",
    widthClass(width),
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

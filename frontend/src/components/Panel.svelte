<script lang="ts">
  import type { Snippet } from "svelte";
  import { cn } from "../ui/cn.ts";
  import { SURFACE_CARD } from "../ui/styles.ts";
  import InfoTip from "./InfoTip.svelte";

  let {
    title,
    hint,
    action,
    children,
  }: {
    title?: string | Snippet;
    hint?: string;
    action?: Snippet;
    children: Snippet;
  } = $props();
</script>

<section class={cn(SURFACE_CARD, "p-4")}>
  {#if title || action || hint}
    <div class="mb-3 flex items-center justify-between gap-3">
      {#if title || hint}
        <h2 class="inline-flex items-center gap-1.5 text-sm font-semibold text-text-bright">
          {#if typeof title === "function"}
            {@render title()}
          {:else if title}
            {title}
          {/if}
          {#if hint}
            <InfoTip text={hint} />
          {/if}
        </h2>
      {:else}
        <span></span>
      {/if}
      {#if action}
        {@render action()}
      {/if}
    </div>
  {/if}
  {@render children()}
</section>

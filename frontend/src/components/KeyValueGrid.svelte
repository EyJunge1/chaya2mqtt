<script lang="ts">
  import type { Snippet } from "svelte";
  import { cn } from "../ui/cn.ts";

  export type KeyValueItem = {
    label: string;
    value: string | Snippet;
    className?: string;
    valueClass?: string;
    span?: 1 | 2;
    testId?: string;
  };

  let { items, class: className = "" }: { items: KeyValueItem[]; class?: string } = $props();
</script>

<dl class={cn("grid gap-3 text-sm sm:grid-cols-2", className)}>
  {#each items as item (item.label)}
    <div class={cn(item.span === 2 ? "sm:col-span-2" : undefined, item.className)} data-testid={item.testId}>
      <dt class="text-muted">{item.label}</dt>
      <dd class={cn("font-semibold text-text-bright", item.valueClass)}>
        {#if typeof item.value === "function"}
          {@render item.value()}
        {:else}
          {item.value}
        {/if}
      </dd>
    </div>
  {/each}
</dl>

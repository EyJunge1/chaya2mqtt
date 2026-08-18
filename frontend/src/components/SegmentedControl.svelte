<script lang="ts" generics="T extends string">
  import { cn } from "../ui/cn.ts";
  import { HOVER_ROW } from "../ui/styles.ts";

  export type SegmentOption<V extends string> = {
    value: V;
    label: string;
    testId?: string;
  };

  let {
    value,
    onChange,
    options,
    label,
  }: {
    value: T;
    onChange: (value: T) => void;
    options: SegmentOption<T>[];
    label: string;
  } = $props();
</script>

<div
  role="radiogroup"
  aria-label={label}
  class="grid grid-cols-2 gap-1 rounded-xl border border-border bg-bg p-1"
>
  {#each options as option (option.value)}
    {@const active = value === option.value}
    <button
      type="button"
      role="radio"
      aria-checked={active}
      data-testid={option.testId}
      onclick={() => onChange(option.value)}
      class={cn(
        "rounded-lg border border-transparent px-3 py-2 text-sm font-semibold transition focus-ring",
        HOVER_ROW,
        active ? "bg-surface text-text-bright shadow-sm" : "text-muted hover:text-text-bright",
      )}
    >
      {option.label}
    </button>
  {/each}
</div>

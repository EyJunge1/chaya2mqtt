<script lang="ts" module>
  export type SegmentOption<V extends string> = {
    value: V;
    label: string;
    testId?: string;
  };
</script>

<script lang="ts" generics="T extends string">
  import { cn } from "../ui/cn.ts";
  import { HOVER_ROW } from "../ui/styles.ts";

  let {
    value,
    onChange,
    options,
    label,
    class: className = "",
    compact = false,
  }: {
    value: T;
    onChange: (value: T) => void;
    options: SegmentOption<T>[];
    label: string;
    class?: string;
    /** Narrow control for placing beside another input. */
    compact?: boolean;
  } = $props();
</script>

<div
  role="radiogroup"
  aria-label={label}
  class={cn(
    "grid grid-cols-2 gap-1 rounded-xl border border-border bg-bg p-1",
    compact && "shrink-0 rounded-lg",
    className,
  )}
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
        "rounded-lg border border-transparent font-semibold transition focus-ring",
        compact ? "px-2.5 py-2 text-xs" : "px-3 py-2 text-sm",
        HOVER_ROW,
        active ? "bg-surface text-text-bright shadow-sm" : "text-muted hover:text-text-bright",
      )}
    >
      {option.label}
    </button>
  {/each}
</div>

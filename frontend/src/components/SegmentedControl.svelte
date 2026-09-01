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

  let groupEl: HTMLDivElement | undefined;

  function radioAt(index: number): HTMLButtonElement | undefined {
    return groupEl?.querySelectorAll<HTMLButtonElement>(':scope > [role="radio"]')[index];
  }

  /** APG radiogroup: navigation keys check the target and call element.focus(). */
  function selectIndex(index: number) {
    const option = options[index];
    if (!option) return;
    onChange(option.value);
    radioAt(index)?.focus();
  }

  function move(delta: number) {
    if (options.length === 0) return;
    const idx = Math.max(
      0,
      options.findIndex((o) => o.value === value),
    );
    selectIndex((idx + delta + options.length) % options.length);
  }

  function onKeydown(e: KeyboardEvent) {
    switch (e.key) {
      case "ArrowRight":
      case "ArrowDown":
        e.preventDefault();
        move(1);
        break;
      case "ArrowLeft":
      case "ArrowUp":
        e.preventDefault();
        move(-1);
        break;
      case "Home":
        e.preventDefault();
        selectIndex(0);
        break;
      case "End":
        e.preventDefault();
        selectIndex(options.length - 1);
        break;
      default:
        break;
    }
  }
</script>

<div
  bind:this={groupEl}
  role="radiogroup"
  aria-label={label}
  tabindex="-1"
  class={cn(
    "grid grid-cols-2 gap-1 rounded-xl border border-border bg-bg p-1",
    compact && "shrink-0 rounded-lg",
    className,
  )}
  onkeydown={onKeydown}
>
  {#each options as option (option.value)}
    {@const active = value === option.value}
    <button
      type="button"
      role="radio"
      aria-checked={active}
      tabindex={active ? 0 : -1}
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

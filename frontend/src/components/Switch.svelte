<script lang="ts">
  import { cn } from "../ui/cn.ts";

  let {
    checked,
    onChange,
    disabled = false,
    label,
  }: {
    checked: boolean;
    onChange: (checked: boolean) => void;
    disabled?: boolean;
    label: string;
  } = $props();
</script>

<button
  type="button"
  role="switch"
  aria-checked={checked}
  aria-label={label}
  {disabled}
  onclick={() => onChange(!checked)}
  class={cn(
    "relative inline-flex h-7 w-12 shrink-0 items-center rounded-full border transition enabled:hover:opacity-90 disabled:opacity-50 focus-ring",
    checked ? "border-accent bg-accent" : "border-border bg-surface",
  )}
>
  <span
    aria-hidden="true"
    class={cn(
      "inline-block h-5 w-5 transform rounded-full bg-bg shadow transition",
      checked ? "translate-x-6" : "translate-x-1",
    )}
  ></span>
</button>

<script lang="ts">
  let {
    value,
    label,
  }: {
    value: number | null;
    label?: string;
  } = $props();

  const pct = $derived(value == null ? null : Math.min(100, Math.max(0, value)));
</script>

<div
  role="progressbar"
  aria-valuemin={0}
  aria-valuemax={100}
  aria-valuenow={pct ?? undefined}
  aria-label={label}
  class="h-2 overflow-hidden rounded-full bg-border"
>
  <div class="h-full rounded-full bg-accent transition-all" style="width: {pct ?? 10}%"></div>
</div>

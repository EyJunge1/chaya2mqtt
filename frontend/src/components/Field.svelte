<script lang="ts">
  import InfoTip from "./InfoTip.svelte";

  let {
    label,
    hint,
    children,
  }: {
    label: string;
    hint?: string;
    children: import("svelte").Snippet;
  } = $props();
</script>

<label class="block">
  <span class="mb-1.5 flex items-center gap-1.5 text-sm font-semibold text-text-bright">
    {label}
    {#if hint}
      <InfoTip text={hint} />
    {/if}
  </span>
  {@render children()}
</label>

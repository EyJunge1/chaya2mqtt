<script lang="ts">
  import InfoTip from "./InfoTip.svelte";

  let {
    label,
    hint,
    required = false,
    children,
  }: {
    label: string;
    hint?: string;
    /** Visual required marker (pair with HTML `required` on the control). */
    required?: boolean;
    children: import("svelte").Snippet;
  } = $props();
</script>

<label class="block">
  <span class="mb-1.5 flex items-center gap-1.5 text-sm font-semibold text-text-bright">
    {label}
    {#if required}
      <span class="text-danger" aria-hidden="true">*</span>
    {/if}
    {#if hint}
      <InfoTip text={hint} />
    {/if}
  </span>
  {@render children()}
</label>

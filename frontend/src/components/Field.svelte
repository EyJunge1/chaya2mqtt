<script lang="ts">
  import { i18n } from "../i18n/i18n.svelte.ts";
  import InfoTip from "./InfoTip.svelte";

  let {
    label,
    hint,
    required = false,
    optional = false,
    children,
  }: {
    label: string;
    hint?: string;
    /** Visual required marker (pair with HTML `required` on the control). */
    required?: boolean;
    /** Visual optional marker next to the label. */
    optional?: boolean;
    children: import("svelte").Snippet;
  } = $props();
</script>

<label class="block">
  <span class="mb-1.5 flex items-center gap-1.5 text-sm font-semibold text-text-bright">
    {label}
    {#if required}
      <span class="text-danger" aria-hidden="true">*</span>
    {/if}
    {#if optional}
      <span class="text-xs font-normal text-muted">({i18n.t("common.optional")})</span>
    {/if}
    {#if hint}
      <InfoTip text={hint} />
    {/if}
  </span>
  {@render children()}
</label>

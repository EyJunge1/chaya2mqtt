<script lang="ts">
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { HOVER_ROW } from "../ui/styles.ts";
  import Badge from "./Badge.svelte";
  import Link from "./Link.svelte";

  let {
    ok,
    label,
    detailOk,
    detailBad,
    href,
  }: {
    ok: boolean;
    label: string;
    detailOk?: string;
    detailBad?: string;
    href?: string;
  } = $props();

  const detail = $derived(
    ok ? (detailOk ?? i18n.t("status.connected")) : (detailBad ?? i18n.t("status.disconnected")),
  );
  const ariaLabel = $derived(`${label}: ${detail}`);
</script>

{#if href}
  <Link
    {href}
    aria-label={ariaLabel}
    title={detail}
    class={cn(
      "inline-flex select-none items-center gap-1.5 rounded-full border border-border bg-surface px-2.5 py-1 text-xs font-semibold text-text-bright transition focus-ring",
      "cursor-pointer",
      HOVER_ROW,
    )}
  >
    <span class={cn("size-2 rounded-full", ok ? "bg-status-ok" : "bg-danger")} aria-hidden="true"
    ></span>
    {label}
  </Link>
{:else}
  <Badge
    tone="neutral"
    dot={ok ? "bg-status-ok" : "bg-danger"}
    aria-label={ariaLabel}
    title={detail}
  >
    {label}
  </Badge>
{/if}

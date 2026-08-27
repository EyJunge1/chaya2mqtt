<script lang="ts">
  import type { IconComponent } from "../nav/settingsNav.ts";
  import { cn } from "../ui/cn.ts";
  import { HOVER_ROW, ICON_WELL, SURFACE_CARD } from "../ui/styles.ts";
  import Link from "./Link.svelte";

  let {
    href,
    title,
    subtitle,
    icon: Icon,
  }: {
    href: string;
    title: string;
    subtitle?: string;
    icon: IconComponent;
  } = $props();
</script>

<Link
  {href}
  class={cn("flex items-center gap-3 px-4 py-4 text-left focus-ring", HOVER_ROW, SURFACE_CARD)}
>
  <span class={cn(ICON_WELL, "h-10 w-10")}>
    <Icon size={20} aria-hidden="true" />
  </span>
  <span class="min-w-0">
    <span class="block font-semibold text-text-bright">{title}</span>
    {#if subtitle}
      <span class="block truncate text-sm text-muted">{subtitle}</span>
    {/if}
  </span>
</Link>

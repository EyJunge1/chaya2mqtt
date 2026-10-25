<script lang="ts">
  import NavCard from "../components/NavCard.svelte";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { settingsNavItems } from "../nav/settingsNav.ts";
</script>

<div class="grid gap-3 sm:grid-cols-2">
  {#each settingsNavItems as item (item.to)}
    <NavCard
      href={item.to}
      title={i18n.t(item.labelKey)}
      subtitle={i18n.t(item.subtitleKey)}
      icon={item.icon}
    />
  {/each}
</div>

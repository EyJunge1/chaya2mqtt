<script lang="ts">
  import { Moon, Sun } from "@lucide/svelte";
  import { cycleLanguage } from "../i18n/store.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { themeView } from "../theme/theme.svelte.ts";
  import { toggleTheme } from "../theme/store.ts";
  import GithubIcon from "./GithubIcon.svelte";
  import IconButton from "./IconButton.svelte";
  import { GITHUB_REPO_URL } from "./github.ts";
  import IconLink from "./IconLink.svelte";
  import ShellUtilityStrip from "./ShellUtilityStrip.svelte";

  let { collapsed = false }: { collapsed?: boolean } = $props();

  const isDark = $derived(themeView.theme === "dark");
  const themeLabel = $derived(
    isDark ? i18n.t("settings.theme-light") : i18n.t("settings.theme-dark"),
  );
  const langLabel = $derived(i18n.language.toUpperCase());
</script>

{#if collapsed}
  <div class="mt-auto flex flex-col items-center gap-1 p-2">
    <IconButton
      onclick={() => cycleLanguage()}
      aria-label={i18n.t("nav.language")}
      title={`${i18n.t("nav.language")}: ${langLabel}`}
      class="text-xs font-semibold tabular-nums"
    >
      {langLabel}
    </IconButton>
    <IconButton onclick={toggleTheme} aria-label={themeLabel} title={themeLabel}>
      {#if isDark}
        <Moon size={14} aria-hidden="true" />
      {:else}
        <Sun size={14} aria-hidden="true" />
      {/if}
    </IconButton>
    <IconLink
      href={GITHUB_REPO_URL}
      target="_blank"
      rel="noopener noreferrer"
      title={i18n.t("nav.github")}
      aria-label={i18n.t("nav.github")}
    >
      <GithubIcon size={14} />
    </IconLink>
  </div>
{:else}
  <div class="mt-auto p-3">
    <ShellUtilityStrip />
  </div>
{/if}

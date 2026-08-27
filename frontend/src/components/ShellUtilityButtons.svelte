<script lang="ts">
  import { Languages, Moon, Sun } from "@lucide/svelte";
  import { cycleLanguage } from "../i18n/store.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { themeView } from "../theme/theme.svelte.ts";
  import { toggleTheme } from "../theme/store.ts";
  import { cn } from "../ui/cn.ts";
  import { HOVER_SURFACE } from "../ui/styles.ts";
  import { GITHUB_REPO_URL } from "./github.ts";
  import GithubIcon from "./GithubIcon.svelte";
  import IconButton from "./IconButton.svelte";
  import IconLink from "./IconLink.svelte";

  const isDark = $derived(themeView.theme === "dark");
  const themeLabel = $derived(
    isDark ? i18n.t("settings.theme-light") : i18n.t("settings.theme-dark"),
  );
  const langLabel = $derived(i18n.language.toUpperCase());
</script>

<div class="flex items-center gap-1.5">
  <button
    type="button"
    class={cn(
      "inline-flex h-8 w-14 shrink-0 items-center justify-center gap-1 rounded-lg border border-border bg-surface text-xs font-semibold text-muted tabular-nums transition focus-ring",
      HOVER_SURFACE,
    )}
    onclick={() => cycleLanguage()}
    aria-label={i18n.t("nav.language")}
    title={`${i18n.t("nav.language")}: ${langLabel}`}
  >
    <Languages size={14} aria-hidden="true" />
    <span>{langLabel}</span>
  </button>
  <IconButton
    variant="bordered"
    size="sm"
    onclick={toggleTheme}
    aria-label={themeLabel}
    title={themeLabel}
  >
    {#if isDark}
      <Moon size={16} aria-hidden="true" />
    {:else}
      <Sun size={16} aria-hidden="true" />
    {/if}
  </IconButton>
  <IconLink
    variant="bordered"
    size="sm"
    href={GITHUB_REPO_URL}
    target="_blank"
    rel="noopener noreferrer"
    title={i18n.t("nav.github")}
    aria-label={i18n.t("nav.github")}
  >
    <GithubIcon size={16} />
  </IconLink>
</div>

<script lang="ts">
  import { Languages, Moon, Sun } from "@lucide/svelte";
  import { cycleLanguage } from "../i18n/store.ts";
  import { i18n } from "../i18n/i18n.svelte.ts";
  import { persistUiPrefsDebounced } from "../prefs/uiPrefs.ts";
  import { themeView } from "../theme/theme.svelte.ts";
  import { toggleTheme, type ThemePreference } from "../theme/store.ts";
  import { cn } from "../ui/cn.ts";
  import { HOVER_SURFACE } from "../ui/styles.ts";
  import { GITHUB_REPO_URL } from "./github.ts";
  import GithubIcon from "./GithubIcon.svelte";

  let { class: className = "" }: { class?: string } = $props();

  const CYCLE: ThemePreference[] = ["system", "light", "dark"];

  const isDark = $derived(themeView.theme === "dark");
  const nextPreference = $derived(CYCLE[(CYCLE.indexOf(themeView.preference) + 1) % CYCLE.length]!);
  const themeLabel = $derived(
    nextPreference === "system"
      ? i18n.t("settings.theme-system")
      : nextPreference === "light"
        ? i18n.t("settings.theme-light")
        : i18n.t("settings.theme-dark"),
  );
  const langLabel = $derived(i18n.language.toUpperCase());

  const btn = cn(
    "focus-ring inline-flex shrink-0 items-center justify-center rounded-xl text-muted transition",
    HOVER_SURFACE,
  );

  function onCycleLanguage() {
    cycleLanguage();
    persistUiPrefsDebounced();
  }

  function onToggleTheme() {
    toggleTheme();
    persistUiPrefsDebounced();
  }
</script>

<div class={cn("flex items-center gap-1", className)}>
  <button
    type="button"
    onclick={onCycleLanguage}
    class={cn(btn, "h-10 w-[4.25rem] gap-1.5")}
    aria-label={i18n.t("nav.language")}
    title={`${i18n.t("nav.language")}: ${langLabel}`}
  >
    <Languages size={17} class="shrink-0" aria-hidden="true" />
    <span class="w-5 text-center text-xs font-bold tabular-nums">{langLabel}</span>
  </button>
  <button
    type="button"
    onclick={onToggleTheme}
    class={cn(btn, "size-10")}
    aria-label={themeLabel}
    title={themeLabel}
  >
    {#if isDark}
      <Moon size={18} aria-hidden="true" />
    {:else}
      <Sun size={18} aria-hidden="true" />
    {/if}
  </button>
  <a
    href={GITHUB_REPO_URL}
    target="_blank"
    rel="noopener noreferrer"
    class={cn(btn, "size-10")}
    title={i18n.t("nav.github")}
    aria-label={i18n.t("nav.github")}
  >
    <GithubIcon size={18} />
  </a>
</div>

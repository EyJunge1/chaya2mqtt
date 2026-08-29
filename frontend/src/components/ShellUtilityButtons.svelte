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

  let {
    stacked = false,
    class: className = "",
  }: {
    stacked?: boolean;
    class?: string;
  } = $props();

  const isDark = $derived(themeView.theme === "dark");
  const themeLabel = $derived(
    isDark ? i18n.t("settings.theme-light") : i18n.t("settings.theme-dark"),
  );
  const langLabel = $derived(i18n.language.toUpperCase());

  const btn = cn(
    "focus-ring inline-flex shrink-0 items-center justify-center rounded-xl text-muted transition",
    HOVER_SURFACE,
  );
</script>

<div class={cn(stacked ? "flex flex-col items-center gap-1" : "flex items-center gap-1", className)}>
  <button
    type="button"
    onclick={() => cycleLanguage()}
    class={cn(
      btn,
      stacked ? "size-10" : "h-10 w-[4.25rem] gap-1.5",
    )}
    aria-label={i18n.t("nav.language")}
    title={`${i18n.t("nav.language")}: ${langLabel}`}
  >
    {#if !stacked}
      <Languages size={17} class="shrink-0" aria-hidden="true" />
    {/if}
    <span class={cn("text-xs font-bold tabular-nums", stacked ? "" : "w-5 text-center")}>
      {langLabel}
    </span>
  </button>
  <button
    type="button"
    onclick={toggleTheme}
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

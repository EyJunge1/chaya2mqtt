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

  let { compact = false, class: className = "" }: { compact?: boolean; class?: string } = $props();

  const isDark = $derived(themeView.theme === "dark");
  const themeLabel = $derived(
    isDark ? i18n.t("settings.theme-light") : i18n.t("settings.theme-dark"),
  );
  const langLabel = $derived(i18n.language.toUpperCase());
  const stripBtn = cn("inline-flex items-center text-muted transition focus-ring", HOVER_SURFACE);
</script>

<div
  class={cn(
    "flex items-center overflow-hidden rounded-lg border border-border bg-bg",
    compact ? "w-fit" : "w-full",
    className,
  )}
>
  <button
    type="button"
    class={cn(stripBtn, compact ? "gap-1 px-2.5 py-1.5" : "min-w-0 flex-1 gap-1.5 px-2 py-1.5")}
    onclick={() => cycleLanguage()}
    aria-label={i18n.t("nav.language")}
    title={`${i18n.t("nav.language")}: ${langLabel}`}
  >
    <Languages size={compact ? 13 : 14} class="shrink-0" aria-hidden="true" />
    <span class={cn("truncate", compact ? "text-[0.75rem]" : "text-xs")}>
      {i18n.t("nav.language")}
    </span>
    <span class={cn("font-semibold tabular-nums", compact ? "text-[0.75rem]" : "ms-auto text-xs")}>
      {langLabel}
    </span>
  </button>
  <button
    type="button"
    class={cn(stripBtn, "self-stretch border-l border-border px-2.5")}
    onclick={toggleTheme}
    aria-label={themeLabel}
    title={themeLabel}
  >
    {#if isDark}
      <Moon size={compact ? 13 : 14} aria-hidden="true" />
    {:else}
      <Sun size={compact ? 13 : 14} aria-hidden="true" />
    {/if}
  </button>
  <a
    href={GITHUB_REPO_URL}
    target="_blank"
    rel="noopener noreferrer"
    class={cn(stripBtn, "self-stretch border-l border-border px-2.5")}
    title={i18n.t("nav.github")}
    aria-label={i18n.t("nav.github")}
  >
    <GithubIcon size={compact ? 13 : 14} />
  </a>
</div>

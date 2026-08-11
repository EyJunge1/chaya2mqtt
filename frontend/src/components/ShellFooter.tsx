import { Languages, Moon, Sun } from "lucide-react";
import { cycleLanguage } from "../i18n/store";
import { useI18n } from "../i18n/useI18n";
import { useTheme } from "../theme/useTheme";
import { toggleTheme } from "../theme/store";
import { cn } from "../ui/cn";
import { HOVER_SURFACE } from "../ui/styles";
import { GithubIcon } from "./GithubIcon";
import { IconButton, IconLink } from "./IconButton";

export const GITHUB_REPO_URL = "https://github.com/EyJunge1/chaya2mqtt";

function ShellUtilityStrip({
  compact = false,
  className = "",
}: {
  compact?: boolean;
  className?: string;
}) {
  const { t, language } = useI18n();
  const theme = useTheme();
  const isDark = theme === "dark";
  const themeLabel = isDark ? t("settings.theme-light") : t("settings.theme-dark");
  const langLabel = language.toUpperCase();
  const stripBtn = cn("inline-flex items-center text-muted transition focus-ring", HOVER_SURFACE);

  return (
    <div
      className={cn(
        "flex items-center overflow-hidden rounded-lg border border-border bg-bg",
        compact ? "w-fit" : "w-full",
        className,
      )}
    >
      <button
        type="button"
        className={cn(
          stripBtn,
          compact ? "gap-1 px-2.5 py-1.5" : "min-w-0 flex-1 gap-1.5 px-2 py-1.5",
        )}
        onClick={() => cycleLanguage()}
        aria-label={t("nav.language")}
        title={`${t("nav.language")}: ${langLabel}`}
      >
        <Languages size={compact ? 13 : 14} className="shrink-0" aria-hidden />
        <span className={cn("truncate", compact ? "text-[0.75rem]" : "text-xs")}>
          {t("nav.language")}
        </span>
        <span
          className={cn(
            "font-semibold tabular-nums",
            compact ? "text-[0.75rem]" : "ms-auto text-xs",
          )}
        >
          {langLabel}
        </span>
      </button>
      <button
        type="button"
        className={cn(stripBtn, "self-stretch border-l border-border px-2.5")}
        onClick={toggleTheme}
        aria-label={themeLabel}
        title={themeLabel}
      >
        {isDark ? (
          <Moon size={compact ? 13 : 14} aria-hidden />
        ) : (
          <Sun size={compact ? 13 : 14} aria-hidden />
        )}
      </button>
      <a
        href={GITHUB_REPO_URL}
        target="_blank"
        rel="noopener noreferrer"
        className={cn(stripBtn, "self-stretch border-l border-border px-2.5")}
        title={t("nav.github")}
        aria-label={t("nav.github")}
      >
        <GithubIcon size={compact ? 13 : 14} />
      </a>
    </div>
  );
}

export function ShellFooter({ collapsed = false }: { collapsed?: boolean }) {
  const { t, language } = useI18n();
  const theme = useTheme();
  const isDark = theme === "dark";
  const themeLabel = isDark ? t("settings.theme-light") : t("settings.theme-dark");
  const langLabel = language.toUpperCase();

  if (collapsed) {
    return (
      <div className="mt-auto flex flex-col items-center gap-1 p-2">
        <IconButton
          onClick={() => cycleLanguage()}
          aria-label={t("nav.language")}
          title={`${t("nav.language")}: ${langLabel}`}
          className="text-xs font-semibold tabular-nums"
        >
          {langLabel}
        </IconButton>
        <IconButton onClick={toggleTheme} aria-label={themeLabel} title={themeLabel}>
          {isDark ? <Moon size={14} aria-hidden /> : <Sun size={14} aria-hidden />}
        </IconButton>
        <IconLink
          href={GITHUB_REPO_URL}
          target="_blank"
          rel="noopener noreferrer"
          title={t("nav.github")}
          aria-label={t("nav.github")}
        >
          <GithubIcon size={14} />
        </IconLink>
      </div>
    );
  }

  return (
    <div className="mt-auto p-3">
      <ShellUtilityStrip />
    </div>
  );
}

/** Language + theme + GitHub strip for the mobile header. */
export function ShellUtilityButtons() {
  return <ShellUtilityStrip compact />;
}

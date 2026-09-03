import { api } from "../api/client.ts";
import { getLanguage, setLanguage } from "../i18n/store.ts";
import { getThemePreference, setTheme } from "../theme/store.ts";
import type { UiLang, UiTheme } from "../api/types.ts";

let applyingFromDevice = false;
let persistTimer: ReturnType<typeof setTimeout> | undefined;

/** Apply lang/theme from device NVS. Device is source of truth when settings load succeeds. */
export function applyDeviceUiPrefs(lang: string, theme: string): void {
  applyingFromDevice = true;
  try {
    if (lang === "de" || lang === "en") setLanguage(lang);
    if (theme === "dark" || theme === "light" || theme === "system") setTheme(theme);
  } finally {
    applyingFromDevice = false;
  }
}

/** Persist current browser lang/theme to device (debounced). No-op while applying from device. */
export function persistUiPrefsDebounced(delayMs = 400): void {
  if (applyingFromDevice) return;
  clearTimeout(persistTimer);
  persistTimer = setTimeout(() => {
    void api
      .saveSettings({
        lang: getLanguage() as UiLang,
        theme: getThemePreference() as UiTheme,
      })
      .catch(() => {
        /* AP mode / offline — localStorage already updated */
      });
  }, delayMs);
}

export function flushUiPrefsPersist(): void {
  clearTimeout(persistTimer);
  persistTimer = undefined;
}

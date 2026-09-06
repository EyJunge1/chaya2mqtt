import { api } from "../api/client.ts";
import { getLanguage, setLanguage } from "../i18n/store.ts";
import { getThemePreference, setTheme } from "../theme/store.ts";
import type { UiLang, UiTheme } from "../api/types.ts";

const PREFS_APPLY_POLL_MS = 80;
const PREFS_APPLY_POLL_MAX = 25;
const PREFS_RETRY_MS = 800;

let applyingFromDevice = false;
let persistTimer: ReturnType<typeof setTimeout> | undefined;
let writeTail: Promise<void> = Promise.resolve();

/** Serialize settings POSTs so prefs persist and device save cannot race. */
export function enqueueSettingsWrite<T>(task: () => Promise<T>): Promise<T> {
  const run = writeTail.then(task, task);
  writeTail = run.then(
    () => undefined,
    () => undefined,
  );
  return run;
}

async function waitUntilSettingsIdle(): Promise<boolean> {
  for (let i = 0; i < PREFS_APPLY_POLL_MAX; i++) {
    try {
      const s = await api.getSettings();
      if (!s.applyPending) return true;
    } catch {
      /* keep polling — a single GET fail must not drop the write */
    }
    if (i < PREFS_APPLY_POLL_MAX - 1) {
      await new Promise((r) => setTimeout(r, PREFS_APPLY_POLL_MS));
    }
  }
  return false;
}

async function persistUiPrefs(opts?: { retry?: boolean }): Promise<void> {
  let retry = false;
  await enqueueSettingsWrite(async () => {
    if (!(await waitUntilSettingsIdle())) {
      retry = true;
      return;
    }
    await api
      .saveSettings({
        lang: getLanguage() as UiLang,
        theme: getThemePreference() as UiTheme,
      })
      .catch(() => {
        /* AP mode / offline — localStorage already updated */
      });
  });
  if (retry && opts?.retry !== false) {
    persistUiPrefsDebounced(PREFS_RETRY_MS);
  }
}

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
    persistTimer = undefined;
    void persistUiPrefs();
  }, delayMs);
}

/** Drop a debounced prefs write without sending (caller will POST a full snapshot). */
export function cancelUiPrefsPersist(): void {
  clearTimeout(persistTimer);
  persistTimer = undefined;
}

/** Clear the debounce timer and send the pending prefs write now. */
export function flushUiPrefsPersist(): Promise<void> {
  if (persistTimer === undefined) return Promise.resolve();
  clearTimeout(persistTimer);
  persistTimer = undefined;
  return persistUiPrefs({ retry: false });
}

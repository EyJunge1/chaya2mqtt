export type Theme = "dark" | "light";
export type ThemePreference = "system" | Theme;

const THEME_STORAGE_KEY = "chaya2mqtt.theme";
const PREFERENCE_CYCLE: ThemePreference[] = ["system", "light", "dark"];

function isThemePreference(value: string | null | undefined): value is ThemePreference {
  return value === "system" || value === "dark" || value === "light";
}

function systemPrefersDark(): boolean {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return false;
  }
  try {
    return window.matchMedia("(prefers-color-scheme: dark)").matches;
  } catch {
    return false;
  }
}

function resolveTheme(preference: ThemePreference): Theme {
  if (preference === "light") return "light";
  if (preference === "dark") return "dark";
  return systemPrefersDark() ? "dark" : "light";
}

function readStoredPreference(): ThemePreference {
  if (typeof window === "undefined") return "system";
  try {
    const stored = window.localStorage.getItem(THEME_STORAGE_KEY);
    return isThemePreference(stored) ? stored : "system";
  } catch {
    return "system";
  }
}

let currentPreference: ThemePreference = readStoredPreference();
let currentTheme: Theme = resolveTheme(currentPreference);
const listeners = new Set<() => void>();

let mediaQuery: MediaQueryList | null = null;
let mediaListener: ((event: MediaQueryListEvent) => void) | null = null;

function emit() {
  for (const listener of listeners) listener();
}

function applyDom(theme: Theme) {
  if (typeof document === "undefined") return;
  document.documentElement.dataset.theme = theme;
  document.documentElement.style.colorScheme = theme;
}

function teardownMediaListener() {
  if (mediaQuery && mediaListener) {
    mediaQuery.removeEventListener("change", mediaListener);
  }
  mediaQuery = null;
  mediaListener = null;
}

function syncMediaListener() {
  teardownMediaListener();
  if (currentPreference !== "system") return;
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") return;
  try {
    mediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    mediaListener = () => {
      currentTheme = resolveTheme("system");
      applyDom(currentTheme);
      emit();
    };
    mediaQuery.addEventListener("change", mediaListener);
  } catch {
    mediaQuery = null;
    mediaListener = null;
  }
}

function applyPreference(preference: ThemePreference) {
  currentPreference = preference;
  if (typeof window !== "undefined") {
    try {
      window.localStorage.setItem(THEME_STORAGE_KEY, preference);
    } catch {
      /* Preference still applies when browser storage is unavailable. */
    }
  }
  currentTheme = resolveTheme(preference);
  applyDom(currentTheme);
  syncMediaListener();
  emit();
}

export function subscribeTheme(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** Resolved appearance (`light` / `dark`) for CSS and UI chrome. */
export function getTheme(): Theme {
  return currentTheme;
}

/** Stored preference, including `system`. */
export function getThemePreference(): ThemePreference {
  return currentPreference;
}

export function setTheme(theme: ThemePreference) {
  if (!isThemePreference(theme)) return;
  applyPreference(theme);
}

/** Cycle system → light → dark → system. Returns the new preference. */
export function toggleTheme(): ThemePreference {
  const idx = PREFERENCE_CYCLE.indexOf(currentPreference);
  const next = PREFERENCE_CYCLE[(idx + 1) % PREFERENCE_CYCLE.length]!;
  applyPreference(next);
  return next;
}

if (typeof document !== "undefined") {
  applyDom(currentTheme);
  syncMediaListener();
}

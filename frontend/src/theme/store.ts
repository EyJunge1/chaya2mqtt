export type Theme = "dark" | "light";

const THEME_STORAGE_KEY = "chaya2mqtt.theme";

function readStoredTheme(): Theme {
  if (typeof window === "undefined") return "light";
  try {
    const stored = window.localStorage.getItem(THEME_STORAGE_KEY);
    return stored === "dark" || stored === "light" ? stored : "light";
  } catch {
    return "light";
  }
}

let currentTheme: Theme = readStoredTheme();
const listeners = new Set<() => void>();

function emit() {
  for (const listener of listeners) listener();
}

function applyTheme(theme: Theme) {
  currentTheme = theme;
  if (typeof window !== "undefined") {
    try {
      window.localStorage.setItem(THEME_STORAGE_KEY, theme);
    } catch {
      /* Theme still applies when browser storage is unavailable. */
    }
  }
  if (typeof document !== "undefined") {
    document.documentElement.dataset.theme = theme;
    document.documentElement.style.colorScheme = theme;
  }
  emit();
}

export function subscribeTheme(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function getTheme(): Theme {
  return currentTheme;
}

export function setTheme(theme: Theme) {
  if (theme !== "dark" && theme !== "light") return;
  applyTheme(theme);
}

export function toggleTheme(): Theme {
  const next: Theme = currentTheme === "dark" ? "light" : "dark";
  applyTheme(next);
  return next;
}

if (typeof document !== "undefined") {
  document.documentElement.dataset.theme = currentTheme;
  document.documentElement.style.colorScheme = currentTheme;
}

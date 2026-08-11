import { translations, type Lang, type TranslationKey } from "./translations";

export type TranslateFn = (key: TranslationKey, params?: Record<string, string | number>) => string;

const LANGS: Lang[] = ["en", "de"];
const LANG_STORAGE_KEY = "chaya2mqtt.lang";

function detectBrowserLang(): Lang {
  if (typeof navigator === "undefined") return "en";
  const candidates = [...(navigator.languages ?? []), navigator.language];
  for (const raw of candidates) {
    if (!raw) continue;
    const code = raw.toLowerCase().split("-")[0];
    if (code === "de") return "de";
    if (code === "en") return "en";
  }
  return "en";
}

function readStoredLang(): Lang {
  if (typeof window === "undefined") return detectBrowserLang();
  try {
    const stored = window.localStorage.getItem(LANG_STORAGE_KEY);
    if (stored === "de" || stored === "en") return stored;
  } catch {
    /* fall through to browser language */
  }
  return detectBrowserLang();
}

let currentLang: Lang = readStoredLang();
const listeners = new Set<() => void>();

function emit() {
  for (const listener of listeners) listener();
}

function applyLang(lang: Lang) {
  currentLang = lang;
  if (typeof window !== "undefined") {
    try {
      window.localStorage.setItem(LANG_STORAGE_KEY, lang);
    } catch {
      /* Language still applies when browser storage is unavailable. */
    }
  }
  if (typeof document !== "undefined") {
    document.documentElement.lang = lang;
  }
  emit();
}

export function subscribeLanguage(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function getLanguage(): Lang {
  return currentLang;
}

export function setLanguage(lang: Lang) {
  if (lang !== "de" && lang !== "en") return;
  applyLang(lang);
}

export function cycleLanguage(): Lang {
  const next = LANGS[(LANGS.indexOf(getLanguage()) + 1) % LANGS.length] ?? "en";
  setLanguage(next);
  return next;
}

export function t(
  key: TranslationKey,
  params: Record<string, string | number> = {},
  lang: Lang = getLanguage(),
): string {
  let text: string = translations[lang][key] || translations.en[key] || key;
  for (const [k, v] of Object.entries(params)) {
    text = text.replaceAll(`{${k}}`, String(v));
  }
  return text;
}

if (typeof document !== "undefined") {
  document.documentElement.lang = currentLang;
}

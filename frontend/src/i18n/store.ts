import { translations, type Lang, type TranslationKey } from './translations'

export type TranslateFn = (key: TranslationKey, params?: Record<string, string | number>) => string

let currentLang: Lang = 'en'
const listeners = new Set<() => void>()

function emit() {
  for (const listener of listeners) listener()
}

function applyLang(lang: Lang) {
  currentLang = lang
  if (typeof document !== 'undefined') {
    document.documentElement.lang = lang
  }
  emit()
}

export function subscribeLanguage(listener: () => void) {
  listeners.add(listener)
  return () => {
    listeners.delete(listener)
  }
}

export function getLanguage(): Lang {
  return currentLang
}

export function setLanguage(lang: Lang) {
  if (lang !== 'de' && lang !== 'en') return
  applyLang(lang)
}

export function t(
  key: TranslationKey,
  params: Record<string, string | number> = {},
  lang: Lang = getLanguage(),
): string {
  let text: string = translations[lang][key] || translations.en[key] || key
  for (const [k, v] of Object.entries(params)) {
    text = text.replaceAll(`{${k}}`, String(v))
  }
  return text
}

if (typeof document !== 'undefined') {
  document.documentElement.lang = currentLang
}

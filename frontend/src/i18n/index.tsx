import {
  createContext,
  createElement,
  useContext,
  useMemo,
  useSyncExternalStore,
  type ReactNode,
} from 'react'
import { translations, type Lang, type TranslationKey } from './translations'

export type TranslateFn = (
  key: TranslationKey,
  params?: Record<string, string | number>,
) => string

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

function subscribe(listener: () => void) {
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

type I18nContextValue = {
  language: Lang
  t: TranslateFn
  setLanguage: (lang: Lang) => void
}

const I18nContext = createContext<I18nContextValue | null>(null)

function useLanguageStore(): Lang {
  return useSyncExternalStore(subscribe, getLanguage, () => 'en' as Lang)
}

export function I18nProvider({ children }: { children: ReactNode }) {
  const language = useLanguageStore()
  const value = useMemo<I18nContextValue>(
    () => ({
      language,
      t: (key, params = {}) => t(key, params, language),
      setLanguage,
    }),
    [language],
  )
  return createElement(I18nContext.Provider, { value }, children)
}

export function useI18n(): I18nContextValue {
  const ctx = useContext(I18nContext)
  const language = useLanguageStore()
  return useMemo(
    () =>
      ctx ?? {
        language,
        t: (key, params = {}) => t(key, params, language),
        setLanguage,
      },
    [ctx, language],
  )
}

if (typeof document !== 'undefined') {
  document.documentElement.lang = currentLang
}

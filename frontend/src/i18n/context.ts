import { createContext, useSyncExternalStore } from 'react'
import type { Lang } from './translations'
import { getLanguage, setLanguage, subscribeLanguage, t, type TranslateFn } from './store'

export type I18nContextValue = {
  language: Lang
  t: TranslateFn
  setLanguage: (lang: Lang) => void
}

export const I18nContext = createContext<I18nContextValue | null>(null)

export function useLanguageStore(): Lang {
  return useSyncExternalStore(subscribeLanguage, getLanguage, () => 'en' as Lang)
}

export function createI18nValue(language: Lang): I18nContextValue {
  return {
    language,
    t: (key, params = {}) => t(key, params, language),
    setLanguage,
  }
}

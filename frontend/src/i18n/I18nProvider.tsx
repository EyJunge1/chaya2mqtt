import { createElement, useMemo, type ReactNode } from 'react'
import { createI18nValue, I18nContext, useLanguageStore } from './context'

export function I18nProvider({ children }: { children: ReactNode }) {
  const language = useLanguageStore()
  const value = useMemo(() => createI18nValue(language), [language])
  return createElement(I18nContext.Provider, { value }, children)
}

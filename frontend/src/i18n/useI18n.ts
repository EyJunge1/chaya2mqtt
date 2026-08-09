import { useContext, useMemo } from 'react'
import { createI18nValue, I18nContext, useLanguageStore } from './context'

export function useI18n() {
  const ctx = useContext(I18nContext)
  const language = useLanguageStore()
  return useMemo(() => ctx ?? createI18nValue(language), [ctx, language])
}

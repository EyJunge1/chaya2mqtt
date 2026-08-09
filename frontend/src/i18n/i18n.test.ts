import { describe, expect, it } from 'vitest'
import { setLanguage, t } from './store'
import { translations } from './translations'

describe('i18n', () => {
  it('defaults to English and switches languages', () => {
    setLanguage('en')
    expect(t('common.save')).toBe(translations.en['common.save'])
    setLanguage('de')
    expect(t('common.save')).toBe(translations.de['common.save'])
  })

  it('keeps de and en key sets in sync', () => {
    const deKeys = Object.keys(translations.de).sort()
    const enKeys = Object.keys(translations.en).sort()
    expect(enKeys).toEqual(deKeys)
  })
})

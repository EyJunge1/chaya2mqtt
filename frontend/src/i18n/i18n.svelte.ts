import {
  getLanguage,
  setLanguage,
  subscribeLanguage,
  t as translate,
  type TranslateFn,
} from "./store.ts";
import type { Lang } from "./translations.ts";

class I18n {
  language = $state<Lang>(getLanguage());

  constructor() {
    subscribeLanguage(() => {
      this.language = getLanguage();
    });
  }

  t: TranslateFn = (key, params) => translate(key, params, this.language);
  setLanguage = setLanguage;
}

export const i18n = new I18n();

export function useI18n() {
  return i18n;
}

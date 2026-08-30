import {
  getTheme,
  getThemePreference,
  subscribeTheme,
  type Theme,
  type ThemePreference,
} from "./store.ts";

class ThemeView {
  theme = $state<Theme>(getTheme());
  preference = $state<ThemePreference>(getThemePreference());

  constructor() {
    subscribeTheme(() => {
      this.theme = getTheme();
      this.preference = getThemePreference();
    });
  }
}

export const themeView = new ThemeView();

export function useTheme(): Theme {
  return themeView.theme;
}

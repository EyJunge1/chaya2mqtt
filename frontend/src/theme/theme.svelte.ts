import { getTheme, subscribeTheme, type Theme } from "./store.ts";

class ThemeView {
  theme = $state<Theme>(getTheme());

  constructor() {
    subscribeTheme(() => {
      this.theme = getTheme();
    });
  }
}

export const themeView = new ThemeView();

export function useTheme(): Theme {
  return themeView.theme;
}

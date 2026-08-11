import { render, type RenderOptions } from "@testing-library/react";
import { createElement, type ReactElement, type ReactNode } from "react";
import { MemoryRouter } from "react-router-dom";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";

type Options = {
  route?: string;
  language?: "de" | "en";
} & Omit<RenderOptions, "wrapper">;

function wrap(route: string, children: ReactNode) {
  return createElement(
    MemoryRouter,
    { initialEntries: [route] },
    createElement(I18nProvider, null, children),
  );
}

export function renderApp(ui: ReactElement, options: Options = {}) {
  const { route = "/", language = "de", ...rest } = options;
  setLanguage(language);
  return render(ui, {
    wrapper: ({ children }) => wrap(route, children),
    ...rest,
  });
}

import { render, type RenderOptions } from "@testing-library/svelte";
import type { Component, ComponentProps } from "svelte";
import { setLanguage } from "../i18n/store.ts";
import { router } from "../nav/router.svelte.ts";

type Options<T extends Component<Record<string, unknown>>> = {
  route?: string;
  language?: "de" | "en";
  props?: ComponentProps<T>;
} & Omit<RenderOptions<T>, "props">;

export function renderApp<T extends Component<Record<string, unknown>>>(
  component: T,
  options: Options<T> = {},
) {
  const { route = "/", language = "de", props, ...rest } = options;
  setLanguage(language);
  router.replace(route);
  return render(component, { props, ...rest } as RenderOptions<T>);
}

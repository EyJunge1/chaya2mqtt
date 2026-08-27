import { render } from "@testing-library/svelte";
import type { Component } from "svelte";
import { setLanguage } from "../i18n/store.ts";
import { router } from "../nav/router.svelte.ts";

type Options<Props extends Record<string, unknown>> = {
  route?: string;
  language?: "de" | "en";
  props?: Props;
};

export function renderApp<Props extends Record<string, unknown>>(
  component: Component<Props>,
  options: Options<Props> = {},
) {
  const { route = "/", language = "de", props } = options;
  setLanguage(language);
  router.replace(route);
  return props === undefined
    ? render(component as Component)
    : render(component as Component, { props });
}

import { Radio, Smartphone, Upload, Wifi } from "@lucide/svelte";
import type { Component } from "svelte";
import type { TranslationKey } from "../i18n/translations.ts";

export type IconComponent = Component<{
  size?: number;
  class?: string;
  "aria-hidden"?: boolean | "true" | "false";
}>;

export type SettingsNavItem = {
  to: string;
  labelKey: TranslationKey;
  subtitleKey: TranslationKey;
  icon: IconComponent;
};

export const settingsNavItems: SettingsNavItem[] = [
  { to: "/wifi", labelKey: "nav.wifi", subtitleKey: "nav.wifi-sub", icon: Wifi },
  { to: "/mqtt", labelKey: "nav.mqtt", subtitleKey: "nav.mqtt-sub", icon: Radio },
  {
    to: "/settings/device",
    labelKey: "nav.device",
    subtitleKey: "nav.device-sub",
    icon: Smartphone,
  },
  { to: "/update", labelKey: "nav.update", subtitleKey: "nav.update-sub", icon: Upload },
];

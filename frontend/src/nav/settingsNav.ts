import { Radio, SlidersHorizontal, Upload, Wifi, type LucideIcon } from "lucide-react";
import type { TranslationKey } from "../i18n/translations";

export type SettingsNavItem = {
  to: string;
  labelKey: TranslationKey;
  subtitleKey: TranslationKey;
  icon: LucideIcon;
};

export const settingsNavItems: SettingsNavItem[] = [
  { to: "/wifi", labelKey: "nav.wifi", subtitleKey: "nav.wifi-sub", icon: Wifi },
  { to: "/mqtt", labelKey: "nav.mqtt", subtitleKey: "nav.mqtt-sub", icon: Radio },
  {
    to: "/settings/device",
    labelKey: "nav.device",
    subtitleKey: "nav.device-sub",
    icon: SlidersHorizontal,
  },
  { to: "/update", labelKey: "nav.update", subtitleKey: "nav.update-sub", icon: Upload },
];

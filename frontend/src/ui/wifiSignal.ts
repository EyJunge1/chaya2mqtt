import { Wifi, WifiHigh, WifiLow } from "@lucide/svelte";
import type { WifiStatus } from "../api/types.ts";
import type { IconComponent } from "../nav/settingsNav.ts";

export function wifiSignalIcon(wifi: WifiStatus): IconComponent {
  if (!wifi.connected || wifi.rssi >= -60) return Wifi;
  if (wifi.rssi >= -72) return WifiHigh;
  return WifiLow;
}

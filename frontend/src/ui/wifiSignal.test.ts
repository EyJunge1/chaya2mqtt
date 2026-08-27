import { Wifi, WifiHigh, WifiLow } from "@lucide/svelte";
import { describe, expect, it } from "vitest";
import type { WifiStatus } from "../api/types.ts";
import { wifiSignalIcon } from "./wifiSignal.ts";

function connected(rssi: number): WifiStatus {
  return {
    connected: true,
    ssid: "Home",
    ip: "192.168.1.2",
    gateway: "192.168.1.1",
    netmask: "255.255.255.0",
    dns1: "1.1.1.1",
    dns2: "",
    rssi,
  };
}

describe("wifiSignalIcon", () => {
  it.each([
    [{ connected: false } satisfies WifiStatus, Wifi],
    [connected(-60), Wifi],
    [connected(-61), WifiHigh],
    [connected(-72), WifiHigh],
    [connected(-73), WifiLow],
  ])("maps Wi-Fi status %# to the expected icon", (status, icon) => {
    expect(wifiSignalIcon(status)).toBe(icon);
  });
});

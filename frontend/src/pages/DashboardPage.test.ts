import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { ChayaStatus, DeviceInfo, WifiStatus } from "../api/types.ts";
import { setLanguage } from "../i18n/store.ts";
import DashboardPage from "./DashboardPage.svelte";

const sendChaya = vi.fn();
const scanWifi = vi.fn();
const getWifiConfig = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    sendChaya: () => sendChaya(),
    scanWifi: () => scanWifi(),
    getWifiConfig: () => getWifiConfig(),
  },
}));

const device: DeviceInfo = {
  mode: "sta",
  deviceId: "a1b2c3",
  version: "2026.8.1",
  hostname: "chaya2mqtt-a1b2c3",
  batteryMv: 3900,
  batteryPct: 55,
};

const wifi: WifiStatus = {
  connected: true,
  ssid: "Home",
  ip: "192.168.1.2",
  gateway: "192.168.1.1",
  netmask: "255.255.255.0",
  dns1: "1.1.1.1",
  dns2: "",
  rssi: -40,
};

const chaya: ChayaStatus = { connected: true, configured: true, rx: 3, tx: 1 };

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe("DashboardPage", () => {
  it.each([
    [100, "battery-full"],
    [80, "battery-full"],
    [79, "battery-medium"],
    [40, "battery-medium"],
    [39, "battery-low"],
    [15, "battery-low"],
    [14, "battery-warning"],
    [0, "battery-warning"],
  ])("shows the correct battery icon at %i%%", (batteryPct, iconName) => {
    setLanguage("en");
    const { container } = render(DashboardPage, {
      props: { device: { ...device, batteryPct }, chaya, wifi, onToast: vi.fn() },
    });

    expect(container.querySelector(`.lucide-${iconName}`)).toBeInTheDocument();
  });

  it("shows RadioOff when MQTT is not configured", () => {
    setLanguage("en");
    const { container } = render(DashboardPage, {
      props: {
        device,
        chaya: { connected: false, configured: false, rx: 0, tx: 0 },
        wifi,
        onToast: vi.fn(),
      },
    });

    expect(container.querySelector(".lucide-radio-off")).toBeInTheDocument();
  });

  it("shows Wi-Fi setup in AP mode without the display-scan card", async () => {
    setLanguage("en");
    scanWifi.mockResolvedValue([]);
    getWifiConfig.mockResolvedValue({
      ssid: "",
      mode: "dhcp",
      ip: "",
      gateway: "",
      netmask: "",
      dns1: "",
      dns2: "",
      ntp1: "",
      ntp2: "",
    });
    const apDevice: DeviceInfo = {
      ...device,
      mode: "ap",
      hostname: "chaya2mqtt",
      apSsid: "Chaya2MQTT",
      apIp: "4.3.2.1",
    };
    const wifiDown: WifiStatus = { connected: false };

    render(DashboardPage, {
      props: {
        device: apDevice,
        chaya: { connected: false, configured: false, rx: 0, tx: 0 },
        wifi: wifiDown,
        onToast: vi.fn(),
      },
    });

    expect(await screen.findByText("Networks")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Test & connect" })).toBeInTheDocument();
    expect(screen.queryByText(/Scan the display to join/i)).not.toBeInTheDocument();
    expect(screen.queryByText(/QR code on the device/i)).not.toBeInTheDocument();
  });

  it("renders heart counters and sends a heart", async () => {
    setLanguage("en");
    sendChaya.mockResolvedValue({ ok: true, queued: true });
    const onToast = vi.fn();

    render(DashboardPage, {
      props: { device, chaya, wifi, onToast },
    });

    expect(screen.getByText("3")).toBeInTheDocument();
    expect(screen.getByText("1")).toBeInTheDocument();
    expect(screen.getByLabelText(/Battery: 55%/i)).toBeInTheDocument();
    expect(screen.getByText(/55%/)).toBeInTheDocument();
    expect(screen.queryByText(/chaya2mqtt-a1b2c3\.local/)).not.toBeInTheDocument();
    expect(screen.queryByText("a1b2c3")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Send heart/i })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: /Send heart/i }));
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Heart sent", "success");
    });
  });

  it("shows a busy toast when the device rejects a second send", async () => {
    setLanguage("en");
    sendChaya.mockResolvedValue({ ok: false, error: "busy" });
    const onToast = vi.fn();

    render(DashboardPage, {
      props: { device, chaya, wifi, onToast },
    });

    fireEvent.click(screen.getByRole("button", { name: /Send heart/i }));
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Heart still sending — please wait", "error");
    });
    expect(onToast).not.toHaveBeenCalledWith("Send failed (MQTT offline?)", "error");
  });
});

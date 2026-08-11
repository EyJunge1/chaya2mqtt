import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { ChayaStatus, DeviceInfo, WifiStatus } from "../api/types";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { DashboardPage } from "./DashboardPage";

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
  hostname: "chaya2mqtt",
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

const chaya: ChayaStatus = { connected: true, rx: 3, tx: 1 };

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe("DashboardPage", () => {
  it("shows open SoftAP connection data in AP setup mode", async () => {
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
      apSsid: "Chaya2MQTT",
      apIp: "4.3.2.1",
    };
    const wifiDown: WifiStatus = { connected: false };

    render(
      <MemoryRouter>
        <I18nProvider>
          <DashboardPage
            device={apDevice}
            chaya={{ connected: false, rx: 0, tx: 0 }}
            wifi={wifiDown}
            onToast={vi.fn()}
          />
        </I18nProvider>
      </MemoryRouter>,
    );

    expect(await screen.findByText(/Connect to this device first/i)).toBeInTheDocument();
    expect(screen.getByText(/open Wi.Fi.*Chaya2MQTT/i)).toBeInTheDocument();
  });

  it("renders heart counters and sends a heart", async () => {
    setLanguage("en");
    sendChaya.mockResolvedValue({ ok: true, message: "sent" });
    const onToast = vi.fn();

    render(
      <MemoryRouter>
        <I18nProvider>
          <DashboardPage device={device} chaya={chaya} wifi={wifi} onToast={onToast} />
        </I18nProvider>
      </MemoryRouter>,
    );

    expect(screen.getByText("3")).toBeInTheDocument();
    expect(screen.getByText("1")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Send heart/i })).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: /Send heart/i }));
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Heart sent", "success");
    });
  });
});

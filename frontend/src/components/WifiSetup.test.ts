import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { WifiConfig, WifiStatus } from "../api/types.ts";
import WifiSetup from "./WifiSetup.svelte";

const startWifiScan = vi.fn();
const scanWifi = vi.fn();
const connectWifi = vi.fn();
const getWifiConfig = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    startWifiScan: () => startWifiScan(),
    scanWifi: () => scanWifi(),
    connectWifi: (fields: unknown) => connectWifi(fields),
    getWifiConfig: () => getWifiConfig(),
  },
}));

vi.mock("../i18n/i18n.svelte.ts", () => ({
  i18n: { t: (key: string) => key, language: "en", setLanguage: () => undefined },
  useI18n: () => ({ t: (key: string) => key }),
}));

const wifiConnected: WifiStatus = {
  connected: true,
  ssid: "HomeNet",
  ip: "192.168.1.42",
  gateway: "192.168.1.1",
  netmask: "255.255.255.0",
  dns1: "1.1.1.1",
  dns2: "1.0.0.1",
  rssi: -50,
};

const dhcpConfig: WifiConfig = {
  ssid: "HomeNet",
  mode: "dhcp",
  ip: "",
  gateway: "",
  netmask: "255.255.255.0",
  dns1: "",
  dns2: "",
  ntp1: "",
  ntp2: "",
};

function addServer(prefix: string, value: string) {
  fireEvent.click(screen.getByTestId(`${prefix}-add`));
  fireEvent.change(screen.getByTestId(`${prefix}-input`), { target: { value } });
  fireEvent.click(screen.getByTestId(`${prefix}-confirm`));
}

describe("WifiSetup", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    startWifiScan.mockResolvedValue({ ok: true });
    scanWifi.mockResolvedValue({ status: "ready", aps: [] });
    getWifiConfig.mockResolvedValue(dhcpConfig);
    connectWifi.mockResolvedValue({ ok: true, message: "saved_rebooting" });
  });

  it("renders duplicate scan rows without duplicate-key failure", async () => {
    scanWifi.mockResolvedValue({
      status: "ready",
      aps: [
        { ssid: "MeshNet", rssi: -48, open: false },
        { ssid: "MeshNet", rssi: -48, open: false },
      ],
    });

    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt",
          version: "dev",
          mode: "ap",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: { connected: false },
        onToast: vi.fn(),
      },
    });

    await waitFor(() => expect(screen.getAllByText("MeshNet")).toHaveLength(2));
    expect(startWifiScan).not.toHaveBeenCalled();
  });

  it("starts a sweep on mount when the snapshot is idle", async () => {
    scanWifi
      .mockResolvedValueOnce({ status: "idle" })
      .mockResolvedValue({
        status: "ready",
        aps: [{ ssid: "FreshNet", rssi: -40, open: true }],
      });

    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt",
          version: "dev",
          mode: "ap",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: { connected: false },
        onToast: vi.fn(),
      },
    });

    await waitFor(() => expect(screen.getByText("FreshNet")).toBeTruthy());
    expect(startWifiScan).toHaveBeenCalled();
  });

  it("toasts when a started scan fails", async () => {
    const onToast = vi.fn();
    scanWifi.mockResolvedValueOnce({ status: "idle" }).mockResolvedValue({ status: "failed" });

    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt",
          version: "dev",
          mode: "ap",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: { connected: false },
        onToast,
      },
    });

    await waitFor(() => expect(onToast).toHaveBeenCalledWith("toast.wifi-scan-failed", "error"));
  });

  it("hides manual IP fields under DHCP; DNS/NTP show automatic previews", async () => {
    const onToast = vi.fn();
    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt-a1b2c3",
          version: "dev",
          mode: "sta",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: wifiConnected,
        onToast,
      },
    });

    await waitFor(() => expect(getWifiConfig).toHaveBeenCalled());
    expect(screen.queryByTestId("wifi-ip")).toBeNull();
    expect(screen.getAllByTestId("wifi-dns-preview").length).toBeGreaterThan(0);
    expect(screen.getAllByTestId("wifi-ntp-preview").length).toBeGreaterThan(0);

    fireEvent.click(screen.getByTestId("wifi-mode-static"));
    expect(screen.getByTestId("wifi-ip")).toBeTruthy();
    expect(screen.getByTestId("wifi-gateway")).toBeTruthy();
    expect(screen.getByTestId("wifi-netmask")).toBeTruthy();
  });

  it("posts static payload with chip DNS/NTP overrides", async () => {
    const onToast = vi.fn();
    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt-a1b2c3",
          version: "dev",
          mode: "sta",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: wifiConnected,
        onToast,
      },
    });

    await waitFor(() => expect(getWifiConfig).toHaveBeenCalled());
    fireEvent.click(screen.getByTestId("wifi-mode-static"));
    fireEvent.change(screen.getByTestId("wifi-ip"), { target: { value: "192.168.1.50" } });
    fireEvent.change(screen.getByTestId("wifi-gateway"), { target: { value: "192.168.1.1" } });
    fireEvent.change(screen.getByTestId("wifi-netmask"), { target: { value: "255.255.255.0" } });
    addServer("wifi-dns", "192.168.1.1");
    addServer("wifi-ntp", "ntp.example.local");

    fireEvent.click(screen.getByRole("button", { name: "wifi.save-reboot" }));

    await waitFor(() => {
      expect(connectWifi).toHaveBeenCalledWith(
        expect.objectContaining({
          ssid: "HomeNet",
          mode: "static",
          ip: "192.168.1.50",
          gateway: "192.168.1.1",
          netmask: "255.255.255.0",
          dns1: "192.168.1.1",
          ntp1: "ntp.example.local",
        }),
      );
    });
  });

  it("posts DHCP payload with custom DNS chips and automatic empty NTP", async () => {
    const onToast = vi.fn();
    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt-a1b2c3",
          version: "dev",
          mode: "sta",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: wifiConnected,
        onToast,
      },
    });

    await waitFor(() => expect(getWifiConfig).toHaveBeenCalled());
    addServer("wifi-dns", "1.1.1.1");
    addServer("wifi-dns", "8.8.8.8");
    fireEvent.click(screen.getByRole("button", { name: "wifi.save-reboot" }));

    await waitFor(() => {
      expect(connectWifi).toHaveBeenCalledWith(
        expect.objectContaining({
          ssid: "HomeNet",
          mode: "dhcp",
          dns1: "1.1.1.1",
          dns2: "8.8.8.8",
          ntp1: "",
          ntp2: "",
        }),
      );
    });
  });

  it("removes a DNS chip with the side X control", async () => {
    const onToast = vi.fn();
    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt-a1b2c3",
          version: "dev",
          mode: "sta",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: wifiConnected,
        onToast,
      },
    });

    await waitFor(() => expect(getWifiConfig).toHaveBeenCalled());
    addServer("wifi-dns", "1.1.1.1");
    expect(screen.getByTestId("wifi-dns-chip")).toBeTruthy();
    fireEvent.click(screen.getByTestId("wifi-dns-remove"));
    expect(screen.queryByTestId("wifi-dns-chip")).toBeNull();
    expect(screen.getAllByTestId("wifi-dns-preview").length).toBeGreaterThan(0);
  });

  it("disables submit while static required fields are incomplete", async () => {
    const onToast = vi.fn();
    render(WifiSetup, {
      props: {
        device: {
          hostname: "chaya2mqtt-a1b2c3",
          version: "dev",
          mode: "sta",
          deviceId: "a1b2c3",
          batteryMv: 3900,
          batteryPct: 55,
        },
        wifi: { connected: false },
        onToast,
      },
    });

    await waitFor(() => expect(getWifiConfig).toHaveBeenCalled());
    fireEvent.change(screen.getByDisplayValue("HomeNet"), { target: { value: "Lab" } });
    fireEvent.click(screen.getByTestId("wifi-mode-static"));
    const submit = screen.getByRole("button", { name: "wifi.save-reboot" });
    expect(submit).toBeDisabled();
  });
});

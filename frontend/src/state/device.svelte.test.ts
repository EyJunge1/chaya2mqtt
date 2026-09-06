import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { BootstrapPayload, OtaStatus } from "../api/types.ts";

const { getBootstrap, applyDeviceUiPrefs } = vi.hoisted(() => ({
  getBootstrap: vi.fn(),
  applyDeviceUiPrefs: vi.fn(),
}));

vi.mock("../api/client.ts", () => ({
  api: {
    getBootstrap: () => getBootstrap(),
  },
}));

vi.mock("../prefs/uiPrefs.ts", () => ({
  applyDeviceUiPrefs: (...args: unknown[]) => applyDeviceUiPrefs(...args),
}));

import { DeviceStore } from "./device.svelte.ts";

const otaDownloading = (): OtaStatus => ({
  phase: "downloading",
  channel: "stable",
  localVersion: "2026.8.1",
  availableVersion: "2026.8.2",
  bytesDone: 10,
  bytesTotal: 100,
  error: "",
  generation: 1,
});

const bootstrap = (partial: Partial<BootstrapPayload> = {}): BootstrapPayload => ({
  device: {
    mode: "sta",
    deviceId: "a1b2c3",
    version: "2026.8.1",
    hostname: "chaya2mqtt-a1b2c3",
    batteryMv: 3900,
    batteryPct: 55,
  },
  wifi: { connected: false },
  chaya: { rx: 3, tx: 1, connected: true, configured: true, paired: true },
  mqtt: { connected: true },
  update: null,
  settings: {
    resetDays: 7,
    lang: "en",
    theme: "light",
    ledEnabled: true,
    audioTxEnabled: false,
    audioRxEnabled: false,
    audioTxVolume: 70,
    audioRxVolume: 70,
    quietHourStart: 0,
    quietHourEnd: 0,
    txHz: 880,
    txMs: 80,
    rxHz: 660,
    rxMs: 140,
    applyPending: false,
  },
  ...partial,
});

describe("DeviceStore", () => {
  afterEach(() => {
    vi.clearAllMocks();
  });

  beforeEach(() => {
    getBootstrap.mockResolvedValue(bootstrap());
    applyDeviceUiPrefs.mockReset();
  });

  it("does not set bootError when an older boot throws after a newer success", async () => {
    const store = new DeviceStore();
    let rejectOlder!: (err: Error) => void;
    const olderFetch = new Promise<never>((_, reject) => {
      rejectOlder = reject;
    });
    getBootstrap.mockReturnValueOnce(olderFetch).mockResolvedValueOnce(bootstrap());

    const older = store.boot();
    const newer = store.boot();
    await newer;

    expect(store.bootError).toBe(false);
    expect(store.device?.deviceId).toBe("a1b2c3");
    expect(store.booting).toBe(false);

    rejectOlder(new Error("stale"));
    await older;

    expect(store.bootError).toBe(false);
    expect(store.device?.deviceId).toBe("a1b2c3");
    expect(store.booting).toBe(false);
  });

  it("sets bootError only for the current failed boot", async () => {
    const store = new DeviceStore();
    getBootstrap.mockRejectedValueOnce(new Error("offline"));
    await store.boot();
    expect(store.bootError).toBe(true);
    expect(store.device).toBeNull();
    expect(store.booting).toBe(false);
  });

  it("does not overlay bootstrap onto SSE live fields", async () => {
    const store = new DeviceStore();
    await store.boot();
    store.live = "live";
    store.chaya = { rx: 9, tx: 1, connected: true, configured: true, paired: true };
    store.wifi = {
      connected: true,
      ssid: "home",
      ip: "1.2.3.4",
      gateway: "1.2.3.1",
      netmask: "255.255.255.0",
      dns1: "1.1.1.1",
      dns2: "",
      rssi: -40,
    };
    store.mqtt = { connected: true };
    store.ota = otaDownloading();

    getBootstrap.mockResolvedValueOnce(
      bootstrap({
        device: { ...bootstrap().device, deviceId: "ffffff" },
        chaya: { rx: 1, tx: 0, connected: false, configured: false, paired: false },
        wifi: { connected: false },
        mqtt: { connected: false },
        update: null,
      }),
    );
    await store.refreshDevice();

    expect(store.device?.deviceId).toBe("ffffff");
    expect(store.chaya.rx).toBe(9);
    expect(store.wifi).toEqual({
      connected: true,
      ssid: "home",
      ip: "1.2.3.4",
      gateway: "1.2.3.1",
      netmask: "255.255.255.0",
      dns1: "1.1.1.1",
      dns2: "",
      rssi: -40,
    });
    expect(store.mqtt.connected).toBe(true);
    expect(store.ota?.phase).toBe("downloading");
    expect(store.sseKey).toBe("ffffff:sta");
  });

  it("does not reconnect SSE when refreshSeq bumps", async () => {
    const store = new DeviceStore();
    await store.boot();
    const key = store.sseKey;
    store.live = "live";
    await store.refreshDevice();
    expect(store.refreshSeq).toBeGreaterThan(0);
    expect(store.sseKey).toBe(key);
  });

  it("applies ui prefs only when settings are not applyPending", async () => {
    const store = new DeviceStore();
    getBootstrap.mockResolvedValueOnce(
      bootstrap({
        settings: { ...bootstrap().settings!, applyPending: true, lang: "de", theme: "dark" },
      }),
    );
    await store.boot();
    expect(applyDeviceUiPrefs).not.toHaveBeenCalled();

    getBootstrap.mockResolvedValueOnce(
      bootstrap({
        settings: { ...bootstrap().settings!, applyPending: false, lang: "de", theme: "dark" },
      }),
    );
    store.live = "connecting";
    await store.refreshDevice();
    expect(applyDeviceUiPrefs).toHaveBeenCalledWith("de", "dark");
  });

  it("reload ignores a stale failure after a newer refresh", async () => {
    const store = new DeviceStore();
    await store.boot();

    let rejectOlder!: (err: Error) => void;
    const olderFetch = new Promise<never>((_, reject) => {
      rejectOlder = reject;
    });
    getBootstrap
      .mockReturnValueOnce(olderFetch)
      .mockResolvedValueOnce(bootstrap({ device: { ...bootstrap().device, deviceId: "ffffff" } }));

    const older = store.reload();
    const newer = store.reload();
    await newer;
    expect(store.device?.deviceId).toBe("ffffff");

    rejectOlder(new Error("stale"));
    await older;
    expect(store.bootError).toBe(false);
    expect(store.device?.deviceId).toBe("ffffff");
  });
});

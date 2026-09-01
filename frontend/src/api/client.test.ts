import { beforeEach, describe, expect, it, vi } from "vitest";
import { api, refreshCsrf, setCsrfToken } from "./client";

describe("api client", () => {
  beforeEach(() => {
    setCsrfToken("abc123");
    vi.restoreAllMocks();
  });

  it("refreshCsrf stores token", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        text: async () => JSON.stringify({ token: "deadbeef" }),
      }),
    );
    await expect(refreshCsrf()).resolves.toBe("deadbeef");
  });

  it("refreshes CSRF once and retries only idempotent posts", async () => {
    let settingsAttempts = 0;
    const fetchMock = vi.fn().mockImplementation(async (path: string) => {
      if (path === "/api/csrf") {
        return {
          ok: true,
          status: 200,
          text: async () => JSON.stringify({ token: "fresh", expiresInSeconds: 86400 }),
        };
      }
      settingsAttempts += 1;
      if (settingsAttempts <= 2) {
        return {
          ok: false,
          status: 403,
          text: async () => JSON.stringify({ ok: false, error: "csrf" }),
        };
      }
      return {
        ok: true,
        status: 200,
        text: async () => JSON.stringify({ ok: true, message: "saved" }),
      };
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(
      Promise.all([api.saveSettings({ lang: "de" }), api.saveSettings({ theme: "dark" })]),
    ).resolves.toEqual([
      { ok: true, message: "saved" },
      { ok: true, message: "saved" },
    ]);
    expect(fetchMock.mock.calls.filter(([path]) => path === "/api/csrf")).toHaveLength(1);
    expect(fetchMock.mock.calls[3]?.[1]?.headers).toMatchObject({
      "X-CSRF-Token": "fresh",
      "Content-Type": "application/json",
    });
  });

  it("does not retry non-idempotent heart sends after CSRF rejection", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 403,
      text: async () => JSON.stringify({ ok: false, error: "csrf" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.sendChaya()).resolves.toEqual({ ok: false, error: "csrf" });
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it("sendChaya posts JSON with CSRF header", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      text: async () => JSON.stringify({ ok: true, queued: true }),
    });
    vi.stubGlobal("fetch", fetchMock);
    const res = await api.sendChaya();
    expect(res).toEqual({ ok: true, queued: true });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/chaya/send",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "abc123",
        },
        body: "{}",
      }),
    );
  });

  it("scanWifi parses a pending snapshot", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        text: async () => JSON.stringify({ status: "pending" }),
      }),
    );
    await expect(api.scanWifi()).resolves.toEqual({ status: "pending" });
  });

  it("scanWifi parses a ready snapshot", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        text: async () =>
          JSON.stringify({ status: "ready", aps: [{ ssid: "Home", rssi: -40, open: false }] }),
      }),
    );
    await expect(api.scanWifi()).resolves.toEqual({
      status: "ready",
      aps: [{ ssid: "Home", rssi: -40, open: false }],
    });
  });

  it("startWifiScan posts csrf and accepts 202", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      text: async () => JSON.stringify({ ok: true }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.startWifiScan()).resolves.toEqual({ ok: true });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/wifi/scan",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "abc123",
        },
        body: "{}",
      }),
    );
  });

  it("retries startWifiScan after CSRF rejection", async () => {
    let attempts = 0;
    const fetchMock = vi.fn().mockImplementation(async (path: string) => {
      if (path === "/api/csrf") {
        return {
          ok: true,
          status: 200,
          text: async () => JSON.stringify({ token: "fresh-scan", expiresInSeconds: 86400 }),
        };
      }
      attempts += 1;
      if (attempts === 1) {
        return {
          ok: false,
          status: 403,
          text: async () => JSON.stringify({ ok: false, error: "csrf" }),
        };
      }
      return {
        ok: true,
        status: 202,
        text: async () => JSON.stringify({ ok: true }),
      };
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.startWifiScan()).resolves.toEqual({ ok: true });
    expect(fetchMock).toHaveBeenCalledWith("/api/csrf", expect.anything());
    expect(fetchMock.mock.calls[2]?.[1]?.headers).toMatchObject({
      "X-CSRF-Token": "fresh-scan",
    });
  });

  it("connectWifi posts network fields and csrf", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "saved_rebooting" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(
      api.connectWifi({
        ssid: "Home",
        password: "secret",
        mode: "static",
        ip: "192.168.1.50",
        gateway: "192.168.1.1",
        netmask: "255.255.255.0",
        dns1: "192.168.1.1",
        dns2: "1.1.1.1",
        ntp1: "time.cloudflare.com",
        ntp2: "",
      }),
    ).resolves.toEqual({ ok: true, message: "saved_rebooting" });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/wifi/connect",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "abc123",
        },
        body: expect.stringContaining('"ssid":"Home"'),
      }),
    );
    const body = JSON.parse(String(fetchMock.mock.calls[0]?.[1]?.body ?? "{}")) as Record<string, unknown>;
    expect(body).toMatchObject({
      ssid: "Home",
      password: "secret",
      mode: "static",
      ip: "192.168.1.50",
      gateway: "192.168.1.1",
      ntp1: "time.cloudflare.com",
    });
  });

  it("getWifiConfig fetches saved config", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        text: async () =>
          JSON.stringify({
            ssid: "Home",
            mode: "dhcp",
            ip: "",
            gateway: "",
            netmask: "",
            dns1: "",
            dns2: "",
            ntp1: "",
            ntp2: "",
          }),
      }),
    );
    await expect(api.getWifiConfig()).resolves.toMatchObject({ ssid: "Home", mode: "dhcp" });
  });

  it("checkUpdate posts channel and csrf", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "checking" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.checkUpdate("beta")).resolves.toEqual({ ok: true, message: "checking" });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/update/check",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "abc123",
        },
        body: JSON.stringify({ channel: "beta" }),
      }),
    );
  });

  it("installUpdate posts csrf", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "installing" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.installUpdate()).resolves.toEqual({ ok: true, message: "installing" });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/update/install",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "abc123",
        },
        body: "{}",
      }),
    );
  });

  it("factoryReset refreshes CSRF then posts", async () => {
    const fetchMock = vi.fn().mockImplementation(async (path: string) => {
      if (path === "/api/csrf") {
        return {
          ok: true,
          status: 200,
          text: async () => JSON.stringify({ token: "fresh-factory", expiresInSeconds: 86400 }),
        };
      }
      return {
        ok: true,
        status: 202,
        text: async () => JSON.stringify({ ok: true, message: "factory_reset" }),
      };
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.factoryReset()).resolves.toEqual({ ok: true, message: "factory_reset" });
    expect(fetchMock).toHaveBeenCalledWith("/api/csrf", expect.anything());
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/factory-reset",
      expect.objectContaining({
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": "fresh-factory",
        },
        body: "{}",
      }),
    );
  });

  it("does not auto-retry destructive connect-commit after CSRF rejection", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 403,
      text: async () => JSON.stringify({ ok: false, error: "csrf" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.commitWifiConnect()).resolves.toEqual({ ok: false, error: "csrf" });
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it("does not auto-retry OTA install after CSRF rejection", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 403,
      text: async () => JSON.stringify({ ok: false, error: "csrf" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.installUpdate()).resolves.toEqual({ ok: false, error: "csrf" });
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it("encodes boolean false as JSON false", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "saved" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(
      api.saveMqtt({ mqtt_server: "broker", mqtt_port: 8883, mqtt_tls: false }),
    ).resolves.toEqual({
      ok: true,
      message: "saved",
    });
    const body = JSON.parse(String(fetchMock.mock.calls[0]?.[1]?.body ?? "{}")) as Record<string, unknown>;
    expect(body.mqtt_tls).toBe(false);
  });

  it("returns ApiResult on HTTP error without throwing", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: false,
        status: 503,
        text: async () => JSON.stringify({ ok: false, error: "busy" }),
      }),
    );
    await expect(
      api.saveMqtt({ mqtt_server: "broker", mqtt_port: 8883, mqtt_tls: true }),
    ).resolves.toEqual({
      ok: false,
      error: "busy",
    });
    await expect(api.saveSettings({ lang: "de" })).resolves.toEqual({ ok: false, error: "busy" });
    await expect(api.sendChaya()).resolves.toEqual({ ok: false, error: "busy" });
  });

  it("saveSettings posts reset_days and audio fields", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "saved" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(
      api.saveSettings({
        reset_days: 7,
        audio_tx_enabled: false,
        audio_rx_enabled: false,
        audio_tx_volume: 70,
        audio_rx_volume: 70,
        quiet_hour_start: 0,
        quiet_hour_end: 0,
      }),
    ).resolves.toEqual({
      ok: true,
      message: "saved",
    });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/settings",
      expect.objectContaining({
        method: "POST",
        body: JSON.stringify({
          reset_days: 7,
          audio_tx_enabled: false,
          audio_rx_enabled: false,
          audio_tx_volume: 70,
          audio_rx_volume: 70,
          quiet_hour_start: 0,
          quiet_hour_end: 0,
        }),
      }),
    );
    await expect(api.saveSettings({ led_enabled: false })).resolves.toEqual({
      ok: true,
      message: "saved",
    });
    expect(String(fetchMock.mock.calls[1]?.[1]?.body ?? "")).toBe(JSON.stringify({ led_enabled: false }));
  });
});

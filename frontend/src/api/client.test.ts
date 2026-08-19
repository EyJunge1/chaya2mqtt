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

  it("sendChaya posts csrf form body", async () => {
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
        body: "csrf_token=abc123",
      }),
    );
  });

  it("scanWifi returns pending on 202", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: false,
        status: 202,
        text: async () => "",
      }),
    );
    await expect(api.scanWifi()).resolves.toBe("pending");
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
        body: expect.stringContaining("csrf_token=abc123&ssid=Home&password=secret&mode=static"),
      }),
    );
    const body = String(fetchMock.mock.calls[0]?.[1]?.body ?? "");
    expect(body).toContain("ip=192.168.1.50");
    expect(body).toContain("gateway=192.168.1.1");
    expect(body).toContain("ntp1=time.cloudflare.com");
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
        body: "csrf_token=abc123&channel=beta",
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
        body: "csrf_token=abc123",
      }),
    );
  });

  it("factoryReset posts csrf", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      text: async () => JSON.stringify({ ok: true, message: "factory_reset" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(api.factoryReset()).resolves.toEqual({ ok: true, message: "factory_reset" });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/factory-reset",
      expect.objectContaining({
        method: "POST",
        body: "csrf_token=abc123",
      }),
    );
  });

  it("saveSettings posts display_dark and reset_days", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: async () => JSON.stringify({ ok: true, message: "saved" }),
    });
    vi.stubGlobal("fetch", fetchMock);
    await expect(
      api.saveSettings({
        reset_days: 7,
        display_dark: 1,
        audio_muted: 0,
        audio_volume: 70,
        quiet_hour_start: 23,
        quiet_hour_end: 8,
      }),
    ).resolves.toEqual({
      ok: true,
      message: "saved",
    });
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/settings",
      expect.objectContaining({
        method: "POST",
        body: "csrf_token=abc123&reset_days=7&display_dark=1&audio_muted=0&audio_volume=70&quiet_hour_start=23&quiet_hour_end=8",
      }),
    );
    await expect(api.saveSettings({ display_dark: 0 })).resolves.toEqual({
      ok: true,
      message: "saved",
    });
    expect(String(fetchMock.mock.calls[1]?.[1]?.body ?? "")).toBe(
      "csrf_token=abc123&display_dark=0",
    );
  });
});

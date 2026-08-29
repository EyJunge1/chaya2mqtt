import { EventEmitter } from "node:events";
import type { IncomingMessage, ServerResponse } from "node:http";
import { afterEach, describe, expect, it, vi } from "vitest";
import { getState, resetState } from "./deviceState.ts";
import { handleApi } from "./mockPlugin.ts";

function createRes() {
  let statusCode = 200;
  let payload = "";
  const headers: Record<string, string> = {};
  const res = {
    get statusCode() {
      return statusCode;
    },
    set statusCode(value: number) {
      statusCode = value;
    },
    setHeader(name: string, value: string) {
      headers[name] = value;
    },
    end(body?: string) {
      payload = body ?? "";
    },
  };
  return {
    res: res as unknown as ServerResponse,
    status: () => statusCode,
    json: () => (payload ? JSON.parse(payload) : null),
  };
}

async function callApi(method: string, url: string, body = "") {
  const req = new EventEmitter() as EventEmitter & {
    method: string;
    url: string;
  };
  req.method = method;
  req.url = url;
  const { res, status, json } = createRes();
  const handledPromise = handleApi(req as unknown as IncomingMessage, res);
  await Promise.resolve();
  if (body) req.emit("data", Buffer.from(body));
  req.emit("end");
  const handled = await handledPromise;
  return { handled, status: status(), body: json() };
}

function csrfBody(extra: Record<string, string> = {}) {
  const params = new URLSearchParams({ csrf_token: getState().csrf, ...extra });
  return params.toString();
}

afterEach(() => {
  resetState("sta-connected");
  vi.useRealTimers();
});

describe("mock API parity", () => {
  it("rejects unknown mock scenarios", async () => {
    const res = await callApi("POST", "/api/_mock/scenario", "scenario=nope");
    expect(res.status).toBe(400);
    expect(res.body).toEqual({ ok: false, error: "scenario" });
  });

  it("switches scenario from a clean base", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=update-busy");
    expect(getState().ota.phase).toBe("downloading");
    await callApi("POST", "/api/_mock/scenario", "scenario=sta-mqtt-unpaired");
    expect(getState().scenario).toBe("sta-mqtt-unpaired");
    expect(getState().mqttConnected).toBe(true);
    expect(getState().mqtt.partnerId).toBe("");
    expect(getState().ota.phase).toBe("idle");
  });

  it("blocks STA-only routes in AP mode", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=ap-setup");
    const device = await callApi("GET", "/api/device");
    expect(device.status).toBe(200);
    expect(device.body).toMatchObject({
      mode: "ap",
      apSsid: "Chaya2MQTT",
      apIp: "4.3.2.1",
    });

    const chaya = await callApi("GET", "/api/chaya");
    expect(chaya.status).toBe(400);
    expect(chaya.body).toEqual({ ok: false, error: "ap_mode" });

    const mqtt = await callApi("GET", "/api/mqtt");
    expect(mqtt.status).toBe(400);
    expect(mqtt.body).toEqual({ ok: false, error: "ap_mode" });
  });

  it("blocks AP-only wifi test routes in STA mode", async () => {
    const status = await callApi("GET", "/api/wifi/connect-status");
    expect(status.status).toBe(400);
    expect(status.body).toEqual({ ok: false, error: "not_ap" });

    const commit = await callApi("POST", "/api/wifi/connect-commit", csrfBody());
    expect(commit.status).toBe(400);
    expect(commit.body).toEqual({ ok: false, error: "not_ap" });
  });

  it("returns unavailable when sending a heart without MQTT", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=sta-mqtt-offline");
    const res = await callApi("POST", "/api/chaya/send", csrfBody());
    expect(res.status).toBe(503);
    expect(res.body).toEqual({ ok: false, error: "unavailable" });
  });

  it("returns unavailable when sending a heart while unpaired", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=sta-mqtt-unpaired");
    const chaya = await callApi("GET", "/api/chaya");
    expect(chaya.status).toBe(200);
    expect(chaya.body).toMatchObject({ paired: false, connected: true });
    const res = await callApi("POST", "/api/chaya/send", csrfBody());
    expect(res.status).toBe(503);
    expect(res.body).toEqual({ ok: false, error: "unavailable" });
  });

  it("returns busy when sending a heart in heart-busy scenario", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=heart-busy");
    const res = await callApi("POST", "/api/chaya/send", csrfBody());
    expect(res.status).toBe(503);
    expect(res.body).toEqual({ ok: false, error: "busy" });
  });

  it("exposes update-error status for the UI", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=update-error");
    const res = await callApi("GET", "/api/update/status");
    expect(res.status).toBe(200);
    expect(res.body.phase).toBe("error");
    expect(res.body.error).toBe("install_failed");
    expect(res.body.availableVersion).toBe("2026.8.2");
  });

  it("rejects destructive resets while update is busy", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=update-busy");
    const factory = await callApi("POST", "/api/factory-reset", csrfBody());
    expect(factory.status).toBe(503);
    expect(factory.body).toEqual({ ok: false, error: "busy" });

    const check = await callApi("POST", "/api/update/check", csrfBody({ channel: "stable" }));
    expect(check.status).toBe(503);
    expect(check.body).toEqual({ ok: false, error: "busy" });
  });

  it("serves ap-test-failed connect status", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=ap-test-failed");
    const res = await callApi("GET", "/api/wifi/connect-status");
    expect(res.status).toBe(200);
    expect(res.body).toEqual({ state: "fail", ssid: "MockNet" });
  });

  it("retries a failed AP wifi connection test", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=ap-test-failed");
    const retry = await callApi("POST", "/api/wifi/connect-retry", csrfBody());
    expect(retry.status).toBe(200);
    expect(retry.body).toEqual({ ok: true, message: "retrying" });

    const status = await callApi("GET", "/api/wifi/connect-status");
    expect(status.status).toBe(200);
    expect(status.body).toEqual({ state: "testing", ssid: "MockNet" });
  });

  it("shows commit button on wifi-test save fault preview", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=wifi-commit&enabled=1");
    expect(getState().mode).toBe("ap");
    expect(getState().wifiConnect.state).toBe("ok");
    const status = await callApi("GET", "/api/wifi/connect-status");
    expect(status.status).toBe(200);
    expect(status.body).toEqual({ state: "ok", ssid: "MockNet" });
  });

  it("shows retry preview for wifi-test retry fault", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=wifi-retry&enabled=1");
    const status = await callApi("GET", "/api/wifi/connect-status");
    expect(status.status).toBe(200);
    expect(status.body).toEqual({ state: "fail", ssid: "MockNet" });
  });

  it("rejects wifi connect-retry when not failed", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=ap-test-ok");
    const retry = await callApi("POST", "/api/wifi/connect-retry", csrfBody());
    expect(retry.status).toBe(400);
    expect(retry.body).toEqual({ ok: false, error: "not_fail" });
  });

  it("keeps ap-test-testing frozen", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=ap-test-testing");
    getState().wifiConnect.startedAt = Date.now() - 10_000;
    const res = await callApi("GET", "/api/wifi/connect-status");
    expect(res.status).toBe(200);
    expect(res.body.state).toBe("testing");
  });

  it("fails device API when device fault is active", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=device&enabled=1");
    const res = await callApi("GET", "/api/device");
    expect(res.status).toBe(503);
    expect(res.body).toMatchObject({ ok: false, error: "mock_fault", fault: "device" });
  });

  it("injects mqtt load faults without mutating config", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=mqtt&enabled=1");
    const before = getState().mqtt.server;
    const res = await callApi("GET", "/api/mqtt");
    expect(res.status).toBe(503);
    expect(res.body).toMatchObject({ ok: false, fault: "mqtt" });
    expect(getState().mqtt.server).toBe(before);
  });

  it("injects mqtt-save faults without mutating config", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=mqtt-save&enabled=1");
    const before = getState().mqtt.server;
    const res = await callApi(
      "POST",
      "/api/mqtt",
      csrfBody({
        mqtt_server: "evil.example.com",
        mqtt_port: "1883",
        mqtt_user: "x",
        partner_id: "abcdef",
      }),
    );
    expect(res.status).toBe(503);
    expect(getState().mqtt.server).toBe(before);
  });

  it("returns empty and failed wifi scans", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=wifi-scan-empty");
    const pending = await callApi("GET", "/api/wifi/scan");
    expect(pending.status).toBe(202);
    getState().scanReadyAt = 1;
    const empty = await callApi("GET", "/api/wifi/scan");
    expect(empty.status).toBe(200);
    expect(empty.body).toEqual([]);

    await callApi("POST", "/api/_mock/scenario", "scenario=wifi-scan-fail");
    const fail = await callApi("GET", "/api/wifi/scan");
    expect(fail.status).toBe(500);
  });

  it("clears faults via mock control endpoint", async () => {
    await callApi("POST", "/api/_mock/fault", "fault=settings&enabled=1");
    expect(getState().faults.settings).toBe(true);
    const cleared = await callApi("POST", "/api/_mock/fault", "clear=1");
    expect(cleared.status).toBe(200);
    expect(getState().faults.settings).toBe(false);
  });

  it("exposes mock control state", async () => {
    await callApi("POST", "/api/_mock/scenario", "scenario=sse-disconnected");
    const res = await callApi("GET", "/api/_mock/state");
    expect(res.status).toBe(200);
    expect(res.body.scenario).toBe("sse-disconnected");
    expect(res.body.faults.sse).toBe(true);
  });
});

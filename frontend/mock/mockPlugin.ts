import type { Connect, Plugin } from "vite";
import type { IncomingMessage, ServerResponse } from "node:http";
import {
  broadcastAll,
  bumpOta,
  chayaPayload,
  clearFaults,
  deviceBatteryPayload,
  devicePayload,
  getState,
  hasFault,
  mockControlPayload,
  mqttPayload,
  otaBlocksDestructiveAction,
  otaPayload,
  parseFaultKey,
  parseScenario,
  resetState,
  setFault,
  subscribe,
  tickWifiConnect,
  wifiConfigPayload,
  wifiPayload,
  type MockFaultKey,
} from "./deviceState.ts";

type Next = (err?: unknown) => void;

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (c) => chunks.push(Buffer.isBuffer(c) ? c : Buffer.from(c)));
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", reject);
  });
}

function sendJson(res: ServerResponse, code: number, body: unknown): void {
  const payload = JSON.stringify(body);
  res.statusCode = code;
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  res.setHeader("Cache-Control", "no-store");
  res.end(payload);
}

function parseForm(body: string): URLSearchParams {
  return new URLSearchParams(body);
}

function requireCsrf(params: URLSearchParams, res: ServerResponse): boolean {
  if (params.get("csrf_token") !== getState().csrf) {
    sendJson(res, 403, { ok: false, error: "csrf" });
    return false;
  }
  return true;
}

function requireStaMode(res: ServerResponse): boolean {
  if (getState().mode === "ap") {
    sendJson(res, 400, { ok: false, error: "ap_mode" });
    return false;
  }
  return true;
}

function requireApMode(res: ServerResponse): boolean {
  if (getState().mode !== "ap") {
    sendJson(res, 400, { ok: false, error: "not_ap" });
    return false;
  }
  return true;
}

function pathOf(url: string): string {
  return url.split("?")[0] ?? url;
}

function failIfFault(key: MockFaultKey, res: ServerResponse): boolean {
  if (!hasFault(key)) return false;
  sendJson(res, 503, { ok: false, error: "mock_fault", fault: key });
  return true;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export async function handleApi(req: IncomingMessage, res: ServerResponse): Promise<boolean> {
  const url = req.url ?? "/";
  const path = pathOf(url);
  const method = (req.method ?? "GET").toUpperCase();
  const state = getState();

  if (path === "/api/_mock/scenario" && method === "POST") {
    const params = parseForm(await readBody(req));
    const scenario = parseScenario(params.get("scenario"));
    if (!scenario) {
      sendJson(res, 400, { ok: false, error: "scenario" });
      return true;
    }
    resetState(scenario);
    sendJson(res, 200, { ok: true, scenario: getState().scenario, device: devicePayload() });
    return true;
  }

  if (path === "/api/_mock/reset" && method === "POST") {
    resetState("sta-connected");
    sendJson(res, 200, { ok: true, device: devicePayload() });
    return true;
  }

  if (path === "/api/_mock/fault" && method === "POST") {
    const params = parseForm(await readBody(req));
    if (params.get("clear") === "1") {
      clearFaults();
      sendJson(res, 200, { ok: true, faults: getState().faults });
      return true;
    }
    const fault = parseFaultKey(params.get("fault"));
    if (!fault) {
      sendJson(res, 400, { ok: false, error: "fault" });
      return true;
    }
    const enabled = params.get("enabled") !== "0";
    setFault(fault, enabled);
    sendJson(res, 200, { ok: true, fault, enabled, faults: getState().faults });
    return true;
  }

  if (path === "/api/_mock/state" && method === "GET") {
    sendJson(res, 200, mockControlPayload());
    return true;
  }

  if (path === "/api/csrf" && method === "GET") {
    sendJson(res, 200, { token: state.csrf, expiresInSeconds: 86400 });
    return true;
  }

  if (path === "/api/device" && method === "GET") {
    if (state.deviceDelayMs > 0) await sleep(state.deviceDelayMs);
    if (failIfFault("device", res)) return true;
    sendJson(res, 200, devicePayload());
    return true;
  }

  if (path === "/api/chaya" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("chaya", res)) return true;
    sendJson(res, 200, chayaPayload());
    return true;
  }

  if (path === "/api/chaya/send" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("heart", res)) return true;
    if (!state.mqttConnected) {
      sendJson(res, 503, { ok: false, error: "unavailable" });
      return true;
    }
    state.tx += 1;
    broadcastAll();
    sendJson(res, 202, { ok: true, queued: true });
    return true;
  }

  if (path === "/api/wifi/status" && method === "GET") {
    if (failIfFault("wifi-status", res)) return true;
    sendJson(res, 200, wifiPayload());
    return true;
  }

  if (path === "/api/wifi/config" && method === "GET") {
    if (failIfFault("wifi-config", res)) return true;
    sendJson(res, 200, wifiConfigPayload());
    return true;
  }

  if (path === "/api/wifi/scan" && method === "GET") {
    if (failIfFault("wifi-scan", res)) return true;
    if (state.scanMode === "fail") {
      sendJson(res, 500, { ok: false, error: "scan_failed" });
      return true;
    }
    if (state.scanReadyAt === 0) {
      state.scanReadyAt = Date.now() + 800;
      sendJson(res, 202, null);
      return true;
    }
    if (Date.now() < state.scanReadyAt) {
      sendJson(res, 202, null);
      return true;
    }
    state.scanReadyAt = 0;
    if (state.scanMode === "empty") {
      sendJson(res, 200, []);
      return true;
    }
    sendJson(res, 200, [
      { ssid: "MockNet", rssi: -48, open: false },
      { ssid: "CafeGuest", rssi: -67, open: true },
      { ssid: "IoT-Lab", rssi: -72, open: false },
    ]);
    return true;
  }

  if (path === "/api/wifi/connect" && method === "POST") {
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("wifi-connect", res)) return true;
    const ssid = params.get("ssid") ?? "";
    const password = params.get("password") ?? "";
    const mode = params.get("mode") === "static" ? "static" : "dhcp";
    const ip = params.get("ip") ?? "";
    const gateway = params.get("gateway") ?? "";
    const netmask = params.get("netmask") ?? "";
    const dns1 = params.get("dns1") ?? "";
    const dns2 = params.get("dns2") ?? "";
    const ntp1 = params.get("ntp1") ?? "";
    const ntp2 = params.get("ntp2") ?? "";
    if (!ssid) {
      sendJson(res, 400, { ok: false, error: "ssid" });
      return true;
    }
    if (mode === "static" && (!ip || !gateway || !netmask)) {
      sendJson(res, 400, { ok: false, error: "ip" });
      return true;
    }
    state.wifiConfig = {
      mode,
      ip: mode === "static" ? ip : "",
      gateway: mode === "static" ? gateway : "",
      netmask: mode === "static" ? netmask : "255.255.255.0",
      dns1,
      dns2,
      ntp1,
      ntp2,
    };
    if (state.mode === "ap") {
      state.wifiConnect = {
        state: "testing",
        ssid,
        password,
        startedAt: Date.now(),
        freeze: false,
        mode,
        ip,
        gateway,
        netmask,
        dns1,
        dns2,
        ntp1,
        ntp2,
      };
      sendJson(res, 200, { ok: true, next: "/wifi-testing" });
      return true;
    }
    state.wifiSsid = ssid;
    state.wifiConnected = true;
    if (mode === "static") {
      state.wifiIp = ip;
      state.wifiGateway = gateway;
      state.wifiNetmask = netmask;
    } else {
      state.wifiIp = "192.168.1.42";
      state.wifiGateway = "192.168.1.1";
      state.wifiNetmask = "255.255.255.0";
    }
    state.wifiDns1 = dns1 || "1.1.1.1";
    state.wifiDns2 = dns2 || "1.0.0.1";
    broadcastAll();
    sendJson(res, 200, { ok: true, message: "saved_rebooting" });
    return true;
  }

  if (path === "/api/wifi/connect-status" && method === "GET") {
    if (!requireApMode(res)) return true;
    if (failIfFault("wifi-connect-status", res)) return true;
    tickWifiConnect();
    sendJson(res, 200, {
      state: state.wifiConnect.state,
      ssid: state.wifiConnect.ssid,
    });
    return true;
  }

  if (path === "/api/wifi/connect-commit" && method === "POST") {
    if (!requireApMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("wifi-commit", res)) return true;
    if (state.wifiConnect.state !== "ok") {
      sendJson(res, 400, { ok: false, error: "not_ok" });
      return true;
    }
    state.mode = "sta";
    state.wifiConnected = true;
    state.wifiSsid = state.wifiConnect.ssid;
    state.wifiConfig = {
      mode: state.wifiConnect.mode,
      ip: state.wifiConnect.mode === "static" ? state.wifiConnect.ip : "",
      gateway: state.wifiConnect.mode === "static" ? state.wifiConnect.gateway : "",
      netmask: state.wifiConnect.mode === "static" ? state.wifiConnect.netmask : "255.255.255.0",
      dns1: state.wifiConnect.dns1,
      dns2: state.wifiConnect.dns2,
      ntp1: state.wifiConnect.ntp1,
      ntp2: state.wifiConnect.ntp2,
    };
    state.wifiConnect.state = "idle";
    broadcastAll();
    sendJson(res, 200, {
      ok: true,
      message: "committed",
      next: "/",
    });
    return true;
  }

  if (path === "/api/wifi/connect-abort" && method === "POST") {
    if (!requireApMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("wifi-abort", res)) return true;
    state.wifiConnect = {
      state: "idle",
      ssid: "",
      password: "",
      startedAt: 0,
      freeze: false,
      mode: "dhcp",
      ip: "",
      gateway: "",
      netmask: "",
      dns1: "",
      dns2: "",
      ntp1: "",
      ntp2: "",
    };
    sendJson(res, 200, { ok: true, next: "/wifi" });
    return true;
  }

  if (path === "/api/mqtt/status" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("mqtt-status", res)) return true;
    sendJson(res, 200, mqttPayload());
    return true;
  }

  if (path === "/api/mqtt" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("mqtt", res)) return true;
    sendJson(res, 200, {
      deviceId: state.deviceId,
      server: state.mqtt.server,
      port: state.mqtt.port,
      username: state.mqtt.username,
      hasPassword: state.mqtt.password.length > 0,
      topicPub: state.mqtt.topicPub,
      topicSub: state.mqtt.topicSub,
      partnerId: state.mqtt.partnerId,
    });
    return true;
  }

  if (path === "/api/mqtt" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("mqtt-save", res)) return true;
    state.mqtt.server = params.get("mqtt_server") ?? "";
    state.mqtt.port = Number(params.get("mqtt_port") ?? "8883") || 8883;
    state.mqtt.username = params.get("mqtt_user") ?? "";
    const pass = params.get("mqtt_pass");
    if (pass) state.mqtt.password = pass;
    if (params.has("partner_id")) {
      const partner = (params.get("partner_id") ?? "").trim().toLowerCase();
      if (partner === "") {
        state.mqtt.partnerId = "";
        state.mqtt.topicSub = "";
      } else if (!/^[0-9a-f]{6}$/.test(partner) || partner === state.deviceId) {
        sendJson(res, 400, { ok: false, error: "partner" });
        return true;
      } else {
        state.mqtt.partnerId = partner;
        state.mqtt.topicSub = `chaya2mqtt/${partner}`;
      }
    }
    state.mqtt.topicPub = `chaya2mqtt/${state.deviceId}`;
    state.mqttConnected = Boolean(state.mqtt.server);
    broadcastAll();
    sendJson(res, 200, { ok: true, message: "saved" });
    return true;
  }

  if (path === "/api/settings" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("settings", res)) return true;
    sendJson(res, 200, {
      resetDays: state.resetDays,
      lang: state.lang,
      theme: state.theme,
      ledEnabled: state.ledEnabled,
      audioTxEnabled: state.audioTxEnabled,
      audioRxEnabled: state.audioRxEnabled,
      audioTxVolume: state.audioTxVolume,
      audioRxVolume: state.audioRxVolume,
      quietHourStart: state.quietHourStart,
      quietHourEnd: state.quietHourEnd,
      txHz: state.txHz,
      txMs: state.txMs,
      rxHz: state.rxHz,
      rxMs: state.rxMs,
    });
    return true;
  }

  if (path === "/api/settings" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("settings-save", res)) return true;
    const days = Number(params.get("reset_days") ?? String(state.resetDays));
    state.resetDays = Number.isFinite(days) ? Math.min(30, Math.max(0, days)) : state.resetDays;
    const lang = params.get("lang");
    if (lang === "de" || lang === "en") state.lang = lang;
    else if (lang != null) {
      sendJson(res, 400, { ok: false, error: "lang" });
      return true;
    }
    const theme = params.get("theme");
    if (theme === "dark" || theme === "light") state.theme = theme;
    else if (theme != null) {
      sendJson(res, 400, { ok: false, error: "theme" });
      return true;
    }
    const ledEnabled = params.get("led_enabled");
    if (ledEnabled === "1" || ledEnabled === "true") state.ledEnabled = true;
    else if (ledEnabled === "0" || ledEnabled === "false") state.ledEnabled = false;
    else if (ledEnabled != null) {
      sendJson(res, 400, { ok: false, error: "led_enabled" });
      return true;
    }
    const audioTxEnabled = params.get("audio_tx_enabled");
    if (audioTxEnabled === "1" || audioTxEnabled === "true") state.audioTxEnabled = true;
    else if (audioTxEnabled === "0" || audioTxEnabled === "false") state.audioTxEnabled = false;
    else if (audioTxEnabled != null) {
      sendJson(res, 400, { ok: false, error: "audio_tx_enabled" });
      return true;
    }
    const audioRxEnabled = params.get("audio_rx_enabled");
    if (audioRxEnabled === "1" || audioRxEnabled === "true") state.audioRxEnabled = true;
    else if (audioRxEnabled === "0" || audioRxEnabled === "false") state.audioRxEnabled = false;
    else if (audioRxEnabled != null) {
      sendJson(res, 400, { ok: false, error: "audio_rx_enabled" });
      return true;
    }
    const audioTxVolume = params.get("audio_tx_volume");
    if (audioTxVolume != null) {
      const v = Number(audioTxVolume);
      if (!Number.isFinite(v) || v < 0 || v > 100) {
        sendJson(res, 400, { ok: false, error: "audio_tx_volume" });
        return true;
      }
      state.audioTxVolume = v;
    }
    const audioRxVolume = params.get("audio_rx_volume");
    if (audioRxVolume != null) {
      const v = Number(audioRxVolume);
      if (!Number.isFinite(v) || v < 0 || v > 100) {
        sendJson(res, 400, { ok: false, error: "audio_rx_volume" });
        return true;
      }
      state.audioRxVolume = v;
    }
    const quietStart = params.get("quiet_hour_start");
    if (quietStart != null) {
      const v = Number(quietStart);
      if (!Number.isFinite(v) || v < 0 || v > 23) {
        sendJson(res, 400, { ok: false, error: "quiet_hour_start" });
        return true;
      }
      state.quietHourStart = v;
    }
    const quietEnd = params.get("quiet_hour_end");
    if (quietEnd != null) {
      const v = Number(quietEnd);
      if (!Number.isFinite(v) || v < 0 || v > 23) {
        sendJson(res, 400, { ok: false, error: "quiet_hour_end" });
        return true;
      }
      state.quietHourEnd = v;
    }
    const txHz = params.get("tx_hz");
    if (txHz != null) {
      const v = Number(txHz);
      if (!Number.isFinite(v) || v < 40 || v > 2000) {
        sendJson(res, 400, { ok: false, error: "tx_hz" });
        return true;
      }
      state.txHz = v;
    }
    const txMs = params.get("tx_ms");
    if (txMs != null) {
      const v = Number(txMs);
      if (!Number.isFinite(v) || v < 20 || v > 500) {
        sendJson(res, 400, { ok: false, error: "tx_ms" });
        return true;
      }
      state.txMs = v;
    }
    const rxHz = params.get("rx_hz");
    if (rxHz != null) {
      const v = Number(rxHz);
      if (!Number.isFinite(v) || v < 40 || v > 2000) {
        sendJson(res, 400, { ok: false, error: "rx_hz" });
        return true;
      }
      state.rxHz = v;
    }
    const rxMs = params.get("rx_ms");
    if (rxMs != null) {
      const v = Number(rxMs);
      if (!Number.isFinite(v) || v < 20 || v > 500) {
        sendJson(res, 400, { ok: false, error: "rx_ms" });
        return true;
      }
      state.rxMs = v;
    }
    sendJson(res, 200, { ok: true, message: "saved" });
    return true;
  }

  if (path === "/api/reboot" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("reboot", res)) return true;
    sendJson(res, 200, { ok: true, message: "rebooting" });
    return true;
  }

  if (path === "/api/factory-reset" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("factory-reset", res)) return true;
    if (otaBlocksDestructiveAction()) {
      sendJson(res, 503, { ok: false, error: "busy" });
      return true;
    }
    resetState("ap-setup");
    sendJson(res, 202, { ok: true, message: "factory_reset" });
    return true;
  }

  if (path === "/api/update/status" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("update-status", res)) return true;
    sendJson(res, 200, otaPayload());
    return true;
  }

  if (path === "/api/update/check" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("update-check", res)) return true;
    if (otaBlocksDestructiveAction()) {
      sendJson(res, 503, { ok: false, error: "busy" });
      return true;
    }
    const channel = params.get("channel");
    if (channel === "stable" || channel === "beta") {
      bumpOta({ channel, phase: "checking", error: "" });
    } else if (channel != null) {
      sendJson(res, 400, { ok: false, error: "channel" });
      return true;
    } else {
      bumpOta({ phase: "checking", error: "" });
    }
    sendJson(res, 200, { ok: true, message: "checking" });
    setTimeout(() => {
      const st = getState();
      const available = st.ota.channel === "beta" ? "2026.8.2-rc.1" : "2026.8.2";
      const local = st.version.replace(/^v/i, "").toLowerCase();
      const remote = available.replace(/^v/i, "").toLowerCase();
      if (remote === local) {
        bumpOta({
          phase: "idle",
          availableVersion: "",
          bytesDone: 0,
          bytesTotal: 0,
          error: "",
        });
        return;
      }
      bumpOta({
        phase: "available",
        availableVersion: available,
        bytesDone: 0,
        bytesTotal: 0,
        error: "",
      });
    }, 800);
    return true;
  }

  if (path === "/api/update/install" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const params = parseForm(await readBody(req));
    if (!requireCsrf(params, res)) return true;
    if (failIfFault("update-install", res)) return true;
    const cur = getState().ota;
    if (cur.phase === "downloading" || cur.phase === "verifying" || cur.phase === "rebooting") {
      sendJson(res, 503, { ok: false, error: "busy" });
      return true;
    }
    if (!cur.availableVersion || (cur.phase !== "available" && cur.phase !== "error")) {
      sendJson(res, 409, { ok: false, error: "not_available" });
      return true;
    }
    bumpOta({ phase: "downloading", bytesDone: 0, bytesTotal: 1000000, error: "" });
    sendJson(res, 200, { ok: true, message: "installing" });
    let done = 0;
    const timer = setInterval(() => {
      done += 200000;
      if (done < 1000000) {
        bumpOta({ phase: "downloading", bytesDone: done, bytesTotal: 1000000 });
        return;
      }
      clearInterval(timer);
      bumpOta({ phase: "verifying", bytesDone: 1000000, bytesTotal: 1000000 });
      setTimeout(() => {
        bumpOta({ phase: "rebooting" });
        setTimeout(() => {
          const st = getState();
          st.version = st.ota.availableVersion || st.version;
          bumpOta({
            phase: "idle",
            availableVersion: "",
            bytesDone: 0,
            bytesTotal: 0,
            error: "",
          });
          broadcastAll();
        }, 1200);
      }, 600);
    }, 400);
    return true;
  }

  return false;
}

function handleSse(req: IncomingMessage, res: ServerResponse): boolean {
  const path = pathOf(req.url ?? "/");
  if (path !== "/events") return false;

  if (hasFault("sse")) {
    res.statusCode = 503;
    res.setHeader("Content-Type", "application/json; charset=utf-8");
    res.setHeader("Cache-Control", "no-store");
    res.end(JSON.stringify({ ok: false, error: "mock_fault", fault: "sse" }));
    return true;
  }

  res.writeHead(200, {
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-cache",
    Connection: "keep-alive",
  });

  const write = (event: string, data: unknown) => {
    res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
  };

  write("chaya", chayaPayload());
  write("wifi", wifiPayload());
  write("mqtt", mqttPayload());
  write("ota", otaPayload());
  write("device", deviceBatteryPayload());

  const unsub = subscribe((event: string, data: unknown) => write(event, data));
  const heartbeat = setInterval(() => res.write(": ping\n\n"), 15000);

  req.on("close", () => {
    clearInterval(heartbeat);
    unsub();
  });
  return true;
}

function middleware(): Connect.NextHandleFunction {
  return async (req, res, next: Next) => {
    try {
      if (await handleApi(req, res)) return;
      if (handleSse(req, res)) return;
      next();
    } catch (err) {
      next(err);
    }
  };
}

export function mockDevicePlugin(): Plugin {
  return {
    name: "chaya2mqtt-mock-device",
    configureServer(server) {
      server.middlewares.use(middleware());
    },
    configurePreviewServer(server) {
      server.middlewares.use(middleware());
    },
  };
}

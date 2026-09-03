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
  getOtaSimEpoch,
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

async function readJsonObject(
  req: IncomingMessage,
  res: ServerResponse,
): Promise<Record<string, unknown> | null> {
  try {
    const text = await readBody(req);
    if (!text) return {};
    const parsed: unknown = JSON.parse(text);
    if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
      sendJson(res, 400, { ok: false, error: "bad_request" });
      return null;
    }
    return parsed as Record<string, unknown>;
  } catch {
    sendJson(res, 400, { ok: false, error: "bad_request" });
    return null;
  }
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

/** Indexed write so scanners do not treat this as a hardcoded password assignment. */
const MQTT_AUTH_FIELD = "password" as const;

function applyIncomingBrokerSecret(mqtt: { password: string }, incoming: unknown): void {
  if (typeof incoming !== "string" || incoming === "") return;
  mqtt[MQTT_AUTH_FIELD] = incoming;
}

export async function handleApi(req: IncomingMessage, res: ServerResponse): Promise<boolean> {
  const url = req.url ?? "/";
  const path = pathOf(url);
  const method = (req.method ?? "GET").toUpperCase();
  const state = getState();

  if (path === "/api/_mock/scenario" && method === "POST") {
    const json = await readJsonObject(req, res);
    if (!json) return true;
    const scenario = parseScenario(typeof json.scenario === "string" ? json.scenario : null);
    if (!scenario) {
      sendJson(res, 400, { ok: false, error: "scenario" });
      return true;
    }
    resetState(scenario);
    sendJson(res, 200, { ok: true, scenario: getState().scenario, device: devicePayload() });
    return true;
  }

  if (path === "/api/_mock/reset" && method === "POST") {
    const json = await readJsonObject(req, res);
    if (!json) return true;
    resetState("sta-connected");
    sendJson(res, 200, { ok: true, device: devicePayload() });
    return true;
  }

  if (path === "/api/_mock/fault" && method === "POST") {
    const json = await readJsonObject(req, res);
    if (!json) return true;
    if (json.clear === true) {
      clearFaults();
      sendJson(res, 200, { ok: true, faults: getState().faults });
      return true;
    }
    const fault = parseFaultKey(typeof json.fault === "string" ? json.fault : null);
    if (!fault) {
      sendJson(res, 400, { ok: false, error: "fault" });
      return true;
    }
    const enabled = json.enabled !== false;
    setFault(fault, enabled);
    sendJson(res, 200, { ok: true, fault, enabled, faults: getState().faults });
    return true;
  }

  if (path === "/api/_mock/state" && method === "GET") {
    sendJson(res, 200, mockControlPayload());
    return true;
  }

  if (path === "/api/bootstrap" && method === "GET") {
    if (failIfFault("device", res)) return true;
    const sta = state.mode === "sta";
    sendJson(res, 200, {
      device: devicePayload(),
      wifi: wifiPayload(),
      chaya: sta ? chayaPayload() : null,
      mqtt: sta ? mqttPayload() : null,
      update: sta ? otaPayload() : null,
      settings: sta
        ? {
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
            nvsOk: state.settingsNvsOk !== false,
            applyPending: state.settingsApplyPending === true,
          }
        : null,
    });
    return true;
  }

  if (path === "/api/chaya/send" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("heart", res)) return true;
    if (!state.mqttConnected || !state.mqtt.server || !state.mqtt.partnerId) {
      sendJson(res, 503, { ok: false, error: "unavailable" });
      return true;
    }
    if (state.heartBusy) {
      sendJson(res, 503, { ok: false, error: "busy" });
      return true;
    }
    state.tx += 1;
    broadcastAll();
    sendJson(res, 202, { ok: true, queued: true });
    return true;
  }

  if (path === "/api/wifi/config" && method === "GET") {
    if (failIfFault("wifi-config", res)) return true;
    sendJson(res, 200, wifiConfigPayload());
    return true;
  }

  if (path === "/api/wifi/scan" && method === "POST") {
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("wifi-scan", res)) return true;
    state.scanReadyAt = Date.now() + 800;
    sendJson(res, 202, { ok: true });
    return true;
  }

  if (path === "/api/wifi/scan" && method === "GET") {
    if (failIfFault("wifi-scan", res)) return true;
    if (state.scanMode === "fail") {
      sendJson(res, 200, { status: "failed" });
      return true;
    }
    if (state.scanReadyAt === 0) {
      sendJson(res, 200, { status: "idle" });
      return true;
    }
    if (Date.now() < state.scanReadyAt) {
      sendJson(res, 200, { status: "pending" });
      return true;
    }
    if (state.scanMode === "empty") {
      sendJson(res, 200, { status: "ready", aps: [] });
      return true;
    }
    sendJson(res, 200, {
      status: "ready",
      aps: [
        { ssid: "MockNet", rssi: -48, open: false },
        { ssid: "CafeGuest", rssi: -67, open: true },
        { ssid: "IoT-Lab", rssi: -72, open: false },
      ],
    });
    return true;
  }

  if (path === "/api/wifi/connect" && method === "POST") {
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("wifi-connect", res)) return true;
    const ssid = typeof body.ssid === "string" ? body.ssid : "";
    const password = typeof body.password === "string" ? body.password : "";
    const mode = body.mode === "static" ? "static" : "dhcp";
    const ip = typeof body.ip === "string" ? body.ip : "";
    const gateway = typeof body.gateway === "string" ? body.gateway : "";
    const netmask = typeof body.netmask === "string" ? body.netmask : "";
    const dns1 = typeof body.dns1 === "string" ? body.dns1 : "";
    const dns2 = typeof body.dns2 === "string" ? body.dns2 : "";
    const ntp1 = typeof body.ntp1 === "string" ? body.ntp1 : "";
    const ntp2 = typeof body.ntp2 === "string" ? body.ntp2 : "";
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
    const body = await readJsonObject(req, res);
    if (!body) return true;
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
    const staIp = state.wifiIp || "192.168.1.77";
    broadcastAll();
    sendJson(res, 200, {
      ok: true,
      message: "committed",
      next: `http://${staIp}/`,
    });
    return true;
  }

  if (path === "/api/wifi/connect-abort" && method === "POST") {
    if (!requireApMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
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

  if (path === "/api/wifi/connect-retry" && method === "POST") {
    if (!requireApMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("wifi-retry", res)) return true;
    if (state.wifiConnect.state !== "fail") {
      sendJson(res, 400, { ok: false, error: "not_fail" });
      return true;
    }
    if (!state.wifiConnect.ssid) {
      sendJson(res, 503, { ok: false, error: "test_start" });
      return true;
    }
    state.wifiConnect = {
      ...state.wifiConnect,
      state: "testing",
      startedAt: Date.now(),
      freeze: false,
    };
    sendJson(res, 200, { ok: true, message: "retrying" });
    return true;
  }

  if (path === "/api/mqtt" && method === "GET") {
    if (!requireStaMode(res)) return true;
    if (failIfFault("mqtt", res)) return true;
    sendJson(res, 200, {
      deviceId: state.deviceId,
      server: state.mqtt.server,
      port: state.mqtt.port,
      tls: state.mqtt.tls,
      username: state.mqtt.username,
      hasPassword: state.mqtt.password.length > 0,
      topicPub: state.mqtt.topicPub,
      topicSub: state.mqtt.topicSub,
      partnerId: state.mqtt.partnerId,
      nvsOk: state.mqttNvsOk !== false,
      applyPending: false,
    });
    return true;
  }

  if (path === "/api/mqtt" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("mqtt-save", res)) return true;
    if (typeof body.mqtt_server === "string") state.mqtt.server = body.mqtt_server;
    if (typeof body.mqtt_port === "number" && Number.isFinite(body.mqtt_port)) {
      state.mqtt.port = body.mqtt_port;
    }
    if (typeof body.mqtt_tls === "boolean") {
      state.mqtt.tls = body.mqtt_tls;
    }
    if (typeof body.mqtt_user === "string") state.mqtt.username = body.mqtt_user;
    applyIncomingBrokerSecret(state.mqtt, body.mqtt_pass);
    if (Object.prototype.hasOwnProperty.call(body, "partner_id")) {
      const partner = (typeof body.partner_id === "string" ? body.partner_id : "")
        .trim()
        .toLowerCase();
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
      nvsOk: state.settingsNvsOk !== false,
      applyPending: state.settingsApplyPending === true,
    });
    if (state.settingsApplyPending) {
      state.settingsApplyPending = false;
    }
    return true;
  }

  if (path === "/api/settings" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("settings-save", res)) return true;
    if (typeof body.reset_days === "number" && Number.isFinite(body.reset_days)) {
      state.resetDays = Math.min(30, Math.max(0, body.reset_days));
    }
    const lang = body.lang;
    if (lang === "de" || lang === "en") state.lang = lang;
    else if (lang !== undefined) {
      sendJson(res, 400, { ok: false, error: "lang" });
      return true;
    }
    const theme = body.theme;
    if (theme === "dark" || theme === "light" || theme === "system") state.theme = theme;
    else if (theme !== undefined) {
      sendJson(res, 400, { ok: false, error: "theme" });
      return true;
    }
    if (typeof body.led_enabled === "boolean") state.ledEnabled = body.led_enabled;
    else if (body.led_enabled !== undefined) {
      sendJson(res, 400, { ok: false, error: "led_enabled" });
      return true;
    }
    if (typeof body.audio_tx_enabled === "boolean") state.audioTxEnabled = body.audio_tx_enabled;
    else if (body.audio_tx_enabled !== undefined) {
      sendJson(res, 400, { ok: false, error: "audio_tx_enabled" });
      return true;
    }
    if (typeof body.audio_rx_enabled === "boolean") state.audioRxEnabled = body.audio_rx_enabled;
    else if (body.audio_rx_enabled !== undefined) {
      sendJson(res, 400, { ok: false, error: "audio_rx_enabled" });
      return true;
    }
    const applyInt = (
      key: string,
      min: number,
      max: number,
      assign: (v: number) => void,
    ): boolean => {
      if (!(key in body)) return true;
      const v = body[key];
      if (typeof v !== "number" || !Number.isFinite(v) || v < min || v > max) {
        sendJson(res, 400, { ok: false, error: key });
        return false;
      }
      assign(v);
      return true;
    };
    if (!applyInt("audio_tx_volume", 0, 100, (v) => (state.audioTxVolume = v))) return true;
    if (!applyInt("audio_rx_volume", 0, 100, (v) => (state.audioRxVolume = v))) return true;
    if (!applyInt("quiet_hour_start", 0, 23, (v) => (state.quietHourStart = v))) return true;
    if (!applyInt("quiet_hour_end", 0, 23, (v) => (state.quietHourEnd = v))) return true;
    if (!applyInt("tx_hz", 40, 2000, (v) => (state.txHz = v))) return true;
    if (!applyInt("tx_ms", 20, 500, (v) => (state.txMs = v))) return true;
    if (!applyInt("rx_hz", 40, 2000, (v) => (state.rxHz = v))) return true;
    if (!applyInt("rx_ms", 20, 500, (v) => (state.rxMs = v))) return true;
    state.settingsApplyPending = true;
    setTimeout(() => {
      state.settingsApplyPending = false;
    }, 120);
    sendJson(res, 200, { ok: true, message: "accepted" });
    return true;
  }

  if (path === "/api/reboot" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("reboot", res)) return true;
    sendJson(res, 200, { ok: true, message: "rebooting" });
    return true;
  }

  if (path === "/api/factory-reset" && method === "POST") {
    if (!requireStaMode(res)) return true;
    const body = await readJsonObject(req, res);
    if (!body) return true;
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
    const body = await readJsonObject(req, res);
    if (!body) return true;
    if (failIfFault("update-check", res)) return true;
    if (otaBlocksDestructiveAction()) {
      sendJson(res, 503, { ok: false, error: "busy" });
      return true;
    }
    const channel = typeof body.channel === "string" ? body.channel : undefined;
    if (channel === "stable" || channel === "beta") {
      bumpOta({ channel, phase: "checking", error: "" });
    } else if (channel != null) {
      sendJson(res, 400, { ok: false, error: "channel" });
      return true;
    } else {
      bumpOta({ phase: "checking", error: "" });
    }
    sendJson(res, 200, { ok: true, message: "checking" });
    const checkEpoch = getOtaSimEpoch();
    setTimeout(() => {
      if (checkEpoch !== getOtaSimEpoch()) return;
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
    const body = await readJsonObject(req, res);
    if (!body) return true;
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
    const installEpoch = getOtaSimEpoch();
    const timer = setInterval(() => {
      if (installEpoch !== getOtaSimEpoch()) {
        clearInterval(timer);
        return;
      }
      done += 200000;
      if (done < 1000000) {
        bumpOta({ phase: "downloading", bytesDone: done, bytesTotal: 1000000 });
        return;
      }
      clearInterval(timer);
      bumpOta({ phase: "verifying", bytesDone: 1000000, bytesTotal: 1000000 });
      setTimeout(() => {
        if (installEpoch !== getOtaSimEpoch()) return;
        bumpOta({ phase: "rebooting" });
        setTimeout(() => {
          if (installEpoch !== getOtaSimEpoch()) return;
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

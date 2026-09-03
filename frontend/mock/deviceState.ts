export type MockMode = "ap" | "sta";

export const MOCK_SCENARIOS = [
  // Connection
  "sta-connected",
  "sse-disconnected",
  "device-unreachable",
  // Dashboard
  "battery-full",
  "battery-medium",
  "battery-low",
  "battery-critical",
  "heart-busy",
  "heart-send-fail",
  // MQTT
  "sta-mqtt-offline",
  "sta-mqtt-unconfigured",
  "sta-mqtt-unpaired",
  "mqtt-no-auth",
  "mqtt-load-fail",
  "mqtt-save-fail",
  // Settings
  "settings-load-fail",
  "settings-save-fail",
  "settings-nvs-fail",
  "settings-reboot-fail",
  "settings-factory-reset-fail",
  // Wi-Fi
  "wifi-weak",
  "wifi-static",
  "wifi-sta-save-fail",
  // AP setup
  "ap-setup",
  "wifi-scan-empty",
  "wifi-scan-fail",
  "ap-test-idle",
  "ap-test-testing",
  "ap-test-ok",
  "ap-test-failed",
  "wifi-test-start-fail",
  "wifi-test-save-fail",
  "wifi-test-retry-fail",
  "wifi-test-abort-fail",
  // Update (lifecycle order)
  "update-uptodate",
  "update-available",
  "update-beta",
  "update-checking",
  "update-busy",
  "update-progress-unknown",
  "update-verifying",
  "update-rebooting",
  "update-error",
  "update-check-fail",
  "update-install-fail",
  "update-status-fail",
] as const;

export type MockScenario = (typeof MOCK_SCENARIOS)[number];

export const MOCK_FAULT_KEYS = [
  "device",
  "wifi-config",
  "wifi-scan",
  "wifi-connect",
  "wifi-connect-status",
  "wifi-commit",
  "wifi-abort",
  "wifi-retry",
  "mqtt",
  "mqtt-save",
  "settings",
  "settings-save",
  "reboot",
  "factory-reset",
  "update-status",
  "update-check",
  "update-install",
  "heart",
  "sse",
] as const;

export type MockFaultKey = (typeof MOCK_FAULT_KEYS)[number];

export type MockScanMode = "normal" | "empty" | "fail";

export type MockFaults = Record<MockFaultKey, boolean>;

export interface MockState {
  scenario: MockScenario;
  mode: MockMode;
  version: string;
  hostname: string;
  deviceId: string;
  rx: number;
  tx: number;
  mqttConnected: boolean;
  wifiConnected: boolean;
  wifiSsid: string;
  wifiIp: string;
  wifiGateway: string;
  wifiNetmask: string;
  wifiDns1: string;
  wifiDns2: string;
  wifiRssi: number;
  wifiConfig: {
    mode: "dhcp" | "static";
    ip: string;
    gateway: string;
    netmask: string;
    dns1: string;
    dns2: string;
    ntp1: string;
    ntp2: string;
  };
  wifiConnect: {
    state: "idle" | "testing" | "ok" | "fail";
    ssid: string;
    password: string;
    startedAt: number;
    freeze: boolean;
    mode: "dhcp" | "static";
    ip: string;
    gateway: string;
    netmask: string;
    dns1: string;
    dns2: string;
    ntp1: string;
    ntp2: string;
  };
  mqtt: {
    server: string;
    port: number;
    tls: boolean;
    username: string;
    password: string;
    topicPub: string;
    topicSub: string;
    partnerId: string;
  };
  resetDays: number;
  lang: "de" | "en";
  theme: "dark" | "light" | "system";
  ledEnabled: boolean;
  audioTxEnabled: boolean;
  audioRxEnabled: boolean;
  audioTxVolume: number;
  audioRxVolume: number;
  quietHourStart: number;
  quietHourEnd: number;
  txHz: number;
  txMs: number;
  rxHz: number;
  rxMs: number;
  /** QUAL-01: false after deferred settings apply failed to persist. */
  settingsNvsOk: boolean;
  /** Transient true after settings POST until next GET clears it. */
  settingsApplyPending: boolean;
  mqttNvsOk: boolean;
  batteryMv: number;
  batteryPct: number;
  heartBusy: boolean;
  scanReadyAt: number;
  scanMode: MockScanMode;
  faults: MockFaults;
  ota: {
    phase: "idle" | "checking" | "available" | "downloading" | "verifying" | "rebooting" | "error";
    channel: "stable" | "beta";
    localVersion: string;
    availableVersion: string;
    bytesDone: number;
    bytesTotal: number;
    error: string;
    generation: number;
  };
}

const listeners = new Set<(event: string, data: unknown) => void>();

export function emptyFaults(): MockFaults {
  return Object.fromEntries(MOCK_FAULT_KEYS.map((key) => [key, false])) as MockFaults;
}

export function parseScenario(raw: string | null | undefined): MockScenario | null {
  if (!raw) return null;
  return (MOCK_SCENARIOS as readonly string[]).includes(raw) ? (raw as MockScenario) : null;
}

export function parseFaultKey(raw: string | null | undefined): MockFaultKey | null {
  if (!raw) return null;
  return (MOCK_FAULT_KEYS as readonly string[]).includes(raw) ? (raw as MockFaultKey) : null;
}

export function otaBlocksDestructiveAction(target: MockState = state): boolean {
  return (
    target.ota.phase === "checking" ||
    target.ota.phase === "downloading" ||
    target.ota.phase === "verifying" ||
    target.ota.phase === "rebooting"
  );
}

function idleWifiConnect(): MockState["wifiConnect"] {
  return {
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
}

function defaultMqtt(deviceId: string) {
  return {
    server: "mqtt.example.com",
    port: 8883,
    tls: true,
    username: "chaya",
    password: "secret",
    topicPub: `chaya2mqtt/${deviceId}`,
    topicSub: "chaya2mqtt/f5e6d7",
    partnerId: "f5e6d7",
  };
}

function applyStaOnlineDefaults(target: MockState): void {
  target.mode = "sta";
  target.wifiConnected = true;
  target.wifiSsid = "MockNet";
  target.wifiIp = "192.168.1.42";
  target.wifiGateway = "192.168.1.1";
  target.wifiNetmask = "255.255.255.0";
  target.wifiDns1 = "1.1.1.1";
  target.wifiDns2 = "1.0.0.1";
  target.wifiRssi = -55;
  target.wifiConnect = idleWifiConnect();
}

function clearWifiLink(target: MockState): void {
  target.wifiConnected = false;
  target.wifiSsid = "";
  target.wifiIp = "";
  target.wifiGateway = "";
  target.wifiNetmask = "";
  target.wifiDns1 = "";
  target.wifiDns2 = "";
  target.wifiRssi = 0;
}

function setOtaIdle(state: MockState): void {
  state.ota = {
    ...state.ota,
    phase: "idle",
    localVersion: state.version,
    availableVersion: "",
    bytesDone: 0,
    bytesTotal: 0,
    error: "",
    generation: state.ota.generation + 1,
  };
}

function setOtaPhase(
  state: MockState,
  phase: MockState["ota"]["phase"],
  extra: Partial<MockState["ota"]> = {},
): void {
  state.ota = {
    ...state.ota,
    phase,
    channel: "stable",
    localVersion: state.version,
    availableVersion: "2026.8.2",
    bytesDone: 0,
    bytesTotal: 0,
    error: "",
    generation: state.ota.generation + 1,
    ...extra,
  };
}

function clearSimulatorControls(target: MockState): void {
  target.faults = emptyFaults();
  target.scanMode = "normal";
  target.scanReadyAt = 0;
  target.heartBusy = false;
}

function resetBaselineSettings(target: MockState): void {
  target.batteryMv = 3900;
  target.batteryPct = 55;
  target.audioTxEnabled = false;
  target.audioRxEnabled = false;
  target.audioTxVolume = 70;
  target.audioRxVolume = 70;
  target.quietHourStart = 0;
  target.quietHourEnd = 0;
  target.ledEnabled = true;
  target.resetDays = 7;
  target.lang = "en";
  target.theme = "system";
  target.txHz = 880;
  target.txMs = 80;
  target.rxHz = 660;
  target.rxMs = 140;
  target.settingsNvsOk = true;
  target.settingsApplyPending = false;
  target.mqttNvsOk = true;
  target.mqttNvsOk = true;
}

export function createInitialState(scenario: MockScenario = "sta-connected"): MockState {
  const deviceId = "a1b2c3";
  const state: MockState = {
    scenario,
    mode: "sta",
    version: "dev-sim",
    hostname: `chaya2mqtt-${deviceId}`,
    deviceId,
    rx: 3,
    tx: 7,
    mqttConnected: true,
    wifiConnected: true,
    wifiSsid: "MockNet",
    wifiIp: "192.168.1.42",
    wifiGateway: "192.168.1.1",
    wifiNetmask: "255.255.255.0",
    wifiDns1: "1.1.1.1",
    wifiDns2: "1.0.0.1",
    wifiRssi: -55,
    wifiConfig: {
      mode: "dhcp",
      ip: "",
      gateway: "",
      netmask: "255.255.255.0",
      dns1: "",
      dns2: "",
      ntp1: "",
      ntp2: "",
    },
    mqtt: defaultMqtt(deviceId),
    resetDays: 7,
    lang: "en",
    theme: "system",
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
    settingsNvsOk: true,
    settingsApplyPending: false,
    mqttNvsOk: true,
    batteryMv: 3900,
    batteryPct: 55,
    heartBusy: false,
    wifiConnect: idleWifiConnect(),
    scanReadyAt: 0,
    scanMode: "normal",
    faults: emptyFaults(),
    ota: {
      phase: "idle",
      channel: "stable",
      localVersion: "dev-sim",
      availableVersion: "",
      bytesDone: 0,
      bytesTotal: 0,
      error: "",
      generation: 1,
    },
  };
  applyScenario(state, scenario);
  return state;
}

export function applyScenario(state: MockState, scenario: MockScenario): void {
  state.scenario = scenario;
  state.mqtt = defaultMqtt(state.deviceId);
  state.wifiConfig = {
    mode: "dhcp",
    ip: "",
    gateway: "",
    netmask: "255.255.255.0",
    dns1: "",
    dns2: "",
    ntp1: "",
    ntp2: "",
  };
  state.wifiConnect = idleWifiConnect();
  clearSimulatorControls(state);
  resetBaselineSettings(state);

  switch (scenario) {
    case "ap-setup":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      setOtaIdle(state);
      break;
    case "ap-test-idle":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.wifiConnect = {
        ...idleWifiConnect(),
        state: "idle",
        ssid: "MockNet",
      };
      setOtaIdle(state);
      break;
    case "ap-test-testing":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.wifiConnect = {
        ...idleWifiConnect(),
        state: "testing",
        ssid: "MockNet",
        password: "secret",
        startedAt: Date.now(),
        freeze: true,
      };
      setOtaIdle(state);
      break;
    case "ap-test-ok":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.wifiConnect = {
        ...idleWifiConnect(),
        state: "ok",
        ssid: "MockNet",
        password: "secret",
        startedAt: Date.now() - 3000,
      };
      state.wifiSsid = "MockNet";
      state.wifiIp = "192.168.1.77";
      state.wifiGateway = "192.168.1.1";
      state.wifiNetmask = "255.255.255.0";
      state.wifiDns1 = "1.1.1.1";
      state.wifiDns2 = "1.0.0.1";
      state.wifiRssi = -48;
      setOtaIdle(state);
      break;
    case "ap-test-failed":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.wifiConnect = {
        ...idleWifiConnect(),
        state: "fail",
        ssid: "MockNet",
        password: "fail",
        startedAt: Date.now() - 3000,
      };
      setOtaIdle(state);
      break;
    case "wifi-scan-empty":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.scanMode = "empty";
      setOtaIdle(state);
      break;
    case "wifi-scan-fail":
      state.mode = "ap";
      clearWifiLink(state);
      state.mqttConnected = false;
      state.scanMode = "fail";
      setOtaIdle(state);
      break;
    case "wifi-test-start-fail":
      armApWifiTestPreview(state, "testing");
      state.faults["wifi-connect"] = true;
      break;
    case "wifi-test-save-fail":
      armApWifiTestPreview(state, "ok");
      state.faults["wifi-commit"] = true;
      break;
    case "wifi-test-retry-fail":
      armApWifiTestPreview(state, "fail");
      state.faults["wifi-retry"] = true;
      break;
    case "wifi-test-abort-fail":
      armApWifiTestPreview(state, "testing");
      state.faults["wifi-abort"] = true;
      break;
    case "sse-disconnected":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.sse = true;
      setOtaIdle(state);
      break;
    case "device-unreachable":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.device = true;
      setOtaIdle(state);
      break;
    case "sta-mqtt-offline":
      applyStaOnlineDefaults(state);
      state.mqttConnected = false;
      setOtaIdle(state);
      break;
    case "sta-mqtt-unconfigured":
      applyStaOnlineDefaults(state);
      state.mqtt = {
        server: "",
        port: 8883,
        tls: true,
        username: "",
        password: "",
        topicPub: `chaya2mqtt/${state.deviceId}`,
        topicSub: "",
        partnerId: "",
      };
      state.mqttConnected = false;
      setOtaIdle(state);
      break;
    case "sta-mqtt-unpaired":
      applyStaOnlineDefaults(state);
      state.mqtt.partnerId = "";
      state.mqtt.topicSub = "";
      state.mqttConnected = true;
      setOtaIdle(state);
      break;
    case "mqtt-no-auth":
      applyStaOnlineDefaults(state);
      state.mqtt.username = "";
      state.mqtt.password = "";
      state.mqttConnected = true;
      setOtaIdle(state);
      break;
    case "mqtt-load-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.mqtt = true;
      setOtaIdle(state);
      break;
    case "mqtt-save-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["mqtt-save"] = true;
      setOtaIdle(state);
      break;
    case "settings-load-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.settings = true;
      setOtaIdle(state);
      break;
    case "settings-save-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["settings-save"] = true;
      setOtaIdle(state);
      break;
    case "settings-nvs-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.settingsNvsOk = false;
      setOtaIdle(state);
      break;
    case "settings-reboot-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.reboot = true;
      setOtaIdle(state);
      break;
    case "settings-factory-reset-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["factory-reset"] = true;
      setOtaIdle(state);
      break;
    case "wifi-static":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.wifiConfig = {
        mode: "static",
        ip: "192.168.1.42",
        gateway: "192.168.1.1",
        netmask: "255.255.255.0",
        dns1: "1.1.1.1",
        dns2: "1.0.0.1",
        ntp1: "pool.ntp.org",
        ntp2: "",
      };
      setOtaIdle(state);
      break;
    case "wifi-sta-save-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["wifi-connect"] = true;
      setOtaIdle(state);
      break;
    case "wifi-weak":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.wifiRssi = -78;
      setOtaIdle(state);
      break;
    case "battery-full":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.batteryPct = 100;
      state.batteryMv = 4200;
      setOtaIdle(state);
      break;
    case "battery-medium":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.batteryPct = 55;
      state.batteryMv = 3900;
      setOtaIdle(state);
      break;
    case "battery-low":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.batteryPct = 20;
      state.batteryMv = 3600;
      setOtaIdle(state);
      break;
    case "battery-critical":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.batteryPct = 8;
      state.batteryMv = 3400;
      setOtaIdle(state);
      break;
    case "heart-busy":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.heartBusy = true;
      setOtaIdle(state);
      break;
    case "heart-send-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults.heart = true;
      setOtaIdle(state);
      break;
    case "update-available":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "available");
      break;
    case "update-checking":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "checking", { availableVersion: "" });
      break;
    case "update-error":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "error", { error: "install_failed" });
      break;
    case "update-check-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["update-check"] = true;
      setOtaIdle(state);
      break;
    case "update-install-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["update-install"] = true;
      setOtaPhase(state, "available");
      break;
    case "update-status-fail":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      state.faults["update-status"] = true;
      setOtaIdle(state);
      break;
    case "update-busy":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "downloading", {
        bytesDone: 400000,
        bytesTotal: 1000000,
      });
      break;
    case "update-progress-unknown":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "downloading", {
        bytesDone: 0,
        bytesTotal: 0,
      });
      break;
    case "update-verifying":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "verifying", {
        bytesDone: 1000000,
        bytesTotal: 1000000,
      });
      break;
    case "update-rebooting":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "rebooting", {
        bytesDone: 1000000,
        bytesTotal: 1000000,
      });
      break;
    case "update-uptodate":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaIdle(state);
      break;
    case "update-beta":
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaPhase(state, "available", {
        channel: "beta",
        availableVersion: "2026.8.2-rc.1",
      });
      break;
    case "sta-connected":
    default:
      applyStaOnlineDefaults(state);
      state.mqttConnected = true;
      setOtaIdle(state);
      break;
  }
}

let state = createInitialState("sta-connected");

/** Bumped on every reset so async OTA sim timers stop mutating a new state. */
let otaSimEpoch = 0;

export function getOtaSimEpoch(): number {
  return otaSimEpoch;
}

export function getState(): MockState {
  return state;
}

export function resetState(scenario?: MockScenario): MockState {
  otaSimEpoch += 1;
  state = createInitialState(scenario ?? state.scenario);
  broadcastAll();
  return state;
}

function armApWifiTestPreview(target: MockState, connectState: "testing" | "ok" | "fail"): void {
  target.mode = "ap";
  clearWifiLink(target);
  target.mqttConnected = false;
  setOtaIdle(target);
  if (connectState === "testing") {
    target.wifiConnect = {
      ...idleWifiConnect(),
      state: "testing",
      ssid: "MockNet",
      password: "secret",
      startedAt: Date.now(),
      freeze: true,
    };
    return;
  }
  if (connectState === "ok") {
    target.wifiConnect = {
      ...idleWifiConnect(),
      state: "ok",
      ssid: "MockNet",
      password: "secret",
      startedAt: Date.now() - 3000,
    };
    target.wifiSsid = "MockNet";
    target.wifiIp = "192.168.1.77";
    target.wifiGateway = "192.168.1.1";
    target.wifiNetmask = "255.255.255.0";
    target.wifiDns1 = "1.1.1.1";
    target.wifiDns2 = "1.0.0.1";
    target.wifiRssi = -48;
    return;
  }
  target.wifiConnect = {
    ...idleWifiConnect(),
    state: "fail",
    ssid: "MockNet",
    password: "fail",
    startedAt: Date.now() - 3000,
  };
}

export function setFault(key: MockFaultKey, enabled: boolean): MockFaults {
  state.faults[key] = enabled;
  if (enabled) {
    if (key === "wifi-connect") {
      armApWifiTestPreview(state, "testing");
    } else if (key === "wifi-commit") {
      armApWifiTestPreview(state, "ok");
    } else if (key === "wifi-retry") {
      armApWifiTestPreview(state, "fail");
    }
  }
  return { ...state.faults };
}

export function clearFaults(): MockFaults {
  state.faults = emptyFaults();
  return { ...state.faults };
}

export function hasFault(key: MockFaultKey, target: MockState = state): boolean {
  return target.faults[key];
}

export function subscribe(fn: (event: string, data: unknown) => void): () => void {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export function emit(event: string, data: unknown): void {
  for (const fn of listeners) fn(event, data);
}

export function chayaPayload() {
  return {
    rx: state.rx,
    tx: state.tx,
    connected: state.mqttConnected,
    configured: Boolean(state.mqtt.server),
    paired: Boolean(state.mqtt.partnerId),
  };
}

export function wifiPayload() {
  if (!state.wifiConnected) return { connected: false as const };
  return {
    connected: true as const,
    ssid: state.wifiSsid,
    ip: state.wifiIp,
    gateway: state.wifiGateway,
    netmask: state.wifiNetmask,
    dns1: state.wifiDns1,
    dns2: state.wifiDns2,
    rssi: state.wifiRssi,
  };
}

export function wifiConfigPayload() {
  return {
    ssid: state.wifiSsid,
    mode: state.wifiConfig.mode,
    ip: state.wifiConfig.ip,
    gateway: state.wifiConfig.gateway,
    netmask: state.wifiConfig.netmask,
    dns1: state.wifiConfig.dns1,
    dns2: state.wifiConfig.dns2,
    ntp1: state.wifiConfig.ntp1,
    ntp2: state.wifiConfig.ntp2,
  };
}

export function mqttPayload() {
  return { connected: state.mqttConnected };
}

export function otaPayload() {
  return { ...state.ota, localVersion: state.version };
}

export function bumpOta(partial: Partial<MockState["ota"]>): void {
  state.ota = {
    ...state.ota,
    ...partial,
    localVersion: state.version,
    generation: state.ota.generation + 1,
  };
  emit("ota", otaPayload());
}

export function broadcastAll(): void {
  emit("chaya", chayaPayload());
  emit("wifi", wifiPayload());
  emit("mqtt", mqttPayload());
  emit("ota", otaPayload());
  emit("device", deviceBatteryPayload());
}

export function tickWifiConnect(): void {
  if (state.wifiConnect.state !== "testing") return;
  if (state.wifiConnect.freeze) return;
  if (Date.now() - state.wifiConnect.startedAt < 2500) return;
  const ok = state.wifiConnect.password !== "fail";
  state.wifiConnect.state = ok ? "ok" : "fail";
  if (ok) {
    state.wifiSsid = state.wifiConnect.ssid;
    if (state.wifiConnect.mode === "static" && state.wifiConnect.ip) {
      state.wifiIp = state.wifiConnect.ip;
      state.wifiGateway = state.wifiConnect.gateway;
      state.wifiNetmask = state.wifiConnect.netmask;
      state.wifiDns1 = state.wifiConnect.dns1;
      state.wifiDns2 = state.wifiConnect.dns2;
    } else {
      state.wifiIp = "192.168.1.77";
      state.wifiGateway = "192.168.1.1";
      state.wifiNetmask = "255.255.255.0";
      state.wifiDns1 = "1.1.1.1";
      state.wifiDns2 = "1.0.0.1";
    }
    state.wifiRssi = -48;
  }
}

export function deviceBatteryPayload() {
  return { batteryMv: state.batteryMv, batteryPct: state.batteryPct };
}

export function devicePayload() {
  const base = {
    hostname: state.mode === "ap" ? "chaya2mqtt" : state.hostname,
    version: state.version,
    mode: state.mode,
    deviceId: state.deviceId,
    batteryMv: state.batteryMv,
    batteryPct: state.batteryPct,
  };
  if (state.mode !== "ap") {
    return base;
  }
  return {
    ...base,
    apSsid: "Chaya2MQTT",
    apIp: "4.3.2.1",
  };
}

export function mockControlPayload() {
  return {
    scenario: state.scenario,
    mode: state.mode,
    scanMode: state.scanMode,
    faults: { ...state.faults },
  };
}

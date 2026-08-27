import { afterEach, describe, expect, it } from "vitest";
import {
  MOCK_SCENARIOS,
  applyScenario,
  createInitialState,
  hasFault,
  otaBlocksDestructiveAction,
  parseFaultKey,
  parseScenario,
  resetState,
  setFault,
  type MockScenario,
} from "./deviceState.ts";

afterEach(() => {
  resetState("sta-connected");
});

describe("parseScenario", () => {
  it("accepts known scenarios and rejects unknown values", () => {
    expect(parseScenario("sta-connected")).toBe("sta-connected");
    expect(parseScenario("update-busy")).toBe("update-busy");
    expect(parseScenario("boot-unreachable")).toBe("boot-unreachable");
    expect(parseScenario("nope")).toBeNull();
    expect(parseScenario(null)).toBeNull();
  });

  it("lists every toolbar scenario", () => {
    expect(MOCK_SCENARIOS).toEqual([
      "sta-connected",
      "offline",
      "sta-mqtt-offline",
      "sta-mqtt-unconfigured",
      "sta-mqtt-unpaired",
      "ap-setup",
      "ap-test-idle",
      "ap-test-testing",
      "ap-test-ok",
      "ap-test-failed",
      "wifi-scan-empty",
      "wifi-scan-fail",
      "boot-unreachable",
      "boot-slow",
      "sse-disconnected",
      "update-available",
      "update-checking",
      "update-busy",
      "update-verifying",
      "update-rebooting",
      "update-error",
    ]);
  });
});

describe("parseFaultKey", () => {
  it("accepts known faults", () => {
    expect(parseFaultKey("mqtt")).toBe("mqtt");
    expect(parseFaultKey("sse")).toBe("sse");
    expect(parseFaultKey("nope")).toBeNull();
  });
});

describe("applyScenario", () => {
  const cases: Array<{
    scenario: MockScenario;
    mode: "ap" | "sta";
    wifi: boolean;
    mqtt: boolean;
    partner?: string;
    server?: string;
    otaPhase?: string;
    otaError?: string;
    wifiConnect?: "idle" | "testing" | "ok" | "fail";
    scanMode?: "normal" | "empty" | "fail";
    deviceFault?: boolean;
    sseFault?: boolean;
    deviceDelayMs?: number;
    freeze?: boolean;
  }> = [
    { scenario: "sta-connected", mode: "sta", wifi: true, mqtt: true, partner: "f5e6d7" },
    { scenario: "offline", mode: "sta", wifi: false, mqtt: false },
    {
      scenario: "sta-mqtt-offline",
      mode: "sta",
      wifi: true,
      mqtt: false,
      server: "mqtt.example.com",
    },
    {
      scenario: "sta-mqtt-unconfigured",
      mode: "sta",
      wifi: true,
      mqtt: false,
      server: "",
      partner: "",
    },
    {
      scenario: "sta-mqtt-unpaired",
      mode: "sta",
      wifi: true,
      mqtt: true,
      partner: "",
      server: "mqtt.example.com",
    },
    { scenario: "ap-setup", mode: "ap", wifi: false, mqtt: false, wifiConnect: "idle" },
    { scenario: "ap-test-idle", mode: "ap", wifi: false, mqtt: false, wifiConnect: "idle" },
    {
      scenario: "ap-test-testing",
      mode: "ap",
      wifi: false,
      mqtt: false,
      wifiConnect: "testing",
      freeze: true,
    },
    { scenario: "ap-test-ok", mode: "ap", wifi: false, mqtt: false, wifiConnect: "ok" },
    { scenario: "ap-test-failed", mode: "ap", wifi: false, mqtt: false, wifiConnect: "fail" },
    { scenario: "wifi-scan-empty", mode: "ap", wifi: false, mqtt: false, scanMode: "empty" },
    { scenario: "wifi-scan-fail", mode: "ap", wifi: false, mqtt: false, scanMode: "fail" },
    {
      scenario: "boot-unreachable",
      mode: "sta",
      wifi: true,
      mqtt: true,
      deviceFault: true,
    },
    {
      scenario: "boot-slow",
      mode: "sta",
      wifi: true,
      mqtt: true,
      deviceDelayMs: 3000,
    },
    {
      scenario: "sse-disconnected",
      mode: "sta",
      wifi: true,
      mqtt: true,
      sseFault: true,
    },
    {
      scenario: "update-available",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "available",
    },
    {
      scenario: "update-checking",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "checking",
    },
    {
      scenario: "update-error",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "error",
      otaError: "install_failed",
    },
    {
      scenario: "update-busy",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "downloading",
    },
    {
      scenario: "update-verifying",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "verifying",
    },
    {
      scenario: "update-rebooting",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "rebooting",
    },
  ];

  it.each(cases)(
    "$scenario sets expected invariants",
    ({
      scenario,
      mode,
      wifi,
      mqtt,
      partner,
      server,
      otaPhase,
      otaError,
      wifiConnect,
      scanMode,
      deviceFault,
      sseFault,
      deviceDelayMs,
      freeze,
    }) => {
      const state = createInitialState(scenario);
      expect(state.scenario).toBe(scenario);
      expect(state.mode).toBe(mode);
      expect(state.wifiConnected).toBe(wifi);
      expect(state.mqttConnected).toBe(mqtt);
      if (partner !== undefined) expect(state.mqtt.partnerId).toBe(partner);
      if (server !== undefined) expect(state.mqtt.server).toBe(server);
      if (otaPhase !== undefined) expect(state.ota.phase).toBe(otaPhase);
      if (otaError !== undefined) expect(state.ota.error).toBe(otaError);
      if (wifiConnect !== undefined) expect(state.wifiConnect.state).toBe(wifiConnect);
      if (scanMode !== undefined) expect(state.scanMode).toBe(scanMode);
      if (deviceFault !== undefined) expect(state.faults.device).toBe(deviceFault);
      if (sseFault !== undefined) expect(state.faults.sse).toBe(sseFault);
      if (deviceDelayMs !== undefined) expect(state.deviceDelayMs).toBe(deviceDelayMs);
      if (freeze !== undefined) expect(state.wifiConnect.freeze).toBe(freeze);
    },
  );

  it("rebuilds from a clean base so previous scenario values do not leak", () => {
    const state = createInitialState("update-busy");
    expect(state.ota.phase).toBe("downloading");
    applyScenario(state, "sta-mqtt-unconfigured");
    expect(state.scenario).toBe("sta-mqtt-unconfigured");
    expect(state.wifiConnected).toBe(true);
    expect(state.mqtt.server).toBe("");
    expect(state.mqtt.partnerId).toBe("");
    expect(state.ota.phase).toBe("idle");
    expect(state.ota.availableVersion).toBe("");
    expect(state.wifiConnect.state).toBe("idle");
    expect(state.faults.device).toBe(false);
    expect(state.deviceDelayMs).toBe(0);
  });

  it("clears previous faults when switching away from boot-unreachable", () => {
    const state = createInitialState("boot-unreachable");
    expect(hasFault("device", state)).toBe(true);
    applyScenario(state, "sta-connected");
    expect(hasFault("device", state)).toBe(false);
  });
});

describe("setFault", () => {
  it("toggles individual faults without rewriting the scenario", () => {
    resetState("sta-connected");
    setFault("mqtt", true);
    expect(hasFault("mqtt")).toBe(true);
    setFault("mqtt", false);
    expect(hasFault("mqtt")).toBe(false);
  });
});

describe("otaBlocksDestructiveAction", () => {
  it("blocks while OTA check/flash phases are active", () => {
    expect(otaBlocksDestructiveAction(createInitialState("update-busy"))).toBe(true);
    expect(otaBlocksDestructiveAction(createInitialState("update-checking"))).toBe(true);
    expect(otaBlocksDestructiveAction(createInitialState("update-verifying"))).toBe(true);
    expect(otaBlocksDestructiveAction(createInitialState("update-rebooting"))).toBe(true);
    expect(otaBlocksDestructiveAction(createInitialState("update-available"))).toBe(false);
    expect(otaBlocksDestructiveAction(createInitialState("update-error"))).toBe(false);
  });
});

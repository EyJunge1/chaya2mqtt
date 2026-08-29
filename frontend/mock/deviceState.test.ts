import { afterEach, describe, expect, it } from "vitest";
import {
  MOCK_SCENARIOS,
  applyScenario,
  createInitialState,
  getState,
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
    expect(parseScenario("sse-disconnected")).toBe("sse-disconnected");
    expect(parseScenario("nope")).toBeNull();
    expect(parseScenario(null)).toBeNull();
  });

  it("lists every toolbar scenario", () => {
    expect(MOCK_SCENARIOS).toEqual([
      "sta-connected",
      "sse-disconnected",
      "device-unreachable",
      "battery-full",
      "battery-medium",
      "battery-low",
      "battery-critical",
      "heart-busy",
      "heart-send-fail",
      "sta-mqtt-offline",
      "sta-mqtt-unconfigured",
      "sta-mqtt-unpaired",
      "mqtt-no-auth",
      "mqtt-load-fail",
      "mqtt-save-fail",
      "settings-load-fail",
      "settings-save-fail",
      "settings-reboot-fail",
      "settings-factory-reset-fail",
      "wifi-weak",
      "wifi-static",
      "wifi-sta-save-fail",
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
    mqttFault?: boolean;
    mqttSaveFault?: boolean;
    settingsFault?: boolean;
    settingsSaveFault?: boolean;
    rebootFault?: boolean;
    factoryResetFault?: boolean;
    heartFault?: boolean;
    updateCheckFault?: boolean;
    updateInstallFault?: boolean;
    updateStatusFault?: boolean;
    wifiConnectFault?: boolean;
    freeze?: boolean;
  }> = [
    { scenario: "sta-connected", mode: "sta", wifi: true, mqtt: true, partner: "f5e6d7" },
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
    { scenario: "mqtt-no-auth", mode: "sta", wifi: true, mqtt: true, partner: "f5e6d7" },
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
    {
      scenario: "wifi-test-start-fail",
      mode: "ap",
      wifi: false,
      mqtt: false,
      wifiConnect: "testing",
      freeze: true,
    },
    {
      scenario: "wifi-test-save-fail",
      mode: "ap",
      wifi: false,
      mqtt: false,
      wifiConnect: "ok",
    },
    {
      scenario: "wifi-test-retry-fail",
      mode: "ap",
      wifi: false,
      mqtt: false,
      wifiConnect: "fail",
    },
    {
      scenario: "wifi-test-abort-fail",
      mode: "ap",
      wifi: false,
      mqtt: false,
      wifiConnect: "testing",
      freeze: true,
    },
    { scenario: "wifi-scan-empty", mode: "ap", wifi: false, mqtt: false, scanMode: "empty" },
    { scenario: "wifi-scan-fail", mode: "ap", wifi: false, mqtt: false, scanMode: "fail" },
    { scenario: "wifi-static", mode: "sta", wifi: true, mqtt: true },
    {
      scenario: "wifi-sta-save-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      wifiConnectFault: true,
    },
    { scenario: "wifi-weak", mode: "sta", wifi: true, mqtt: true },
    {
      scenario: "sse-disconnected",
      mode: "sta",
      wifi: true,
      mqtt: true,
      sseFault: true,
    },
    {
      scenario: "device-unreachable",
      mode: "sta",
      wifi: true,
      mqtt: true,
      deviceFault: true,
    },
    {
      scenario: "mqtt-load-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      mqttFault: true,
    },
    {
      scenario: "mqtt-save-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      mqttSaveFault: true,
    },
    {
      scenario: "settings-load-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      settingsFault: true,
    },
    {
      scenario: "settings-save-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      settingsSaveFault: true,
    },
    {
      scenario: "settings-reboot-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      rebootFault: true,
    },
    {
      scenario: "settings-factory-reset-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      factoryResetFault: true,
    },
    { scenario: "battery-full", mode: "sta", wifi: true, mqtt: true },
    { scenario: "battery-medium", mode: "sta", wifi: true, mqtt: true },
    { scenario: "battery-low", mode: "sta", wifi: true, mqtt: true },
    { scenario: "battery-critical", mode: "sta", wifi: true, mqtt: true },
    { scenario: "heart-busy", mode: "sta", wifi: true, mqtt: true },
    {
      scenario: "heart-send-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      heartFault: true,
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
      scenario: "update-check-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      updateCheckFault: true,
    },
    {
      scenario: "update-install-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "available",
      updateInstallFault: true,
    },
    {
      scenario: "update-status-fail",
      mode: "sta",
      wifi: true,
      mqtt: true,
      updateStatusFault: true,
    },
    {
      scenario: "update-busy",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "downloading",
    },
    {
      scenario: "update-progress-unknown",
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
    {
      scenario: "update-uptodate",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "idle",
    },
    {
      scenario: "update-beta",
      mode: "sta",
      wifi: true,
      mqtt: true,
      otaPhase: "available",
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
      mqttFault,
      mqttSaveFault,
      settingsFault,
      settingsSaveFault,
      rebootFault,
      factoryResetFault,
      heartFault,
      updateCheckFault,
      updateInstallFault,
      updateStatusFault,
      wifiConnectFault,
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
      if (mqttFault !== undefined) expect(state.faults.mqtt).toBe(mqttFault);
      if (mqttSaveFault !== undefined) expect(state.faults["mqtt-save"]).toBe(mqttSaveFault);
      if (settingsFault !== undefined) expect(state.faults.settings).toBe(settingsFault);
      if (settingsSaveFault !== undefined) {
        expect(state.faults["settings-save"]).toBe(settingsSaveFault);
      }
      if (rebootFault !== undefined) expect(state.faults.reboot).toBe(rebootFault);
      if (factoryResetFault !== undefined) {
        expect(state.faults["factory-reset"]).toBe(factoryResetFault);
      }
      if (heartFault !== undefined) expect(state.faults.heart).toBe(heartFault);
      if (updateCheckFault !== undefined) {
        expect(state.faults["update-check"]).toBe(updateCheckFault);
      }
      if (updateInstallFault !== undefined) {
        expect(state.faults["update-install"]).toBe(updateInstallFault);
      }
      if (updateStatusFault !== undefined) {
        expect(state.faults["update-status"]).toBe(updateStatusFault);
      }
      if (wifiConnectFault !== undefined) {
        expect(state.faults["wifi-connect"]).toBe(wifiConnectFault);
      }
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
  });

  it("clears previous faults when switching scenarios", () => {
    const state = createInitialState("sse-disconnected");
    expect(hasFault("sse", state)).toBe(true);
    applyScenario(state, "sta-connected");
    expect(hasFault("sse", state)).toBe(false);
  });

  it("sets edge-case scenario fields without leaking across switches", () => {
    const full = createInitialState("battery-full");
    expect(full.batteryPct).toBe(100);
    expect(full.batteryMv).toBe(4200);

    const medium = createInitialState("battery-medium");
    expect(medium.batteryPct).toBe(55);
    expect(medium.batteryMv).toBe(3900);

    const low = createInitialState("battery-low");
    expect(low.batteryPct).toBe(20);
    expect(low.batteryMv).toBe(3600);

    const critical = createInitialState("battery-critical");
    expect(critical.batteryPct).toBe(8);
    expect(critical.heartBusy).toBe(false);

    const busy = createInitialState("heart-busy");
    expect(busy.heartBusy).toBe(true);
    expect(busy.batteryPct).toBe(55);

    const weak = createInitialState("wifi-weak");
    expect(weak.wifiRssi).toBe(-78);

    const staticIp = createInitialState("wifi-static");
    expect(staticIp.wifiConfig.mode).toBe("static");
    expect(staticIp.wifiConfig.ip).toBe("192.168.1.42");

    const noAuth = createInitialState("mqtt-no-auth");
    expect(noAuth.mqtt.username).toBe("");
    expect(noAuth.mqtt.password).toBe("");

    const unknown = createInitialState("update-progress-unknown");
    expect(unknown.ota.phase).toBe("downloading");
    expect(unknown.ota.bytesTotal).toBe(0);

    const uptodate = createInitialState("update-uptodate");
    expect(uptodate.ota.phase).toBe("idle");
    expect(uptodate.ota.availableVersion).toBe("");

    const beta = createInitialState("update-beta");
    expect(beta.ota.channel).toBe("beta");
    expect(beta.ota.availableVersion).toBe("2026.8.2-rc.1");

    applyScenario(noAuth, "sta-connected");
    expect(noAuth.mqtt.username).toBe("chaya");
    expect(noAuth.heartBusy).toBe(false);
    expect(noAuth.batteryPct).toBe(55);
    expect(noAuth.wifiRssi).toBe(-55);
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

  it("arms AP wifi-test previews for start and save faults", () => {
    resetState("sta-connected");
    setFault("wifi-connect", true);
    expect(getState().mode).toBe("ap");
    expect(getState().wifiConnect.state).toBe("testing");
    expect(getState().wifiConnect.ssid).toBe("MockNet");

    resetState("sta-connected");
    setFault("wifi-commit", true);
    expect(getState().mode).toBe("ap");
    expect(getState().wifiConnect.state).toBe("ok");
  });

  it("arms AP wifi-test fail preview for retry fault", () => {
    resetState("sta-connected");
    setFault("wifi-retry", true);
    expect(getState().mode).toBe("ap");
    expect(getState().wifiConnect.state).toBe("fail");
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

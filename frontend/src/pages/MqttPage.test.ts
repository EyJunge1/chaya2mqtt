import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { MqttConfigView } from "../api/types.ts";
import MqttPage from "./MqttPage.svelte";

const { getMqttConfig, saveMqtt } = vi.hoisted(() => ({
  getMqttConfig: vi.fn(),
  saveMqtt: vi.fn(),
}));

vi.mock("../api/client", () => ({
  api: {
    getMqttConfig: () => getMqttConfig(),
    saveMqtt: (fields: unknown) => saveMqtt(fields),
  },
}));

vi.mock("../i18n/i18n.svelte.ts", () => ({
  i18n: { t: (key: string) => key, language: "en", setLanguage: () => undefined },
  useI18n: () => ({ t: (key: string) => key }),
}));

function cfg(partial: Partial<MqttConfigView> = {}): MqttConfigView {
  return {
    deviceId: "a1b2c3",
    server: "mqtt.example.com",
    port: 8883,
    tls: true,
    username: "chaya",
    hasPassword: true,
    topicPub: "chaya2mqtt/a1b2c3",
    topicSub: "chaya2mqtt/f5e6d7",
    partnerId: "f5e6d7",
    ...partial,
  };
}

describe("MqttPage", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    getMqttConfig.mockResolvedValue(cfg());
    saveMqtt.mockResolvedValue({ ok: true, message: "saved" });
  });

  it("saves broker and partner atomically", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    render(MqttPage, {
      props: { mqtt: { connected: true }, onToast, onDeviceRefresh },
    });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_tls: 1,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "abcdef",
      });
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    expect(onDeviceRefresh).toHaveBeenCalled();
  });

  it("saves broker without username or password when partner is set", async () => {
    const onToast = vi.fn();
    getMqttConfig.mockResolvedValue(
      cfg({ username: "", hasPassword: false, partnerId: "f5e6d7", topicSub: "chaya2mqtt/f5e6d7" }),
    );
    render(MqttPage, { props: { mqtt: { connected: false }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    expect(screen.getAllByText("(common.optional)").length).toBe(2);
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_tls: 1,
        mqtt_user: "",
        mqtt_pass: undefined,
        partner_id: "f5e6d7",
      });
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
  });

  it("unpairs without clearing the broker", async () => {
    const onToast = vi.fn();
    getMqttConfig
      .mockResolvedValueOnce(cfg())
      .mockResolvedValueOnce(cfg({ partnerId: "", topicSub: "" }));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "mqtt.unpair" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_tls: 1,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "",
      });
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
  });

  it("switches protocol and auto-adjusts default ports", async () => {
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    expect(screen.getByText("mqtts://mqtt.example.com:8883")).toBeTruthy();

    fireEvent.click(screen.getByTestId("mqtt-proto-mqtt"));
    await waitFor(() => {
      expect(screen.getByDisplayValue("1883")).toBeTruthy();
    });
    expect(screen.getByText("mqtt://mqtt.example.com:1883")).toBeTruthy();

    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith(
        expect.objectContaining({
          mqtt_port: 1883,
          mqtt_tls: 0,
        }),
      );
    });
  });

  it("keeps a custom port when switching protocol", async () => {
    getMqttConfig.mockResolvedValue(cfg({ port: 8884, tls: true }));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByTestId("mqtt-proto-mqtt"));

    expect(screen.getByDisplayValue("8884")).toBeTruthy();
    expect(screen.getByText("mqtt://mqtt.example.com:8884")).toBeTruthy();
  });

  it("rejects invalid partner IDs", async () => {
    const onToast = vi.fn();
    saveMqtt.mockResolvedValue({ ok: false, error: "partner" });
    render(MqttPage, { props: { mqtt: { connected: false }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "a1b2c3" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.partner-invalid", "error");
    });
  });

  it("shows broker details when connected without partner", async () => {
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "", topicSub: "" }));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

    await screen.findByDisplayValue("mqtt.example.com");
    expect(screen.getByText("mqtts://mqtt.example.com:8883")).toBeTruthy();
    expect(screen.getAllByText("-").length).toBeGreaterThan(0);
  });

  it("shows RadioOff when no broker is configured", async () => {
    getMqttConfig.mockResolvedValue(cfg({ server: "", partnerId: "", topicSub: "" }));
    const { container } = render(MqttPage, {
      props: { mqtt: { connected: false }, onToast: vi.fn() },
    });

    await screen.findByText("a1b2c3");
    expect(container.querySelector(".lucide-radio-off")).toBeInTheDocument();
  });

  it("reloads config when refreshSeq changes", async () => {
    getMqttConfig
      .mockResolvedValueOnce(cfg({ server: "", partnerId: "" }))
      .mockResolvedValueOnce(cfg({ partnerId: "", topicSub: "" }));
    const { rerender } = render(MqttPage, {
      props: { mqtt: { connected: false }, refreshSeq: 1, onToast: vi.fn() },
    });

    await screen.findByText("a1b2c3");
    expect(screen.getAllByText("-").length).toBeGreaterThan(0);
    await rerender({ mqtt: { connected: true }, refreshSeq: 2, onToast: vi.fn() });

    expect(await screen.findByText("mqtts://mqtt.example.com:8883")).toBeTruthy();
    expect(getMqttConfig).toHaveBeenCalledTimes(2);
  });
});

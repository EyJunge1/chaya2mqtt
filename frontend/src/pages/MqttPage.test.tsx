import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { MqttPage } from "./MqttPage";
import type { MqttConfigView } from "../api/types";

const getMqttConfig = vi.fn();
const saveMqtt = vi.fn();
const t = (key: string) => key;

vi.mock("../api/client", () => ({
  api: {
    getMqttConfig: () => getMqttConfig(),
    saveMqtt: (fields: unknown) => saveMqtt(fields),
  },
}));

vi.mock("../i18n/useI18n", () => ({
  useI18n: () => ({ t }),
}));

function cfg(partial: Partial<MqttConfigView> = {}): MqttConfigView {
  return {
    deviceId: "a1b2c3",
    server: "mqtt.example.com",
    port: 8883,
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
    render(
      <MqttPage mqtt={{ connected: true }} onToast={onToast} onDeviceRefresh={onDeviceRefresh} />,
    );

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "abcdef",
      });
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    expect(onDeviceRefresh).toHaveBeenCalled();
  });

  it("unpairs without clearing the broker", async () => {
    const onToast = vi.fn();
    getMqttConfig
      .mockResolvedValueOnce(cfg())
      .mockResolvedValueOnce(cfg({ partnerId: "", topicSub: "" }));
    render(<MqttPage mqtt={{ connected: true }} onToast={onToast} />);

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "mqtt.unpair" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "",
      });
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
  });

  it("rejects invalid partner IDs", async () => {
    const onToast = vi.fn();
    saveMqtt.mockResolvedValue({ ok: false, error: "partner" });
    render(<MqttPage mqtt={{ connected: false }} onToast={onToast} />);

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "a1b2c3" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.partner-invalid", "error");
    });
  });

  it("shows broker details when connected without partner", async () => {
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "", topicSub: "" }));
    render(<MqttPage mqtt={{ connected: true }} onToast={vi.fn()} />);

    await screen.findByDisplayValue("mqtt.example.com");
    expect(screen.getByText("mqtt.example.com:8883")).toBeTruthy();
    expect(screen.getAllByText("-").length).toBeGreaterThan(0);
  });

  it("reloads config when refreshSeq changes", async () => {
    getMqttConfig
      .mockResolvedValueOnce(cfg({ server: "", partnerId: "" }))
      .mockResolvedValueOnce(cfg({ partnerId: "", topicSub: "" }));
    const { rerender } = render(
      <MqttPage mqtt={{ connected: false }} refreshSeq={1} onToast={vi.fn()} />,
    );

    await screen.findByText("a1b2c3");
    expect(screen.getAllByText("-").length).toBeGreaterThan(0);
    rerender(<MqttPage mqtt={{ connected: true }} refreshSeq={2} onToast={vi.fn()} />);

    expect(await screen.findByText("mqtt.example.com:8883")).toBeTruthy();
    expect(getMqttConfig).toHaveBeenCalledTimes(2);
  });
});

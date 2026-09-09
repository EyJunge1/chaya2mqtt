import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ApiHttpError } from "../api/client.ts";
import type { MqttConfigView } from "../api/types.ts";
// <script module> exports are available at runtime; generated Svelte types omit them.
// @ts-ignore svelte component types omit named module exports
import MqttPage, { resetMqttApplySession } from "./MqttPage.svelte";

const { getMqttConfig, saveMqtt } = vi.hoisted(() => ({
  getMqttConfig: vi.fn(),
  saveMqtt: vi.fn(),
}));

vi.mock("../api/client", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../api/client.ts")>();
  return {
    ...actual,
    api: {
      getMqttConfig: () => getMqttConfig(),
      saveMqtt: (fields: unknown) => saveMqtt(fields),
    },
  };
});

vi.mock("../i18n/i18n.svelte.ts", () => ({
  i18n: { t: (key: string) => key, language: "en", setLanguage: () => undefined },
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
    resetMqttApplySession();
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
    vi.clearAllMocks();
    Reflect.deleteProperty(document, "execCommand");
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
        mqtt_tls: true,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "abcdef",
      });
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
    expect(onDeviceRefresh).toHaveBeenCalled();
  });

  it("saves broker without username or password when partner is set", async () => {
    const onToast = vi.fn();
    getMqttConfig.mockResolvedValue(
      cfg({ username: "", hasPassword: false, partnerId: "f5e6d7", topicSub: "chaya2mqtt/f5e6d7" }),
    );
    render(MqttPage, { props: { mqtt: { connected: false }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalledWith({
        mqtt_server: "mqtt.example.com",
        mqtt_port: 8883,
        mqtt_tls: true,
        mqtt_user: "",
        mqtt_pass: undefined,
        partner_id: "f5e6d7",
      });
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
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
        mqtt_tls: true,
        mqtt_user: "chaya",
        mqtt_pass: undefined,
        partner_id: "",
      });
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
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
          mqtt_tls: false,
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

  it("copies the device ID without a toast when the clipboard helper succeeds", async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    vi.stubGlobal("isSecureContext", true);
    vi.stubGlobal("navigator", { ...navigator, clipboard: { writeText } });
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: false }, onToast } });

    fireEvent.click(await screen.findByRole("button", { name: "mqtt.copy-device-id" }));

    await waitFor(() => {
      expect(writeText).toHaveBeenCalledWith("a1b2c3");
    });
    expect(onToast).not.toHaveBeenCalled();
  });

  it("toasts when copying the device ID fails", async () => {
    vi.stubGlobal("isSecureContext", false);
    vi.stubGlobal("navigator", { ...navigator, clipboard: undefined });
    Object.defineProperty(document, "execCommand", {
      configurable: true,
      value: vi.fn().mockReturnValue(false),
    });
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: false }, onToast } });

    fireEvent.click(await screen.findByRole("button", { name: "mqtt.copy-device-id" }));

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.device-id-copy-failed", "error");
    });
  });

  it("shows RadioOff when no broker is configured", async () => {
    getMqttConfig.mockResolvedValue(cfg({ server: "", partnerId: "", topicSub: "" }));
    const { container } = render(MqttPage, {
      props: { mqtt: { connected: false }, onToast: vi.fn() },
    });

    await screen.findByText("a1b2c3");
    expect(container.querySelector(".lucide-radio-off")).toBeInTheDocument();
  });

  it("toasts a TLS hint from the submitted form, not the first GET", async () => {
    getMqttConfig
      .mockResolvedValueOnce(cfg({ tls: false, port: 1883 }))
      .mockResolvedValueOnce(cfg({ tls: true, port: 8883, applyPending: false }));
    saveMqtt.mockResolvedValue({ ok: true });
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
    expect(onToast).toHaveBeenCalledWith("toast.mqtt-tls-off", "warning");
  });

  it("waits for applyPending before replacing the form", async () => {
    const onToast = vi.fn();
    getMqttConfig
      .mockResolvedValueOnce(cfg())
      .mockResolvedValueOnce(cfg({ applyPending: true }))
      .mockResolvedValueOnce(
        cfg({ partnerId: "abcdef", topicSub: "chaya2mqtt/abcdef", applyPending: false }),
      );
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalled();
    });
    expect(screen.getByDisplayValue("abcdef")).toBeTruthy();
    expect(onToast).not.toHaveBeenCalled();

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
    expect(screen.getByDisplayValue("abcdef")).toBeTruthy();
    expect(getMqttConfig).toHaveBeenCalledTimes(3);
  });

  it("retries GET 503 busy during apply poll instead of save-failed", async () => {
    const onToast = vi.fn();
    getMqttConfig
      .mockResolvedValueOnce(cfg())
      .mockRejectedValueOnce(new ApiHttpError("/api/mqtt", 503, "busy"))
      .mockResolvedValueOnce(cfg({ applyPending: false }));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(saveMqtt).toHaveBeenCalled();
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
    expect(onToast).not.toHaveBeenCalledWith("toast.save-failed", "error");
  });

  it("shows save-failed when nvsOk is false after apply", async () => {
    const onToast = vi.fn();
    const onDeviceRefresh = vi.fn().mockResolvedValue(undefined);
    getMqttConfig
      .mockResolvedValueOnce(cfg())
      .mockResolvedValueOnce(cfg({ nvsOk: false, applyPending: false }));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast, onDeviceRefresh } });

    await screen.findByDisplayValue("mqtt.example.com");
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.save-failed", "error");
    });
    expect(onToast).not.toHaveBeenCalledWith("toast.mqtt-saved", "success");
    expect(onDeviceRefresh).not.toHaveBeenCalled();
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

  it("keeps submitted partner when remounting during applyPending", async () => {
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "f5e6d7", applyPending: true }));
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(1));

    cleanup();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    expect(await screen.findByDisplayValue("abcdef")).toBeTruthy();
    expect(screen.queryByDisplayValue("f5e6d7")).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await new Promise((r) => setTimeout(r, 30));
    expect(saveMqtt).toHaveBeenCalledTimes(1);
    expect(saveMqtt).not.toHaveBeenCalledWith(expect.objectContaining({ partner_id: "f5e6d7" }));
  });

  it("does not POST the live form when refreshSeq hydrates applyPending", async () => {
    const onToast = vi.fn();
    const { rerender } = render(MqttPage, {
      props: { mqtt: { connected: true }, refreshSeq: 1, onToast },
    });

    await screen.findByDisplayValue("mqtt.example.com");
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "f5e6d7", applyPending: true }));
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(1));

    await rerender({ mqtt: { connected: true }, refreshSeq: 2, onToast });

    expect(await screen.findByDisplayValue("abcdef")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await new Promise((r) => setTimeout(r, 30));
    expect(saveMqtt).toHaveBeenCalledTimes(1);
    expect(saveMqtt).not.toHaveBeenCalledWith(expect.objectContaining({ partner_id: "f5e6d7" }));
  });

  it("does not hydrate live GET fields on a cold load while applyPending", async () => {
    resetMqttApplySession();
    getMqttConfig
      .mockResolvedValueOnce(cfg({ partnerId: "f5e6d7", applyPending: true }))
      .mockResolvedValue(
        cfg({ partnerId: "abcdef", topicSub: "chaya2mqtt/abcdef", applyPending: false }),
      );

    render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

    expect(await screen.findByText("mqtt.loading")).toBeTruthy();
    expect(screen.queryByDisplayValue("f5e6d7")).toBeNull();
    expect(await screen.findByDisplayValue("abcdef")).toBeTruthy();
  });

  it("makes save clickable again after remount when applyPending is already false", async () => {
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "f5e6d7", applyPending: true }));
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(1));

    cleanup();
    getMqttConfig.mockResolvedValue(
      cfg({ partnerId: "abcdef", topicSub: "chaya2mqtt/abcdef", applyPending: false }),
    );
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    const save = await screen.findByRole("button", { name: "common.save" });
    await waitFor(() => expect(save).not.toHaveAttribute("aria-busy", "true"));
    expect(save).toBeEnabled();
    fireEvent.click(save);
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(2));
  });

  it("still toasts mqtt-saved when refreshSeq bumps during applyPending", async () => {
    const onToast = vi.fn();
    const { rerender } = render(MqttPage, {
      props: { mqtt: { connected: true }, refreshSeq: 1, onToast },
    });

    await screen.findByDisplayValue("mqtt.example.com");
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "f5e6d7", applyPending: true }));
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(1));

    await rerender({ mqtt: { connected: true }, refreshSeq: 2, onToast });
    expect(await screen.findByDisplayValue("abcdef")).toBeTruthy();

    getMqttConfig.mockResolvedValue(
      cfg({ partnerId: "abcdef", topicSub: "chaya2mqtt/abcdef", applyPending: false }),
    );
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("toast.mqtt-saved", "success");
    });
  });

  it("keeps submitted form when remount GET fails", async () => {
    const onToast = vi.fn();
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    await screen.findByDisplayValue("mqtt.example.com");
    getMqttConfig.mockResolvedValue(cfg({ partnerId: "f5e6d7", applyPending: true }));
    fireEvent.change(screen.getByDisplayValue("f5e6d7"), { target: { value: "abcdef" } });
    fireEvent.click(screen.getByRole("button", { name: "common.save" }));
    await waitFor(() => expect(saveMqtt).toHaveBeenCalledTimes(1));

    cleanup();
    getMqttConfig.mockRejectedValue(new Error("offline"));
    render(MqttPage, { props: { mqtt: { connected: true }, onToast } });

    expect(await screen.findByDisplayValue("abcdef")).toBeTruthy();
    expect(screen.queryByText("mqtt.load-error")).toBeNull();
    const save = screen.getByRole("button", { name: "common.save" });
    await waitFor(() => expect(save).not.toHaveAttribute("aria-busy", "true"));
    expect(save).toBeEnabled();
  });

  it("shows load-error when apply poll GET fails on a cold load", async () => {
    resetMqttApplySession();
    getMqttConfig
      .mockResolvedValueOnce(cfg({ applyPending: true }))
      .mockRejectedValue(new Error("offline"));

    render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

    expect(await screen.findByText("mqtt.load-error")).toBeTruthy();
    expect(screen.queryByText("mqtt.loading")).toBeNull();
  });

  it("shows load-error instead of a spinner when cold-load applyPending never clears", async () => {
    resetMqttApplySession();
    vi.useFakeTimers();
    try {
      getMqttConfig.mockResolvedValue(cfg({ applyPending: true }));
      render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

      await Promise.resolve();
      await Promise.resolve();
      expect(screen.getByText("mqtt.loading")).toBeTruthy();

      await vi.advanceTimersByTimeAsync(30_200);
      expect(screen.getByText("mqtt.load-error")).toBeTruthy();
      expect(screen.queryByText("mqtt.loading")).toBeNull();
    } finally {
      vi.useRealTimers();
    }
  });

  it("starts apply poll instead of load-error when cold-load GET is 503 busy", async () => {
    resetMqttApplySession();
    getMqttConfig
      .mockRejectedValueOnce(new ApiHttpError("/api/mqtt", 503, "busy"))
      .mockResolvedValue(cfg({ applyPending: false }));

    render(MqttPage, { props: { mqtt: { connected: true }, onToast: vi.fn() } });

    expect(await screen.findByDisplayValue("mqtt.example.com")).toBeTruthy();
    expect(screen.queryByText("mqtt.load-error")).toBeNull();
  });
});

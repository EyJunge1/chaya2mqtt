import { cleanup, screen, waitFor } from "@testing-library/svelte";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { renderApp } from "../test/renderApp.ts";
import { device } from "./device.svelte.ts";
import DeviceRootHarness from "./DeviceRoot.test.svelte";

const {
  refreshCsrf,
  getDevice,
  getChaya,
  getWifiStatus,
  getMqttStatus,
  getUpdateStatus,
  connectEvents,
} = vi.hoisted(() => ({
  refreshCsrf: vi.fn(),
  getDevice: vi.fn(),
  getChaya: vi.fn(),
  getWifiStatus: vi.fn(),
  getMqttStatus: vi.fn(),
  getUpdateStatus: vi.fn(),
  connectEvents: vi.fn(),
}));

vi.mock("../api/client.ts", () => ({
  api: {
    getDevice: () => getDevice(),
    getChaya: () => getChaya(),
    getWifiStatus: () => getWifiStatus(),
    getMqttStatus: () => getMqttStatus(),
    getUpdateStatus: () => getUpdateStatus(),
  },
  refreshCsrf: () => refreshCsrf(),
}));

vi.mock("../api/sse.ts", () => ({
  connectEvents: (handlers: unknown) => connectEvents(handlers),
}));

describe("DeviceRoot", () => {
  afterEach(() => {
    cleanup();
    device.reset();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    device.reset();
    refreshCsrf.mockResolvedValue(undefined);
    getDevice.mockResolvedValue({
      mode: "sta",
      deviceId: "a1b2c3",
      version: "2026.8.1",
      hostname: "chaya2mqtt",
      batteryMv: 3900,
      batteryPct: 55,
    });
    getChaya.mockResolvedValue({ rx: 3, tx: 1, connected: true });
    getWifiStatus.mockResolvedValue({ connected: false });
    getMqttStatus.mockResolvedValue({ connected: true });
    getUpdateStatus.mockResolvedValue(null);
    connectEvents.mockReturnValue(() => undefined);
  });

  it("boots device state and opens SSE", async () => {
    renderApp(DeviceRootHarness);

    expect(await screen.findByTestId("device-id")).toHaveTextContent("a1b2c3");
    expect(screen.getByTestId("rx")).toHaveTextContent("3");
    await waitFor(() => {
      expect(connectEvents).toHaveBeenCalled();
    });
  });

  it("shows boot error and retries", async () => {
    const user = userEvent.setup();
    getDevice.mockRejectedValueOnce(new Error("offline")).mockResolvedValue({
      mode: "sta",
      deviceId: "a1b2c3",
      version: "2026.8.1",
      hostname: "chaya2mqtt",
      batteryMv: 3900,
      batteryPct: 55,
    });

    renderApp(DeviceRootHarness, { props: { showChrome: true } });

    expect(await screen.findByRole("button", { name: /retry|erneut/i })).toBeTruthy();
    expect(screen.getByTestId("chrome")).toHaveTextContent("simulator");
    await user.click(screen.getByRole("button", { name: /retry|erneut/i }));
    expect(await screen.findByTestId("device-id")).toHaveTextContent("a1b2c3");
    expect(screen.getByTestId("chrome")).toHaveTextContent("simulator");
  });

  it("marks live state from SSE handlers", async () => {
    connectEvents.mockImplementation(
      (handlers: {
        chaya?: (d: { rx: number; tx: number; connected: boolean }) => void;
        error?: () => void;
      }) => {
        queueMicrotask(() => handlers.chaya?.({ rx: 9, tx: 1, connected: true }));
        return () => undefined;
      },
    );

    renderApp(DeviceRootHarness);

    await waitFor(() => {
      expect(screen.getByTestId("live")).toHaveTextContent("live");
      expect(screen.getByTestId("rx")).toHaveTextContent("9");
    });
  });
});

import { cleanup, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { renderApp } from "../test/renderApp";
import { DeviceProvider } from "./DeviceProvider";
import { useDevice } from "./deviceContext";

const refreshCsrf = vi.fn();
const getDevice = vi.fn();
const getChaya = vi.fn();
const getWifiStatus = vi.fn();
const getMqttStatus = vi.fn();
const getUpdateStatus = vi.fn();
const connectEvents = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    getDevice: () => getDevice(),
    getChaya: () => getChaya(),
    getWifiStatus: () => getWifiStatus(),
    getMqttStatus: () => getMqttStatus(),
    getUpdateStatus: () => getUpdateStatus(),
  },
  refreshCsrf: () => refreshCsrf(),
}));

vi.mock("../api/sse", () => ({
  connectEvents: (handlers: unknown) => connectEvents(handlers),
}));

function Probe() {
  const { device, live, chaya } = useDevice();
  return (
    <div>
      <span data-testid="device-id">{device.deviceId}</span>
      <span data-testid="live">{live}</span>
      <span data-testid="rx">{chaya.rx}</span>
    </div>
  );
}

describe("DeviceProvider", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    refreshCsrf.mockResolvedValue(undefined);
    getDevice.mockResolvedValue({
      mode: "sta",
      deviceId: "a1b2c3",
      version: "2026.8.1",
      hostname: "chaya2mqtt",
    });
    getChaya.mockResolvedValue({ rx: 3, tx: 1, connected: true });
    getWifiStatus.mockResolvedValue({ connected: false });
    getMqttStatus.mockResolvedValue({ connected: true });
    getUpdateStatus.mockResolvedValue(null);
    connectEvents.mockReturnValue(() => undefined);
  });

  it("boots device state and opens SSE", async () => {
    renderApp(
      <DeviceProvider>
        <Probe />
      </DeviceProvider>,
    );

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
    });

    renderApp(
      <DeviceProvider chrome={() => <div data-testid="chrome">simulator</div>}>
        <Probe />
      </DeviceProvider>,
    );

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

    renderApp(
      <DeviceProvider>
        <Probe />
      </DeviceProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId("live")).toHaveTextContent("live");
      expect(screen.getByTestId("rx")).toHaveTextContent("9");
    });
  });
});

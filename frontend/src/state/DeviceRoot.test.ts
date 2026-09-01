import { cleanup, screen, waitFor } from "@testing-library/svelte";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { ChayaStatus } from "../api/types.ts";
import { renderApp } from "../test/renderApp.ts";
import { device } from "./device.svelte.ts";
import DeviceRootHarness from "./DeviceRoot.test.svelte";

const { getBootstrap, connectEvents } = vi.hoisted(() => ({
  getBootstrap: vi.fn(),
  connectEvents: vi.fn(),
}));

vi.mock("../api/client.ts", () => ({
  api: {
    getBootstrap: () => getBootstrap(),
  },
}));

vi.mock("../api/sse.ts", () => ({
  connectEvents: (handlers: unknown) => connectEvents(handlers),
}));

const staBootstrap = () => ({
  csrf: { token: "a".repeat(32), expiresInSeconds: 86400 },
  device: {
    mode: "sta" as const,
    deviceId: "a1b2c3",
    version: "2026.8.1",
    hostname: "chaya2mqtt-a1b2c3",
    batteryMv: 3900,
    batteryPct: 55,
  },
  wifi: { connected: false as const },
  chaya: { rx: 3, tx: 1, connected: true, configured: true, paired: true },
  mqtt: { connected: true },
  update: null,
  settings: {
    resetDays: 7,
    lang: "en" as const,
    theme: "light" as const,
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
  },
});

describe("DeviceRoot", () => {
  afterEach(() => {
    cleanup();
    device.reset();
    vi.clearAllMocks();
  });

  beforeEach(() => {
    device.reset();
    getBootstrap.mockResolvedValue(staBootstrap());
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
    getBootstrap.mockRejectedValueOnce(new Error("offline")).mockResolvedValue(staBootstrap());

    renderApp(DeviceRootHarness, { props: { showChrome: true } });

    expect(await screen.findByRole("button", { name: /retry|erneut/i })).toBeTruthy();
    expect(screen.getByTestId("chrome")).toHaveTextContent("simulator");
    await user.click(screen.getByRole("button", { name: /retry|erneut/i }));
    expect(await screen.findByTestId("device-id")).toHaveTextContent("a1b2c3");
    expect(screen.getByTestId("chrome")).toHaveTextContent("simulator");
  });

  it("marks live state from SSE handlers", async () => {
    connectEvents.mockImplementation(
      (handlers: { chaya?: (d: ChayaStatus) => void; error?: () => void }) => {
        queueMicrotask(() =>
          handlers.chaya?.({ rx: 9, tx: 1, connected: true, configured: true, paired: true }),
        );
        return () => undefined;
      },
    );

    renderApp(DeviceRootHarness);

    await waitFor(() => {
      expect(screen.getByTestId("live")).toHaveTextContent("live");
      expect(screen.getByTestId("rx")).toHaveTextContent("9");
    });
  });

  it("reopens SSE after a simulator reboot refresh", async () => {
    renderApp(DeviceRootHarness);
    await waitFor(() => expect(connectEvents).toHaveBeenCalledTimes(1));

    device.refreshSeq += 1;
    await waitFor(() => expect(connectEvents).toHaveBeenCalledTimes(2));
  });
});

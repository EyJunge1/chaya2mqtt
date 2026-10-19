import { cleanup, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { renderApp } from "./test/renderApp.ts";
import { router } from "./nav/router.svelte.ts";
import { device } from "./state/device.svelte.ts";
import App from "./App.svelte";

const { getBootstrap, connectEvents } = vi.hoisted(() => ({
  getBootstrap: vi.fn(),
  connectEvents: vi.fn(),
}));

vi.mock("./api/client.ts", () => ({
  api: {
    getBootstrap: () => getBootstrap(),
  },
}));

vi.mock("./api/sse.ts", () => ({
  connectEvents: (handlers: unknown) => connectEvents(handlers),
}));

const staBootstrap = () => ({
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
    applyPending: false,
  },
});

describe("App", () => {
  afterEach(() => {
    cleanup();
    device.reset();
    router.replace("/");
    vi.clearAllMocks();
  });

  beforeEach(() => {
    device.reset();
    getBootstrap.mockResolvedValue(staBootstrap());
    connectEvents.mockReturnValue(() => undefined);
  });

  it("redirects STA away from /wifi-testing", async () => {
    renderApp(App, { route: "/wifi-testing", language: "en" });

    await waitFor(() => {
      expect(router.pathname).toBe("/");
    });
    expect(screen.queryByText(/Testing connection/i)).toBeNull();
    expect(await screen.findByRole("button", { name: /Send heart/i })).toBeTruthy();
  });
});

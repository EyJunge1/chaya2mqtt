import { cleanup, screen, waitFor } from "@testing-library/svelte";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { renderApp } from "../test/renderApp.ts";
import MockToolbar from "./MockToolbar.svelte";

describe("MockToolbar", () => {
  afterEach(() => {
    cleanup();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  beforeEach(() => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
        const url = String(input);
        if (url.includes("/api/_mock/state")) {
          return new Response(
            JSON.stringify({
              scenario: "sta-connected",
              faults: {},
            }),
            { status: 200 },
          );
        }
        if (url.includes("/api/_mock/scenario")) {
          const body = new URLSearchParams(String(init?.body ?? ""));
          const scenario = body.get("scenario") ?? "sta-connected";
          return new Response(JSON.stringify({ ok: true, scenario }), {
            status: 200,
          });
        }
        if (url.includes("/api/_mock/fault")) {
          const body = new URLSearchParams(String(init?.body ?? ""));
          const fault = body.get("fault") ?? "mqtt";
          const enabled = body.get("enabled") === "1";
          return new Response(
            JSON.stringify({
              ok: true,
              fault,
              enabled,
              faults: enabled ? { [fault]: true } : {},
            }),
            { status: 200 },
          );
        }
        if (url.includes("/api/_mock/reset")) {
          return new Response(JSON.stringify({ ok: true }), { status: 200 });
        }
        return new Response("{}", { status: 404 });
      }),
    );
  });

  it("lists new simulator groups and can switch scenarios", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta", bootError: true } });

    expect(screen.getByText("Simulator · offline")).toBeTruthy();
    expect(screen.getByText("Boot unreachable")).toBeTruthy();
    expect(screen.getByText("SSE reconnecting")).toBeTruthy();
    expect(screen.getByText("Wi-Fi test")).toBeTruthy();
    expect(screen.getByText("Wi-Fi test success")).toBeTruthy();
    expect(screen.getByText("Wi-Fi test failed")).toBeTruthy();
    expect(screen.getByText("MQTT load fail")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Testing page" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Wi-Fi connect" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Wi-Fi abort" })).toBeNull();

    await user.click(screen.getByRole("button", { name: "Boot unreachable" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
  });

  it("toggles a load fault", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "MQTT load fail" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
    expect(await screen.findByRole("button", { name: "MQTT load fail" })).toBeTruthy();
  });

  it("keeps only one load fault active at a time", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);
    const faults: Record<string, boolean> = {};
    let scenario = "sta-connected";

    vi.stubGlobal(
      "fetch",
      vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
        const url = String(input);
        if (url.includes("/api/_mock/state")) {
          return new Response(JSON.stringify({ scenario, faults: {} }), {
            status: 200,
          });
        }
        if (url.includes("/api/_mock/scenario")) {
          const body = new URLSearchParams(String(init?.body ?? ""));
          scenario = body.get("scenario") ?? scenario;
          for (const key of Object.keys(faults)) faults[key] = false;
          return new Response(JSON.stringify({ ok: true, scenario }), { status: 200 });
        }
        if (url.includes("/api/_mock/fault")) {
          const body = new URLSearchParams(String(init?.body ?? ""));
          const fault = body.get("fault") ?? "";
          const enabled = body.get("enabled") === "1";
          if (fault) faults[fault] = enabled;
          return new Response(
            JSON.stringify({ ok: true, fault, enabled, faults: { ...faults } }),
            { status: 200 },
          );
        }
        return new Response("{}", { status: 404 });
      }),
    );

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Verifying" }));
    await waitFor(() => expect(scenario).toBe("update-verifying"));

    await user.click(screen.getByRole("button", { name: "Update load fail" }));
    await waitFor(() => {
      expect(scenario).toBe("sta-connected");
      expect(faults["update-status"]).toBe(true);
    });
    expect(screen.getByRole("button", { name: "Verifying" }).className).not.toMatch(
      /font-semibold/,
    );
  });
});

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

  it("lists section headers collapsed by default", async () => {
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta", bootError: true } });

    expect(screen.getByText("Simulator · offline")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Connection" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "MQTT" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "AP setup" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Load errors" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Boot slow" })).toBeNull();
    expect(screen.queryByRole("button", { name: "MQTT config" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Clear faults" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Reset simulator" })).toBeNull();
  });

  it("expands a section and can switch scenarios", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.getByText("Boot slow")).toBeTruthy();
    expect(screen.getByText("SSE reconnecting")).toBeTruthy();

    await user.click(screen.getByRole("button", { name: "SSE reconnecting" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
  });

  it("toggles a load fault", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Load errors" }));
    await user.click(screen.getByRole("button", { name: "MQTT config" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
    expect(await screen.findByRole("button", { name: "MQTT config" })).toBeTruthy();
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
          return new Response(JSON.stringify({ ok: true, fault, enabled, faults: { ...faults } }), {
            status: 200,
          });
        }
        return new Response("{}", { status: 404 });
      }),
    );

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Update" }));
    await user.click(screen.getByRole("button", { name: "Verifying" }));
    await waitFor(() => expect(scenario).toBe("update-verifying"));

    await user.click(screen.getByRole("button", { name: "Load errors" }));
    await user.click(screen.getByRole("button", { name: "Update status" }));
    await waitFor(() => {
      expect(scenario).toBe("sta-connected");
      expect(faults["update-status"]).toBe(true);
    });
    expect(screen.getByRole("button", { name: "Verifying" }).className).not.toMatch(
      /font-semibold/,
    );
  });

  it("keeps only one action fault active at a time", async () => {
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
          return new Response(JSON.stringify({ ok: true, fault, enabled, faults: { ...faults } }), {
            status: 200,
          });
        }
        return new Response("{}", { status: 404 });
      }),
    );

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Action errors" }));
    await user.click(screen.getByRole("button", { name: "MQTT save" }));
    await waitFor(() => expect(faults["mqtt-save"]).toBe(true));

    await user.click(screen.getByRole("button", { name: "Reboot" }));
    await waitFor(() => {
      expect(faults["mqtt-save"]).toBe(false);
      expect(faults.reboot).toBe(true);
    });
  });

  it("expands and collapses simulator sections", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    expect(screen.queryByRole("button", { name: "Boot slow" })).toBeNull();
    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.getByRole("button", { name: "Boot slow" })).toBeTruthy();
    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.queryByRole("button", { name: "Boot slow" })).toBeNull();
  });
});

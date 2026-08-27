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
          const body = String(init?.body ?? "");
          expect(body).toContain("scenario=boot-unreachable");
          return new Response(JSON.stringify({ ok: true, scenario: "boot-unreachable" }), {
            status: 200,
          });
        }
        if (url.includes("/api/_mock/fault")) {
          return new Response(
            JSON.stringify({ ok: true, fault: "mqtt", enabled: true, faults: { mqtt: true } }),
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
    expect(screen.getByText("Test running")).toBeTruthy();
    expect(screen.getByText("MQTT page")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Wi-Fi test" })).toBeTruthy();

    await user.click(screen.getByRole("button", { name: "Boot unreachable" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
  });

  it("toggles a load fault", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "MQTT page" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
    expect(await screen.findByRole("button", { name: "● MQTT page" })).toBeTruthy();
  });
});

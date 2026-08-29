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
    expect(screen.getByRole("button", { name: "Settings" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "AP setup" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Errors" })).toBeNull();
    expect(screen.queryByRole("button", { name: "SSE reconnecting" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Chaya unreachable" })).toBeNull();
  });

  it("expands a section and can switch scenarios", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.getByText("STA online")).toBeTruthy();
    expect(screen.getByText("SSE reconnecting")).toBeTruthy();
    expect(screen.getByText("Chaya unreachable")).toBeTruthy();

    await user.click(screen.getByRole("button", { name: "SSE reconnecting" }));
    await waitFor(() => expect(onChanged).toHaveBeenCalled());
  });

  it("expands and collapses simulator sections", async () => {
    const user = userEvent.setup();
    const onChanged = vi.fn(async () => undefined);

    renderApp(MockToolbar, { props: { onChanged, mode: "sta" } });

    expect(screen.queryByRole("button", { name: "SSE reconnecting" })).toBeNull();
    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.getByRole("button", { name: "SSE reconnecting" })).toBeTruthy();
    await user.click(screen.getByRole("button", { name: "Connection" }));
    expect(screen.queryByRole("button", { name: "SSE reconnecting" })).toBeNull();
  });
});

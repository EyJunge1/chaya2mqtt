import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import { router } from "../nav/router.svelte.ts";
import WifiTestingPage from "./WifiTestingPage.svelte";

const getWifiConnectStatus = vi.fn();
const commitWifiConnect = vi.fn();
const abortWifiConnect = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    getWifiConnectStatus: () => getWifiConnectStatus(),
    commitWifiConnect: () => commitWifiConnect(),
    abortWifiConnect: () => abortWifiConnect(),
  },
}));

afterEach(() => {
  cleanup();
  router.replace("/");
  vi.clearAllMocks();
});

describe("WifiTestingPage", () => {
  beforeEach(() => {
    setLanguage("en");
    getWifiConnectStatus.mockResolvedValue({ state: "testing", ssid: "HomeNet" });
  });

  it("shows testing status with busy indicator", async () => {
    render(WifiTestingPage, { props: { onToast: vi.fn() } });

    await waitFor(() => {
      expect(screen.getByText("HomeNet")).toBeInTheDocument();
    });
    const status = screen.getByRole("status");
    expect(status).toHaveAttribute("aria-busy", "true");
    expect(status).toHaveTextContent("Testing connection…");
    expect(screen.getByRole("button", { name: "Save & reboot" })).toBeDisabled();
  });

  it("returns to the AP setup root after cancelling", async () => {
    abortWifiConnect.mockResolvedValue({ ok: true });
    router.replace("/wifi-testing");
    render(WifiTestingPage, { props: { onToast: vi.fn() } });

    await fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    await waitFor(() => expect(router.pathname).toBe("/"));
    expect(abortWifiConnect).toHaveBeenCalledOnce();
  });

  it("schedules navigation to the tested station IP after commit", async () => {
    getWifiConnectStatus.mockResolvedValue({ state: "ok", ssid: "HomeNet" });
    commitWifiConnect.mockResolvedValue({
      ok: true,
      message: "committed",
      next: "http://192.168.100.131/",
    });
    const timeoutSpy = vi.spyOn(window, "setTimeout");
    render(WifiTestingPage, { props: { onToast: vi.fn() } });

    const commit = await screen.findByRole("button", { name: "Save & reboot" });
    await waitFor(() => expect(commit).toBeEnabled());
    await fireEvent.click(commit);

    await waitFor(() => expect(commitWifiConnect).toHaveBeenCalledOnce());
    expect(timeoutSpy).toHaveBeenCalledWith(expect.any(Function), 2000);
    timeoutSpy.mockRestore();
  });
});

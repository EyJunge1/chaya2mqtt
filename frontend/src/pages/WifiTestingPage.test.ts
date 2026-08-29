import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import { router } from "../nav/router.svelte.ts";
import WifiTestingPage from "./WifiTestingPage.svelte";

const getWifiConnectStatus = vi.fn();
const commitWifiConnect = vi.fn();
const abortWifiConnect = vi.fn();
const retryWifiConnect = vi.fn();

vi.mock("../api/client", () => ({
  api: {
    getWifiConnectStatus: () => getWifiConnectStatus(),
    commitWifiConnect: () => commitWifiConnect(),
    abortWifiConnect: () => abortWifiConnect(),
    retryWifiConnect: () => retryWifiConnect(),
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
    abortWifiConnect.mockResolvedValue({ ok: true });
  });

  it("shows only the testing panel while connecting", async () => {
    render(WifiTestingPage, { props: { onToast: vi.fn() } });

    await waitFor(() => {
      expect(screen.getByText("HomeNet")).toBeInTheDocument();
    });
    const status = screen.getByRole("status");
    expect(status).toHaveAttribute("aria-busy", "true");
    expect(status).toHaveTextContent("Testing connection…");
    expect(screen.queryByRole("button")).toBeNull();
  });

  it("aborts the connection test when leaving the page", async () => {
    router.replace("/wifi-testing");
    const { unmount } = render(WifiTestingPage, { props: { onToast: vi.fn() } });
    await waitFor(() => expect(screen.getByText("HomeNet")).toBeInTheDocument());
    router.replace("/");
    unmount();
    await waitFor(() => expect(abortWifiConnect).toHaveBeenCalledOnce());
  });

  it("does not abort when remounted while still on the testing route", async () => {
    router.replace("/wifi-testing");
    const { unmount } = render(WifiTestingPage, { props: { onToast: vi.fn() } });
    await waitFor(() => expect(screen.getByText("HomeNet")).toBeInTheDocument());
    unmount();
    expect(abortWifiConnect).not.toHaveBeenCalled();
  });

  it("shows save & reboot only after a successful test", async () => {
    getWifiConnectStatus.mockResolvedValue({ state: "ok", ssid: "HomeNet" });
    commitWifiConnect.mockResolvedValue({
      ok: true,
      message: "committed",
      next: "http://192.168.100.131/",
    });
    const timeoutSpy = vi.spyOn(window, "setTimeout");
    render(WifiTestingPage, { props: { onToast: vi.fn() } });

    const commit = await screen.findByRole("button", { name: "Save & reboot" });
    expect(screen.queryByRole("button", { name: "Try again" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Cancel" })).toBeNull();
    await fireEvent.click(commit);

    await waitFor(() => expect(commitWifiConnect).toHaveBeenCalledOnce());
    expect(timeoutSpy).toHaveBeenCalledWith(expect.any(Function), 2000);
    timeoutSpy.mockRestore();
  });

  it("shows retry on fail and restarts the connection test", async () => {
    getWifiConnectStatus.mockResolvedValue({ state: "fail", ssid: "HomeNet" });
    retryWifiConnect.mockResolvedValue({ ok: true, message: "retrying" });
    const onToast = vi.fn();
    render(WifiTestingPage, { props: { onToast } });

    const retry = await screen.findByRole("button", { name: "Try again" });
    expect(screen.queryByRole("button", { name: "Save & reboot" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Cancel" })).toBeNull();
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Connection failed", "error");
    });
    await fireEvent.click(retry);

    await waitFor(() => expect(retryWifiConnect).toHaveBeenCalledOnce());
    await waitFor(() => {
      expect(screen.getByRole("status")).toHaveTextContent("Testing connection…");
    });
  });

  it("toasts connection failed when the test transitions to fail", async () => {
    getWifiConnectStatus
      .mockResolvedValueOnce({ state: "testing", ssid: "HomeNet" })
      .mockResolvedValue({ state: "fail", ssid: "HomeNet" });
    const onToast = vi.fn();
    render(WifiTestingPage, { props: { onToast } });

    await waitFor(() => {
      expect(screen.getByRole("status")).toHaveTextContent("Testing connection…");
    });
    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Connection failed", "error");
    });
    expect(onToast).toHaveBeenCalledOnce();
  });

  it("toasts connection successful when the test transitions to ok", async () => {
    getWifiConnectStatus
      .mockResolvedValueOnce({ state: "testing", ssid: "HomeNet" })
      .mockResolvedValue({ state: "ok", ssid: "HomeNet" });
    const onToast = vi.fn();
    render(WifiTestingPage, { props: { onToast } });

    await waitFor(() => {
      expect(onToast).toHaveBeenCalledWith("Connection successful", "success");
    });
    expect(await screen.findByRole("button", { name: "Save & reboot" })).toBeTruthy();
  });
});

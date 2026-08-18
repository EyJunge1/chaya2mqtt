import { cleanup, render, screen, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setLanguage } from "../i18n/store.ts";
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
});

import { cleanup, render, screen, waitFor } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { WifiTestingPage } from "./WifiTestingPage";

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
    render(
      <MemoryRouter>
        <I18nProvider>
          <WifiTestingPage onToast={vi.fn()} />
        </I18nProvider>
      </MemoryRouter>,
    );

    await waitFor(() => {
      expect(screen.getByText("HomeNet")).toBeInTheDocument();
    });
    const status = screen.getByRole("status");
    expect(status).toHaveAttribute("aria-busy", "true");
    expect(status).toHaveTextContent("Testing connection…");
    expect(screen.getByRole("button", { name: "Save & reboot" })).toBeDisabled();
  });
});

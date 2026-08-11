import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { afterEach, describe, expect, it } from "vitest";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { Layout } from "./Layout";

function renderLayout(
  path = "/",
  mode: "ap" | "sta" = "sta",
  live: "live" | "reconnecting" = "live",
) {
  return render(
    <MemoryRouter initialEntries={[path]}>
      <I18nProvider>
        <Layout mode={mode} live={live}>
          <div>content</div>
        </Layout>
      </I18nProvider>
    </MemoryRouter>,
  );
}

describe("Layout", () => {
  afterEach(() => {
    cleanup();
    try {
      localStorage?.removeItem("chaya2mqtt-sidebar-collapsed");
      localStorage?.removeItem("chaya2mqtt-settings-nav-open");
    } catch {
      /* ignore */
    }
  });

  it("shows Home and expandable Settings with sublinks", () => {
    setLanguage("en");
    renderLayout("/", "sta");
    expect(screen.getByRole("heading", { name: "Chaya2MQTT" })).toBeInTheDocument();
    expect(screen.getByTestId("mobile-brand-home")).toHaveAttribute("href", "/");
    expect(screen.getAllByText("Home").length).toBeGreaterThan(0);
    expect(screen.getAllByText("Settings").length).toBeGreaterThan(0);
    expect(screen.getAllByText("Wi‑Fi").length).toBeGreaterThan(0);
    expect(screen.getAllByText("MQTT").length).toBeGreaterThan(0);
    expect(screen.queryByText("Pairing")).not.toBeInTheDocument();
    expect(screen.getAllByText("Device").length).toBeGreaterThan(0);
    expect(screen.getAllByText("Update").length).toBeGreaterThan(0);
    expect(screen.queryByText("Diagnostics")).not.toBeInTheDocument();
    expect(screen.queryByText("Live")).not.toBeInTheDocument();
    expect(screen.getAllByLabelText("Language").length).toBeGreaterThan(0);
    expect(screen.getAllByText("EN").length).toBeGreaterThan(0);
    expect(screen.getAllByLabelText("GitHub project").length).toBeGreaterThan(0);
    expect(screen.getByText("content")).toBeInTheDocument();
  });

  it("keeps home and shows a back control on settings child pages", () => {
    setLanguage("en");
    renderLayout("/settings/device", "sta");
    expect(screen.getByTestId("mobile-brand-home")).toHaveAttribute("href", "/");
    expect(screen.getByTestId("settings-back")).toHaveAttribute("href", "/settings");
    expect(screen.getByRole("heading", { name: "Device" })).toBeInTheDocument();
  });

  it("can collapse the Settings group", () => {
    setLanguage("en");
    renderLayout("/", "sta");
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(screen.queryByTestId("settings-nav-group")).not.toBeInTheDocument();
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(screen.getByTestId("settings-nav-group")).toBeInTheDocument();
    expect(screen.getAllByText("MQTT").length).toBeGreaterThan(0);
  });

  it("keeps the Settings link size and background stable when closing a child group", () => {
    setLanguage("en");
    renderLayout("/settings/device", "sta");
    const settingsLink = screen.getByTestId("settings-nav-link");

    expect(settingsLink).toHaveClass("text-sm");
    expect(settingsLink).not.toHaveClass("bg-accent/15");
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(settingsLink).toHaveClass("text-sm");
    expect(settingsLink).not.toHaveClass("bg-accent/15");
  });

  it("marks only the Settings overview link as active", () => {
    setLanguage("en");
    renderLayout("/settings", "sta");
    expect(screen.getByTestId("settings-nav-link")).toHaveClass("bg-accent/15");
  });

  it("shows reconnect banner only when live stream drops", () => {
    setLanguage("en");
    renderLayout("/", "sta", "reconnecting");
    expect(screen.getByText("Live connection interrupted — reconnecting…")).toBeInTheDocument();
  });

  it("shows only Wi-Fi in AP mode", () => {
    setLanguage("en");
    renderLayout("/", "ap");
    expect(screen.getAllByText("Wi‑Fi").length).toBeGreaterThan(0);
    expect(screen.queryByText("Home")).not.toBeInTheDocument();
    expect(screen.queryByText("MQTT")).not.toBeInTheDocument();
    expect(screen.queryByText("Update")).not.toBeInTheDocument();
  });

  it("collapses the desktop sidebar to icons only", () => {
    setLanguage("en");
    renderLayout("/", "sta");
    const toggle = screen.getByTestId("sidebar-collapse-toggle");
    expect(toggle).toHaveAttribute("aria-expanded", "true");
    expect(screen.getByTestId("settings-nav-group")).toBeInTheDocument();
    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute("aria-expanded", "false");
    expect(screen.queryByTestId("settings-nav-group")).not.toBeInTheDocument();
  });

  it("navigates to Settings from the collapsed sidebar without expanding it", () => {
    setLanguage("en");
    renderLayout("/", "sta");
    const collapseToggle = screen.getByTestId("sidebar-collapse-toggle");
    fireEvent.click(collapseToggle);
    expect(collapseToggle).toHaveAttribute("aria-expanded", "false");
    expect(screen.queryByTestId("settings-nav-toggle")).not.toBeInTheDocument();

    fireEvent.click(screen.getByTestId("settings-nav-link"));
    expect(collapseToggle).toHaveAttribute("aria-expanded", "false");
    expect(screen.queryByTestId("settings-nav-group")).not.toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Settings" })).toBeInTheDocument();
  });
});

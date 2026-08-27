import { cleanup, fireEvent, screen } from "@testing-library/svelte";
import { afterEach, describe, expect, it } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import { renderApp } from "../test/renderApp.ts";
import LayoutHarness from "./Layout.test.svelte";

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
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "sta" } });
    expect(screen.getByRole("heading", { name: "Home" })).toBeInTheDocument();
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
    renderApp(LayoutHarness, { route: "/settings/device", language: "en", props: { mode: "sta" } });
    expect(screen.getByTestId("mobile-brand-home")).toHaveAttribute("href", "/");
    expect(screen.getByTestId("settings-back")).toHaveAttribute("href", "/settings");
    expect(screen.getByTestId("settings-back-link")).toHaveAttribute("href", "/settings");
    expect(screen.getByLabelText("Back to settings")).toBeInTheDocument();
    expect(screen.getByText("Back to settings")).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Device" })).toBeInTheDocument();
  });

  it("can collapse the Settings group", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "sta" } });
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(screen.queryByTestId("settings-nav-group")).not.toBeInTheDocument();
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(screen.getByTestId("settings-nav-group")).toBeInTheDocument();
    expect(screen.getAllByText("MQTT").length).toBeGreaterThan(0);
  });

  it("keeps the Settings link size and background stable when closing a child group", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/settings/device", language: "en", props: { mode: "sta" } });
    const settingsLink = screen.getByTestId("settings-nav-link");

    expect(settingsLink).toHaveClass("text-sm");
    expect(settingsLink).not.toHaveClass("bg-accent/15");
    fireEvent.click(screen.getByTestId("settings-nav-toggle"));
    expect(settingsLink).toHaveClass("text-sm");
    expect(settingsLink).not.toHaveClass("bg-accent/15");
  });

  it("marks only the Settings overview link as active", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/settings", language: "en", props: { mode: "sta" } });
    expect(screen.getByTestId("settings-nav-link")).toHaveClass("bg-accent/15");
  });

  it("shows reconnect banner only when live stream drops", () => {
    setLanguage("en");
    renderApp(LayoutHarness, {
      route: "/",
      language: "en",
      props: { mode: "sta", live: "reconnecting" },
    });
    expect(screen.getByText("Live connection interrupted — reconnecting…")).toBeInTheDocument();
  });

  it("hides sidebar chrome and titles setup with the app name", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "ap" } });
    expect(screen.getByRole("heading", { name: "Chaya2MQTT" })).toBeInTheDocument();
    expect(screen.getByTestId("setup-brand-heart")).not.toHaveAttribute("href");
    expect(screen.queryByTestId("mobile-brand-home")).not.toBeInTheDocument();
    expect(screen.queryByTestId("sidebar-collapse-toggle")).not.toBeInTheDocument();
    expect(screen.queryByRole("navigation", { name: "Main navigation" })).not.toBeInTheDocument();
    expect(screen.queryByText("Home")).not.toBeInTheDocument();
    expect(screen.queryByText("MQTT")).not.toBeInTheDocument();
    expect(screen.queryByText("Update")).not.toBeInTheDocument();
    expect(screen.getAllByLabelText("Language").length).toBeGreaterThan(0);
    expect(screen.getAllByLabelText("GitHub project").length).toBeGreaterThan(0);
    expect(screen.getByTestId("header-bar")).toHaveClass("max-w-3xl");
  });

  it("frosts the header after the content is scrolled", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "sta" } });
    const header = screen.getByTestId("app-header");
    expect(header).toHaveClass("bg-bg/95");
    expect(header).toHaveClass("border-border");
    expect(header).not.toHaveClass("backdrop-blur-xl");

    const scroller = screen.getByTestId("content-scroll");
    Object.defineProperty(scroller, "scrollTop", { configurable: true, value: 24 });
    fireEvent.scroll(scroller);

    expect(header).toHaveClass("backdrop-blur-xl");
    expect(header).toHaveClass("bg-bg/55");
  });

  it("collapses the desktop sidebar to icons only", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "sta" } });
    const toggle = screen.getByTestId("sidebar-collapse-toggle");
    expect(toggle).toHaveAttribute("aria-expanded", "true");
    expect(screen.getByTestId("settings-nav-group")).toBeInTheDocument();
    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute("aria-expanded", "false");
    expect(screen.queryByTestId("settings-nav-group")).not.toBeInTheDocument();
  });

  it("navigates to Settings from the collapsed sidebar without expanding it", () => {
    setLanguage("en");
    renderApp(LayoutHarness, { route: "/", language: "en", props: { mode: "sta" } });
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

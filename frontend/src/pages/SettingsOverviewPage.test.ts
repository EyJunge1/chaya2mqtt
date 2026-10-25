import { cleanup, render, screen } from "@testing-library/svelte";
import { afterEach, describe, expect, it } from "vitest";
import { setLanguage } from "../i18n/store.ts";
import SettingsOverviewPage from "./SettingsOverviewPage.svelte";

afterEach(() => {
  cleanup();
});

describe("SettingsOverviewPage", () => {
  it("shows settings cards with subtitles", () => {
    setLanguage("en");
    render(SettingsOverviewPage);

    expect(screen.getByText("Wi‑Fi")).toBeInTheDocument();
    expect(screen.getByText("Network & status")).toBeInTheDocument();
    expect(screen.getByText("MQTT")).toBeInTheDocument();
    expect(screen.getByText("Broker & pairing")).toBeInTheDocument();
    expect(screen.getByText("Device")).toBeInTheDocument();
    expect(screen.getByText("Update")).toBeInTheDocument();
  });
});

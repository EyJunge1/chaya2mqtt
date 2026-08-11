import { cleanup, render, screen } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { afterEach, describe, expect, it } from "vitest";
import { I18nProvider } from "../i18n/I18nProvider";
import { setLanguage } from "../i18n/store";
import { SettingsOverviewPage } from "./SettingsOverviewPage";

afterEach(() => {
  cleanup();
});

describe("SettingsOverviewPage", () => {
  it("shows settings cards with subtitles", () => {
    setLanguage("en");
    render(
      <MemoryRouter>
        <I18nProvider>
          <SettingsOverviewPage />
        </I18nProvider>
      </MemoryRouter>,
    );

    expect(screen.getByText("Wi‑Fi")).toBeInTheDocument();
    expect(screen.getByText("Network & status")).toBeInTheDocument();
    expect(screen.getByText("MQTT")).toBeInTheDocument();
    expect(screen.getByText("Broker & pairing")).toBeInTheDocument();
    expect(screen.getByText("Device")).toBeInTheDocument();
    expect(screen.getByText("Update")).toBeInTheDocument();
  });
});

import { afterEach, describe, expect, it } from "vitest";
import { cycleLanguage, getLanguage, setLanguage, t } from "./store";
import { translations } from "./translations";

describe("i18n", () => {
  afterEach(() => {
    setLanguage("en");
  });

  it("defaults to English and switches languages", () => {
    setLanguage("en");
    expect(t("common.save")).toBe(translations.en["common.save"]);
    setLanguage("de");
    expect(t("common.save")).toBe(translations.de["common.save"]);
  });

  it("cycles language between en and de", () => {
    setLanguage("en");
    expect(cycleLanguage()).toBe("de");
    expect(getLanguage()).toBe("de");
    expect(cycleLanguage()).toBe("en");
    expect(getLanguage()).toBe("en");
  });

  it("keeps de and en key sets in sync", () => {
    const deKeys = Object.keys(translations.de).sort();
    const enKeys = Object.keys(translations.en).sort();
    expect(enKeys).toEqual(deKeys);
  });
});

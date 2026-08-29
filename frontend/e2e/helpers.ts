import type { APIRequestContext, Page } from "@playwright/test";
import type { MockScenario } from "../mock/deviceState.ts";

export type { MockScenario };

export async function resetMock(
  request: APIRequestContext,
  scenario: MockScenario = "sta-connected",
) {
  const res = await request.post("/api/_mock/scenario", {
    form: { scenario },
  });
  if (!res.ok()) {
    throw new Error(`mock scenario failed: ${res.status()} ${await res.text()}`);
  }
}

export async function setMockFault(request: APIRequestContext, fault: string, enabled = true) {
  const res = await request.post("/api/_mock/fault", {
    form: { fault, enabled: enabled ? "1" : "0" },
  });
  if (!res.ok()) {
    throw new Error(`mock fault failed: ${res.status()} ${await res.text()}`);
  }
}

export async function waitForAppReady(page: Page) {
  await page.goto("/");
  await page.getByRole("banner").waitFor();
}

/** Field wraps InfoTip buttons inside <label>, so getByLabel is unreliable. */
export function fieldInput(page: Page, label: string) {
  return page
    .locator("label")
    .filter({ hasText: label })
    .locator("input, textarea, select")
    .first();
}

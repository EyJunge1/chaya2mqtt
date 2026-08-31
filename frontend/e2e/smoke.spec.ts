import { expect, test } from "@playwright/test";
import { fieldInput, resetMock, waitForAppReady } from "./helpers";

test.beforeEach(async ({ page, request }) => {
  await page.addInitScript(() => {
    window.localStorage.setItem("chaya2mqtt.lang", "en");
    window.localStorage.setItem("chaya2mqtt.theme", "light");
  });
  await resetMock(request, "sta-connected");
});

test("app boots and navigates @smoke", async ({ page }) => {
  await waitForAppReady(page);
  await expect(page.getByTestId("dashboard-hearts")).toBeVisible();
  await page.getByRole("link", { name: "MQTT" }).first().click();
  await expect(page).toHaveURL(/\/mqtt$/);
  await expect(page.getByText("a1b2c3").first()).toBeVisible();
  await page.getByRole("link", { name: "Settings" }).first().click();
  await expect(page).toHaveURL(/\/settings$/);
});

test("mqtt save and unpair @smoke", async ({ page, request }) => {
  await resetMock(request, "sta-mqtt-unpaired");
  await waitForAppReady(page);
  await page.goto("/mqtt");
  await expect(fieldInput(page, "Broker")).toHaveValue(/.+/);

  await fieldInput(page, "Partner ID").fill("abcdef");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).first().click();
  await expect(page.getByText(/Saved\. MQTT is reconnecting/i)).toBeVisible();

  await page.getByRole("main").getByRole("button", { name: "Unpair", exact: true }).click();
  await expect(page.getByText(/Saved\. MQTT is reconnecting/i)).toBeVisible();
  await expect(fieldInput(page, "Partner ID")).toHaveValue("");
});

test("wifi test then commit in AP mode", async ({ page, request }) => {
  await resetMock(request, "ap-setup");
  await waitForAppReady(page);
  await expect(fieldInput(page, "SSID")).toBeVisible();
  await fieldInput(page, "SSID").fill("TestNet");
  await fieldInput(page, "Password").fill("secret123");
  await page.getByRole("button", { name: "Test & connect" }).click();
  await expect(page).toHaveURL(/\/wifi-testing$/);
  await expect(page.getByRole("button", { name: "Save & reboot" })).toBeEnabled({
    timeout: 15_000,
  });
  await page.getByRole("button", { name: "Save & reboot" }).click();
  await expect(page).toHaveURL(/\/$/);
});

test("diagnostics live via SSE counters @smoke", async ({ page }) => {
  await waitForAppReady(page);
  await expect(page.getByTestId("dashboard-hearts")).toBeVisible();
  await expect(page.getByTestId("dashboard-rx-count")).toBeVisible();
  await expect(page.getByTestId("dashboard-tx-count")).toBeVisible();
});

test("ota check success and error paths", async ({ page, request }) => {
  await resetMock(request, "update-available");
  await waitForAppReady(page);
  await page.goto("/update");
  await expect(page.getByTestId("update-status-phase")).toBeVisible();
  await page.getByRole("button", { name: /Check for updates/i }).click();
  await expect(page.getByTestId("update-status-phase")).toBeVisible();

  await resetMock(request, "update-error");
  await page.reload();
  await waitForAppReady(page);
  await page.goto("/update");
  await expect(
    page.getByRole("alert").filter({ hasText: /Update failed|Update fehlgeschlagen/i }),
  ).toBeVisible();
});

test("device load fault recovers via simulator reset @smoke", async ({ page, request }) => {
  await resetMock(request, "device-unreachable");
  await page.goto("/");
  await expect(page.getByText(/Could not connect to the device/i)).toBeVisible();
  await expect(page.getByText("Simulator", { exact: true })).toBeVisible();

  await resetMock(request, "sta-connected");
  await page.getByRole("button", { name: /Try again|Erneut/i }).click();
  await expect(page.getByRole("navigation", { name: /main navigation/i })).toBeVisible();
});

test("mqtt page load fault shows retry block @smoke", async ({ page, request }) => {
  await resetMock(request, "mqtt-load-fail");
  await waitForAppReady(page);
  await page.goto("/mqtt");
  await expect(page.getByText("Could not load the broker configuration.")).toBeVisible();
  await expect(page.getByRole("main").getByRole("button", { name: "Try again" })).toBeVisible();
});

test("sse disconnect shows reconnecting banner @smoke", async ({ page, request }) => {
  await resetMock(request, "sse-disconnected");
  await waitForAppReady(page);
  await expect(page.getByText(/Live connection interrupted/i)).toBeVisible();
});

test("wifi scan empty state in AP setup @smoke", async ({ page, request }) => {
  await resetMock(request, "wifi-scan-empty");
  await waitForAppReady(page);
  await expect(page.getByText("No networks found")).toBeVisible({ timeout: 10_000 });
});

test("settings save fault shows toast @smoke", async ({ page, request }) => {
  await resetMock(request, "settings-save-fail");
  await waitForAppReady(page);
  await page.goto("/settings/device");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).first().click();
  await expect(page.getByText("Save failed")).toBeVisible();
});

test("settings save success shows toast @smoke", async ({ page }) => {
  await waitForAppReady(page);
  await page.goto("/settings/device");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).first().click();
  await expect(page.getByText("Saved", { exact: true })).toBeVisible();
});

test("heart busy toast from simulator scenario @smoke", async ({ page, request }) => {
  await resetMock(request, "heart-busy");
  await waitForAppReady(page);
  await page.getByRole("button", { name: /Send heart/i }).click();
  await expect(page.getByText(/Heart still sending/i)).toBeVisible();
});

test("battery critical icon from simulator scenario @smoke", async ({ page, request }) => {
  await resetMock(request, "battery-critical");
  await waitForAppReady(page);
  await expect(page.getByLabel(/Battery: 8%/i)).toBeVisible();
  await expect(page.getByText("8%")).toBeVisible();
});

test("ota install confirm starts downloading", async ({ page, request }) => {
  await resetMock(request, "update-available");
  await waitForAppReady(page);
  await page.goto("/update");
  await page.getByRole("button", { name: /Check for updates/i }).click();
  await expect(
    page.getByRole("button", { name: /Install update|Update installieren/i }),
  ).toBeVisible({
    timeout: 10_000,
  });
  await page.getByRole("button", { name: /Install update|Update installieren/i }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page
    .getByRole("dialog")
    .getByRole("button", { name: /Install update|Update installieren/i })
    .click();
  await expect(page.getByText(/Downloading|Herunterladen/i).first()).toBeVisible({
    timeout: 10_000,
  });
});

test("factory reset confirm posts and enters AP setup", async ({ page, request }) => {
  await resetMock(request, "sta-connected");
  await waitForAppReady(page);
  await page.goto("/settings/device");
  const resetPromise = page.waitForResponse(
    (r) => r.url().includes("/api/factory-reset") && r.request().method() === "POST",
  );
  await page
    .getByRole("main")
    .getByRole("button", { name: /Delete everything|Alles löschen/i })
    .click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page
    .getByRole("dialog")
    .getByRole("button", { name: /Delete everything|Alles löschen/i })
    .click();
  const res = await resetPromise;
  expect(res.status()).toBe(202);
  // Mock switches to ap-setup after reset (FE-14).
  await expect(page.getByRole("button", { name: /Test & connect/i })).toBeVisible({
    timeout: 15_000,
  });
});

test("settings nvsOk false shows save-failed toast", async ({ page, request }) => {
  await resetMock(request, "settings-nvs-fail");
  await waitForAppReady(page);
  await page.goto("/settings/device");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).first().click();
  await expect(page.getByText("Save failed")).toBeVisible();
});

test("wifi commit returns absolute next URL", async ({ request }) => {
  await resetMock(request, "ap-test-ok");
  const csrf = await request.get("/api/csrf");
  const csrfJson = (await csrf.json()) as { token: string };
  const commit = await request.post("/api/wifi/connect-commit", {
    form: { csrf_token: csrfJson.token },
  });
  const commitBody = (await commit.json()) as { ok: boolean; next?: string };
  expect(commitBody.ok).toBeTruthy();
  expect(commitBody.next).toMatch(/^http:\/\/\d+\.\d+\.\d+\.\d+\//);
});

test("csrf rejection on wifi commit without token", async ({ request }) => {
  await resetMock(request, "ap-test-ok");
  const res = await request.post("/api/wifi/connect-commit", {
    form: { csrf_token: "deadbeef" },
  });
  expect(res.status()).toBe(403);
  const body = (await res.json()) as { ok: boolean; error?: string };
  expect(body.ok).toBeFalsy();
  expect(body.error).toBe("csrf");
});

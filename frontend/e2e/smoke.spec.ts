import { expect, test } from "@playwright/test";
import { fieldInput, resetMock, setMockFault, waitForAppReady } from "./helpers";

test.beforeEach(async ({ page, request }) => {
  await page.addInitScript(() => {
    window.localStorage.setItem("chaya2mqtt.lang", "en");
    window.localStorage.setItem("chaya2mqtt.theme", "light");
  });
  await resetMock(request, "sta-connected");
});

test("app boots and navigates @smoke", async ({ page }) => {
  await waitForAppReady(page);
  await expect(page.getByText("Hearts")).toBeVisible();
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
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByText(/Saved\. MQTT is reconnecting/i)).toBeVisible();

  await page.getByRole("main").getByRole("button", { name: "Unpair", exact: true }).click();
  await expect(page.getByText(/Saved\. MQTT is reconnecting/i)).toBeVisible();
  await expect(fieldInput(page, "Partner ID")).toHaveValue("");
});

test("wifi test then commit in AP mode", async ({ page, request }) => {
  await resetMock(request, "ap-setup");
  await waitForAppReady(page);
  // AP dashboard shows the open SoftAP connection data + Wi‑Fi setup.
  await expect(
    page.getByText(/Connect to this device first|Zuerst mit diesem Gerät verbinden/i),
  ).toBeVisible();
  await expect(page.getByText(/open Wi.Fi|offenen WLAN/i)).toContainText("Chaya2MQTT");
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
  await expect(page.getByText("Hearts")).toBeVisible();
  await expect(page.getByText(/\d+/).first()).toBeVisible();
});

test("ota check success and error paths", async ({ page, request }) => {
  await resetMock(request, "update-available");
  await waitForAppReady(page);
  await page.goto("/update");
  await expect(page.getByRole("button", { name: /Check for updates/i })).toBeVisible();
  await page.getByRole("button", { name: /Check for updates/i }).click();
  await expect(page.getByText(/available|update/i).first()).toBeVisible();

  await resetMock(request, "update-error");
  await page.reload();
  await waitForAppReady(page);
  await page.goto("/update");
  await expect(page.getByText(/error|failed|fehl/i).first()).toBeVisible();
});

test("boot unreachable recovers via simulator reset @smoke", async ({ page, request }) => {
  await resetMock(request, "boot-unreachable");
  await page.goto("/");
  await expect(page.getByText(/Could not connect to the device/i)).toBeVisible();
  await expect(page.getByRole("button", { name: "Simulator · offline" })).toBeVisible();

  await resetMock(request, "sta-connected");
  await page.getByRole("button", { name: /Try again|Erneut/i }).click();
  await expect(page.getByRole("navigation", { name: /main navigation/i })).toBeVisible();
});

test("mqtt page load fault shows retry block @smoke", async ({ page, request }) => {
  await setMockFault(request, "mqtt", true);
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
  await setMockFault(request, "settings-save", true);
  await waitForAppReady(page);
  await page.goto("/settings/device");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByText("Save failed")).toBeVisible();
});

test("settings save success shows toast @smoke", async ({ page }) => {
  await waitForAppReady(page);
  await page.goto("/settings/device");
  await page.getByRole("main").getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.getByText("Saved", { exact: true })).toBeVisible();
});

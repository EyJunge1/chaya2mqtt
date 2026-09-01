import { existsSync, readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { API_GET_PATHS, API_POST_PATHS, SSE_EVENT_TYPES, SPA_UI_PATHS } from "./contract";
import { api } from "./client";

function findRoot(): string {
  let dir = process.cwd();
  for (let i = 0; i < 6; i++) {
    if (existsSync(resolve(dir, "docs/WEB_ADMIN.md")) && existsSync(resolve(dir, "src/web"))) {
      return dir;
    }
    dir = resolve(dir, "..");
  }
  throw new Error("repository root not found from " + process.cwd());
}

const root = findRoot();

function read(rel: string): string {
  return readFileSync(resolve(root, rel), "utf8");
}

/** Concatenate split firmware API route sources (admin_routes_api*.cpp). */
function readFirmwareApiRoutes(): string {
  const dir = resolve(root, "src/web/routes");
  return readdirSync(dir)
    .filter((name) => name.startsWith("admin_routes_api") && name.endsWith(".cpp"))
    .sort()
    .map((name) => readFileSync(resolve(dir, name), "utf8"))
    .join("\n");
}

describe("api contract", () => {
  it("exposes expected client methods", () => {
    expect(typeof api.getBootstrap).toBe("function");
    expect(typeof api.getDevice).toBe("function");
    expect(typeof api.getUpdateStatus).toBe("function");
    expect(typeof api.checkUpdate).toBe("function");
    expect(typeof api.installUpdate).toBe("function");
    expect(typeof api.sendChaya).toBe("function");
    expect(typeof api.startWifiScan).toBe("function");
    expect(typeof api.scanWifi).toBe("function");
  });

  it("keeps mock plugin aligned with GET/POST paths", () => {
    const mock = read("frontend/mock/mockPlugin.ts");
    for (const path of [...API_GET_PATHS, ...API_POST_PATHS]) {
      expect(mock).toContain(`"${path}"`);
    }
    expect(mock).toContain('"/events"');
  });

  it("keeps firmware API routes aligned", () => {
    const apiCpp = readFirmwareApiRoutes();
    for (const path of [...API_GET_PATHS, ...API_POST_PATHS]) {
      expect(apiCpp).toContain(`"${path}"`);
    }
  });

  it("keeps the device-specific STA hostname aligned", () => {
    const firmware = readFirmwareApiRoutes();
    const mock = read("frontend/mock/deviceState.ts");
    const openapi = read("docs/openapi.yaml");
    expect(firmware).toContain("formatDeviceStaHostname");
    expect(mock).toContain("`chaya2mqtt-${deviceId}`");
    expect(openapi).toContain("http://chaya2mqtt-{deviceId}.local");
  });

  it("documents SSE event types and SPA paths", () => {
    const docs = read("docs/WEB_ADMIN.md");
    for (const event of SSE_EVENT_TYPES) {
      expect(docs).toContain(event);
    }
    for (const path of SPA_UI_PATHS) {
      expect(docs).toContain(path);
    }
  });

  it("keeps OpenAPI paths aligned with the contract", () => {
    const openapi = read("docs/openapi.yaml");
    for (const path of [...API_GET_PATHS, ...API_POST_PATHS]) {
      expect(openapi).toContain(`  ${path}:`);
    }
  });

  it("keeps Wi-Fi continuation URLs aligned", () => {
    const firmware = readFirmwareApiRoutes();
    const mock = read("frontend/mock/mockPlugin.ts");
    const openapi = read("docs/openapi.yaml");
    expect(firmware).toContain("/wifi-testing");
    expect(mock).toContain("next:");
    expect(openapi).toContain("        next:");
  });

  it("keeps MQTT config fields aligned across client, mock and OpenAPI", () => {
    const client = read("frontend/src/api/client.ts");
    const mock = read("frontend/mock/mockPlugin.ts");
    const openapi = read("docs/openapi.yaml");
    const firmware = readFirmwareApiRoutes();
    for (const field of [
      "mqtt_server",
      "mqtt_port",
      "mqtt_tls",
      "mqtt_user",
      "mqtt_pass",
      "partner_id",
    ]) {
      expect(client).toContain(field);
      expect(mock).toContain(field);
      expect(openapi).toContain(field);
      expect(firmware).toContain(field);
    }
  });

  it("documents CSRF in OpenAPI and SSE in AsyncAPI", () => {
    const openapi = read("docs/openapi.yaml");
    const asyncapi = read("docs/asyncapi.yaml");
    expect(openapi).toContain("/api/csrf");
    expect(openapi).toContain("X-CSRF-Token");
    expect(asyncapi).toContain("/events");
    expect(asyncapi).toContain("chaya2mqtt-{deviceId}.local");
  });

  it("keeps mutating client posts expecting X-CSRF-Token", () => {
    const client = read("frontend/src/api/client.ts");
    expect(client).toContain("X-CSRF-Token");
    expect(client).toContain("/api/csrf");
  });
});

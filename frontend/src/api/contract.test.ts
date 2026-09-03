import { existsSync, readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { api } from "./client";
import { KNOWN_ROUTES } from "../nav/router.svelte.ts";

function findRoot(): string {
  let dir = process.cwd();
  for (let i = 0; i < 6; i++) {
    if (existsSync(resolve(dir, "docs/openapi.yaml")) && existsSync(resolve(dir, "src/web"))) {
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

type HttpMethod = "get" | "post" | "put" | "patch" | "delete";

type OpenApiOperation = {
  method: HttpMethod;
  path: string;
  operationId?: string;
  protection?: string;
};

const yamlIndent = (n: number) => " ".repeat(n);

/** Path/method surface from docs/openapi.yaml — the REST source of truth. */
function parseOpenApiOperations(yaml: string): OpenApiOperation[] {
  const ops: OpenApiOperation[] = [];
  let inPaths = false;
  let path: string | null = null;
  let current: OpenApiOperation | null = null;

  const flush = () => {
    if (current) ops.push(current);
    current = null;
  };

  for (const raw of yaml.split(/\r?\n/)) {
    if (raw === "paths:") {
      inPaths = true;
      continue;
    }
    if (inPaths && /^[A-Za-z]/.test(raw)) {
      flush();
      break;
    }
    if (!inPaths) continue;

    const pathMatch = raw.match(new RegExp(`^${yamlIndent(2)}(/[^:]+):$`));
    if (pathMatch) {
      flush();
      path = pathMatch[1] ?? null;
      continue;
    }

    const methodMatch = raw.match(new RegExp(`^${yamlIndent(4)}(get|post|put|patch|delete):$`));
    if (methodMatch && path) {
      flush();
      current = { method: methodMatch[1] as HttpMethod, path };
      continue;
    }

    if (!current) continue;

    const idMatch = raw.match(new RegExp(`^${yamlIndent(6)}operationId:\\s+(\\S+)$`));
    if (idMatch?.[1]) {
      current.operationId = idMatch[1];
      continue;
    }

    const protMatch = raw.match(new RegExp(`^${yamlIndent(6)}x-protection:\\s+(.+)$`));
    if (protMatch?.[1]) {
      current.protection = protMatch[1].trim();
    }
  }
  flush();
  return ops;
}

/** Event names from docs/asyncapi.yaml — the SSE source of truth. */
function parseAsyncApiEventTypes(yaml: string): string[] {
  const events: string[] = [];
  let inChannelMessages = false;
  for (const raw of yaml.split(/\r?\n/)) {
    if (!inChannelMessages) {
      if (raw === `${yamlIndent(4)}messages:`) {
        inChannelMessages = true;
      }
      continue;
    }
    const eventMatch = raw.match(new RegExp(`^${yamlIndent(6)}([A-Za-z][A-Za-z0-9_-]*):$`));
    if (eventMatch?.[1]) {
      events.push(eventMatch[1]);
      continue;
    }
    if (raw.length === 0 || raw.startsWith(yamlIndent(6))) {
      continue;
    }
    break;
  }
  return events;
}

function quotedApiPaths(source: string): string[] {
  const paths = new Set<string>();
  for (const match of source.matchAll(/"(\/api\/(?!_mock)[^"]+)"/g)) {
    if (match[1]) paths.add(match[1]);
  }
  return [...paths].sort();
}

const openapi = read("docs/openapi.yaml");
const asyncapi = read("docs/asyncapi.yaml");
const operations = parseOpenApiOperations(openapi);
const sseEvents = parseAsyncApiEventTypes(asyncapi);
const apiPaths = [...new Set(operations.map((op) => op.path))].sort();
const getPaths = operations.filter((op) => op.method === "get").map((op) => op.path);
const postPaths = operations.filter((op) => op.method === "post").map((op) => op.path);

describe("api contract", () => {
  it("reads REST and SSE surfaces from the docs YAML", () => {
    expect(operations.length).toBeGreaterThan(0);
    expect(getPaths.length).toBeGreaterThan(0);
    expect(postPaths.length).toBeGreaterThan(0);
    expect(sseEvents.length).toBeGreaterThan(0);
    for (const op of operations) {
      expect(op.operationId, `${op.method} ${op.path}`).toBeTruthy();
      expect(op.protection, `${op.method} ${op.path}`).toBeTruthy();
    }
  });

  it("exposes expected client methods", () => {
    expect(typeof api.getBootstrap).toBe("function");
    expect(typeof api.getUpdateStatus).toBe("function");
    expect(typeof api.checkUpdate).toBe("function");
    expect(typeof api.installUpdate).toBe("function");
    expect(typeof api.sendChaya).toBe("function");
    expect(typeof api.startWifiScan).toBe("function");
    expect(typeof api.scanWifi).toBe("function");
  });

  it("keeps the client, mock and firmware paths aligned with OpenAPI", () => {
    const mock = read("frontend/mock/mockPlugin.ts");
    const firmware = readFirmwareApiRoutes();
    const client = read("frontend/src/api/client.ts");
    expect(quotedApiPaths(mock)).toEqual(apiPaths);
    expect(quotedApiPaths(firmware)).toEqual(apiPaths);
    expect(quotedApiPaths(client)).toEqual(apiPaths);
    expect(mock).toContain('"/events"');
  });

  it("keeps the device-specific STA hostname aligned", () => {
    const firmware = readFirmwareApiRoutes();
    const mock = read("frontend/mock/deviceState.ts");
    expect(firmware).toContain("formatDeviceStaHostname");
    expect(mock).toContain("`chaya2mqtt-${deviceId}`");
    expect(openapi).toContain("http://chaya2mqtt-{deviceId}.local");
  });

  it("keeps SSE events aligned with AsyncAPI", () => {
    const firmwareEvents = read("src/web/events.cpp");
    const mock = read("frontend/mock/mockPlugin.ts");
    const mockState = read("frontend/mock/deviceState.ts");
    for (const event of sseEvents) {
      expect(firmwareEvents).toContain(`"${event}"`);
      expect(mock).toContain(`write("${event}"`);
      expect(mockState).toContain(`emit("${event}"`);
    }
    expect(asyncapi).toContain("/events");
    expect(asyncapi).toContain("chaya2mqtt-{deviceId}.local");
  });

  it("documents SPA routes and points at the YAML contracts", () => {
    const docs = read("docs/WEB_ADMIN.md");
    for (const path of KNOWN_ROUTES) {
      expect(docs).toContain(path);
    }
    expect(docs).toContain("openapi.yaml");
    expect(docs).toContain("asyncapi.yaml");
  });

  it("keeps Wi-Fi continuation URLs aligned", () => {
    const firmware = readFirmwareApiRoutes();
    const mock = read("frontend/mock/mockPlugin.ts");
    expect(firmware).toContain("/wifi-testing");
    expect(mock).toContain("next:");
    expect(openapi).toContain("        next:");
  });

  it("keeps MQTT config fields aligned across client, mock and OpenAPI", () => {
    const client = read("frontend/src/api/client.ts");
    const mock = read("frontend/mock/mockPlugin.ts");
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

  it("documents Host allowlist rejection in OpenAPI", () => {
    expect(openapi).toContain("Host rejected");
  });

  it("applies Host once on the server and mode gates via ApiGuard", () => {
    const firmware = readFirmwareApiRoutes();
    const admin = read("src/web/admin.cpp");
    expect(admin).toContain("ws.addMiddleware(mwRequireAllowedHost())");
    expect(firmware).not.toContain("mwRequireAllowedHost");
    expect(firmware).not.toContain("mwApiApPost");
    expect(firmware).toContain("ApiGuard::Sta");
    expect(firmware).toContain("ApiGuard::Ap");
  });
});

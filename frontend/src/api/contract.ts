/** Shared API surface used by client, mock and documentation checks. */
export const API_GET_PATHS = [
  "/api/csrf",
  "/api/device",
  "/api/chaya",
  "/api/wifi/status",
  "/api/wifi/config",
  "/api/wifi/scan",
  "/api/wifi/connect-status",
  "/api/mqtt/status",
  "/api/mqtt",
  "/api/settings",
  "/api/update/status",
] as const;

export const API_POST_PATHS = [
  "/api/chaya/send",
  "/api/wifi/connect",
  "/api/wifi/connect-commit",
  "/api/wifi/connect-abort",
  "/api/mqtt",
  "/api/settings",
  "/api/reboot",
  "/api/factory-reset",
  "/api/update/check",
  "/api/update/install",
] as const;

export const SSE_EVENT_TYPES = ["chaya", "wifi", "mqtt", "ota", "device"] as const;

export const SPA_UI_PATHS = [
  "/",
  "/wifi",
  "/wifi-testing",
  "/mqtt",
  "/settings",
  "/settings/device",
  "/update",
] as const;

import type { ChayaStatus, DeviceBatteryEvent, MqttStatus, OtaStatus, WifiStatus } from "./types";
import {
  parseChayaStatus,
  parseDeviceBattery,
  parseMqttStatus,
  parseOtaStatus,
  parseWifiStatus,
} from "./validate";

export type SseHandlers = {
  chaya?: (data: ChayaStatus) => void;
  wifi?: (data: WifiStatus) => void;
  mqtt?: (data: MqttStatus) => void;
  ota?: (data: OtaStatus) => void;
  device?: (data: DeviceBatteryEvent) => void;
  error?: () => void;
};

export function connectEvents(handlers: SseHandlers): () => void {
  const es = new EventSource("/events");

  const bind = <T>(type: string, cb?: (data: T) => void, map?: (raw: unknown) => T) => {
    if (!cb) return;
    es.addEventListener(type, (ev) => {
      try {
        const raw: unknown = JSON.parse((ev as MessageEvent<string>).data);
        cb(map ? map(raw) : (raw as T));
      } catch {
        /* ignore malformed payloads */
      }
    });
  };

  bind("chaya", handlers.chaya, parseChayaStatus);
  bind("wifi", handlers.wifi, parseWifiStatus);
  bind("mqtt", handlers.mqtt, parseMqttStatus);
  bind("ota", handlers.ota, parseOtaStatus);
  bind("device", handlers.device, parseDeviceBattery);
  es.onerror = () => handlers.error?.();

  return () => es.close();
}

import type { ChayaStatus, MqttStatus, WifiStatus } from "./types";

export type SseHandlers = {
  chaya?: (data: ChayaStatus) => void;
  wifi?: (data: WifiStatus) => void;
  mqtt?: (data: MqttStatus) => void;
  error?: () => void;
};

export function connectEvents(handlers: SseHandlers): () => void {
  const es = new EventSource("/events");

  const bind = <T>(type: string, cb?: (data: T) => void) => {
    if (!cb) return;
    es.addEventListener(type, (ev) => {
      try {
        cb(JSON.parse((ev as MessageEvent<string>).data) as T);
      } catch {
        /* ignore malformed payloads */
      }
    });
  };

  bind("chaya", handlers.chaya);
  bind("wifi", handlers.wifi);
  bind("mqtt", handlers.mqtt);
  es.onerror = () => handlers.error?.();

  return () => es.close();
}

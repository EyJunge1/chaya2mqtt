import { afterEach, describe, expect, it, vi } from "vitest";
import { connectEvents } from "./sse";

class FakeEventSource {
  static instances: FakeEventSource[] = [];
  onerror: ((ev: Event) => void) | null = null;
  private listeners = new Map<string, Set<(ev: MessageEvent<string>) => void>>();
  closed = false;
  url: string;

  constructor(url: string) {
    this.url = url;
    FakeEventSource.instances.push(this);
  }

  addEventListener(type: string, cb: (ev: MessageEvent<string>) => void) {
    const set = this.listeners.get(type) ?? new Set();
    set.add(cb);
    this.listeners.set(type, set);
  }

  emit(type: string, data: unknown) {
    const ev = { data: JSON.stringify(data) } as MessageEvent<string>;
    for (const cb of this.listeners.get(type) ?? []) cb(ev);
  }

  emitRaw(type: string, raw: string) {
    const ev = { data: raw } as MessageEvent<string>;
    for (const cb of this.listeners.get(type) ?? []) cb(ev);
  }

  close() {
    this.closed = true;
  }
}

describe("connectEvents", () => {
  afterEach(() => {
    FakeEventSource.instances = [];
    vi.unstubAllGlobals();
  });

  it("binds typed handlers and closes on cleanup", () => {
    vi.stubGlobal("EventSource", FakeEventSource as unknown as typeof EventSource);
    const chaya = vi.fn();
    const wifi = vi.fn();
    const mqtt = vi.fn();
    const ota = vi.fn();
    const device = vi.fn();
    const error = vi.fn();

    const stop = connectEvents({ chaya, wifi, mqtt, ota, device, error });
    const es = FakeEventSource.instances[0]!;
    expect(es.url).toBe("/events");

    es.emit("chaya", { rx: 1, tx: 2, connected: true, configured: true });
    es.emit("wifi", { connected: false });
    es.emit("mqtt", { connected: true });
    es.emit("ota", {
      phase: "idle",
      channel: "stable",
      localVersion: "1",
      availableVersion: "",
      bytesDone: 0,
      bytesTotal: 0,
      error: "",
      generation: 0,
    });
    es.emit("device", { batteryMv: 3900, batteryPct: 55 });
    es.onerror?.(new Event("error"));

    expect(chaya).toHaveBeenCalledWith({ rx: 1, tx: 2, connected: true, configured: true });
    expect(wifi).toHaveBeenCalledWith({ connected: false });
    expect(mqtt).toHaveBeenCalledWith({ connected: true });
    expect(ota).toHaveBeenCalled();
    expect(device).toHaveBeenCalledWith({ batteryMv: 3900, batteryPct: 55 });
    expect(error).toHaveBeenCalled();

    es.emitRaw("chaya", "{not-json");
    expect(chaya).toHaveBeenCalledTimes(1);

    stop();
    expect(es.closed).toBe(true);
  });
});

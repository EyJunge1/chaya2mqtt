#!/usr/bin/env python3
"""
MQTT-Gegenseite fuer das chaya2mqtt ESP32 E-Ink-Projekt.

Interaktiv (Default): SUB auf Empfangs-Topic des ESP, PUB auf dessen Sende-Gegen-Topic.
Smoke-Modus (--smoke): deterministische Pass/Fail-Pruefung ohne Tastatureingabe.

Lokale Konfiguration unten eintragen oder per CLI ueberschreiben.
Echte Zugangsdaten niemals committen.
"""

from __future__ import annotations

import argparse
import os
import ssl
import sys
import threading
import time
from typing import Any

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover
    print("Fehlt Abhaengigkeit: pip install paho-mqtt", file=sys.stderr)
    raise

# ----- Lokale Konfiguration (keine echten Secrets committen) -----
MQTT_HOST = ""
MQTT_PORT = 8883
MQTT_USER = ""
MQTT_PASS = ""

# Gegenueber ESP-Defaults: ESP publiziert chaya2mqtt/to_b, subscribed chaya2mqtt/to_a
MQTT_TOPIC_PUB = "chaya2mqtt/to_a"
MQTT_TOPIC_SUB = "chaya2mqtt/to_b"


def _conn_ok(reason_code: Any) -> bool:
    if hasattr(reason_code, "is_failure"):
        return not bool(reason_code.is_failure)
    try:
        return int(reason_code) == 0
    except (TypeError, ValueError):
        return True


def _build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Simuliert die MQTT-Gegenseite zum ESP32 (TLS). "
        "Defaults stehen am Anfang des Scripts; CLI ueberschreibt."
    )
    p.add_argument("--host", help="MQTT-Broker (Default: MQTT_HOST im Script)")
    p.add_argument("--port", type=int, help=f"MQTT-Port (Default: {MQTT_PORT})")
    p.add_argument("--user", help="MQTT-Benutzer (Default: MQTT_USER im Script)")
    p.add_argument("--pass", dest="password", help="MQTT-Passwort (Default: MQTT_PASS im Script)")
    p.add_argument("--topic-pub", dest="topic_pub", help="Sende-Topic (Default: MQTT_TOPIC_PUB)")
    p.add_argument("--topic-sub", dest="topic_sub", help="Empfangs-Topic (Default: MQTT_TOPIC_SUB)")
    p.add_argument(
        "--smoke",
        action="store_true",
        help="Nicht-interaktiver Smoke-Test: warte auf ESP-LWT/Counter, sende ein Herz, Exit 0/1",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=45.0,
        help="Smoke-Timeout in Sekunden (Default: 45)",
    )
    return p


class SimulatorState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.received_hearts = 0
        self.sent_hearts = 0
        self.esp_online: bool | None = None
        self.got_counter = False
        self.quit_event = threading.Event()

    def set_remote_counter(self, value: int) -> None:
        with self._lock:
            self.received_hearts = value
            self.got_counter = True

    def next_sent_value(self) -> int:
        with self._lock:
            self.sent_hearts += 1
            return self.sent_hearts

    def hearts(self) -> int:
        with self._lock:
            return self.received_hearts

    def set_lwt(self, payload: bytes) -> None:
        txt = payload.decode("utf-8", errors="replace").strip().lower()
        with self._lock:
            if txt == "online":
                self.esp_online = True
            elif txt == "offline":
                self.esp_online = False


def _make_client(
    host: str,
    port: int,
    user: str,
    password: str,
    topic_pub: str,
    topic_sub: str,
    state: SimulatorState,
    *,
    interactive_prompt: Any | None = None,
) -> mqtt.Client:
    lwt_topic = f"{topic_sub}/lwt"
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"chaya2mqtt-sim-{os.getpid()}",
        protocol=mqtt.MQTTv311,
    )
    ctx = ssl.create_default_context()
    client.tls_set_context(ctx)
    if user:
        client.username_pw_set(user, password or "")

    def on_connect(
        cli: mqtt.Client,
        _userdata: Any,
        _flags: Any,
        reason_code: Any,
        _properties: Any,
    ) -> None:
        if not _conn_ok(reason_code):
            print(f"[Fehler] Verbindung abgelehnt: {reason_code}", flush=True)
            return
        print(f"[OK] Verbunden. Warte auf ESP32-Herzen ({topic_sub})", flush=True)
        cli.subscribe(topic_sub, qos=1)
        cli.subscribe(lwt_topic, qos=1)

    def on_message(
        _cli: mqtt.Client,
        _userdata: Any,
        msg: mqtt.MQTTMessage,
    ) -> None:
        if msg.topic == lwt_topic:
            txt = msg.payload.decode("utf-8", errors="replace").strip()
            state.set_lwt(msg.payload)
            print(f"[ESP32] Status: {txt}", flush=True)
            return
        if msg.topic == topic_sub:
            try:
                remote = int(msg.payload.decode("utf-8").strip())
            except (UnicodeDecodeError, ValueError):
                print("[WARN] Ungueltiger Zaehler-Payload, ignoriert.", flush=True)
                return
            state.set_remote_counter(remote)
            print(f"Remote-Zaehlerstand (ESP gesendet): {remote}", flush=True)
            if interactive_prompt is not None:
                interactive_prompt()

    def on_disconnect(
        _client: mqtt.Client,
        _userdata: Any,
        _disconnect_flags: Any,
        reason_code: Any,
        _properties: Any,
    ) -> None:
        if state.quit_event.is_set():
            return
        print(f"[MQTT] Verbindung getrennt: {reason_code}", flush=True)

    client.on_connect = on_connect  # type: ignore[method-assign]
    client.on_message = on_message  # type: ignore[method-assign]
    client.on_disconnect = on_disconnect  # type: ignore[method-assign]
    return client


def _publish_heart(client: mqtt.Client, state: SimulatorState, topic_pub: str) -> bool:
    if not client.is_connected():
        print("[WARN] Nicht verbunden – kann nicht senden.", flush=True)
        return False
    n = state.next_sent_value()
    payload = str(n).encode("utf-8")
    ok = client.publish(topic_pub, payload, qos=1, retain=True).rc == mqtt.MQTT_ERR_SUCCESS
    if ok:
        print(f"-> Zaehler {n} (retained) auf {topic_pub}", flush=True)
    else:
        print("[WARN] Publish fehlgeschlagen.", flush=True)
    return ok


def run_smoke(
    host: str,
    port: int,
    user: str,
    password: str,
    topic_pub: str,
    topic_sub: str,
    timeout: float,
) -> int:
    """Return 0 on PASS, 1 on FAIL."""
    state = SimulatorState()
    client = _make_client(host, port, user, password, topic_pub, topic_sub, state)
    print(f"[SMOKE] Verbinde mit {host}:{port} (TLS)…", flush=True)
    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        print(f"[FAIL] connect: {e}", file=sys.stderr, flush=True)
        return 1

    client.loop_start()
    deadline = time.monotonic() + max(1.0, timeout)
    connected_wait = time.monotonic() + 10.0
    while time.monotonic() < connected_wait and not client.is_connected():
        time.sleep(0.1)
    if not client.is_connected():
        print("[FAIL] Broker-Verbindung nicht hergestellt", flush=True)
        client.loop_stop()
        return 1

    print("[SMOKE] Warte auf ESP LWT=online oder Counter-Payload…", flush=True)
    while time.monotonic() < deadline:
        with state._lock:
            online = state.esp_online is True
            got = state.got_counter
        if online or got:
            break
        time.sleep(0.2)
    else:
        print(
            "[FAIL] Timeout: weder LWT online noch Counter empfangen. "
            "Topics/Pairing/Broker pruefen.",
            flush=True,
        )
        state.quit_event.set()
        client.loop_stop()
        try:
            client.disconnect()
        except OSError:
            pass
        return 1

    if not _publish_heart(client, state, topic_pub):
        print("[FAIL] Publish an ESP fehlgeschlagen", flush=True)
        state.quit_event.set()
        client.loop_stop()
        return 1

    # Kurz warten, damit retained Publish die Broker-Queue verlassen kann.
    time.sleep(0.5)
    print(
        f"[PASS] smoke ok (esp_online={state.esp_online}, "
        f"counter_seen={state.got_counter}, last_rx={state.hearts()})",
        flush=True,
    )
    state.quit_event.set()
    client.loop_stop()
    try:
        client.disconnect()
    except OSError as exc:
        print(f"[Warn] disconnect: {exc}", file=sys.stderr, flush=True)
    return 0


def run_interactive(
    host: str,
    port: int,
    user: str,
    password: str,
    topic_pub: str,
    topic_sub: str,
) -> int:
    state = SimulatorState()

    def prompt() -> None:
        sys.stdout.flush()
        sys.stderr.flush()
        print(
            "\n─────────────────────────────",
            "\nEnter  → Herz senden   |   q + Enter → Beenden",
            "\n> ",
            end="",
            flush=True,
        )

    client = _make_client(
        host, port, user, password, topic_pub, topic_sub, state, interactive_prompt=prompt
    )

    def input_loop() -> None:
        time.sleep(0.3)
        prompt()
        while not state.quit_event.is_set():
            try:
                line = sys.stdin.readline()
            except (KeyboardInterrupt, EOFError):
                state.quit_event.set()
                break
            if line == "":
                state.quit_event.set()
                break
            s = line.strip()
            if s.lower() in ("q", "quit", "exit"):
                state.quit_event.set()
                break
            if s == "":
                _publish_heart(client, state, topic_pub)
            prompt()

    print(f"[chaya2mqtt Simulator] Verbinde mit {host}:{port} (TLS)...", flush=True)
    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        print(f"[Fehler] connect: {e}", file=sys.stderr, flush=True)
        return 1

    client.loop_start()
    threading.Thread(target=input_loop, name="stdin", daemon=True).start()

    try:
        while not state.quit_event.is_set():
            time.sleep(0.2)
    except KeyboardInterrupt:
        state.quit_event.set()

    client.loop_stop()
    try:
        client.disconnect()
    except OSError as exc:
        print(f"[Warn] disconnect: {exc}", file=sys.stderr, flush=True)
    print("\n[Auf Wiedersehen]", flush=True)
    return 0


def main() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args()

    host = (args.host or MQTT_HOST).strip()
    port = args.port if args.port is not None else MQTT_PORT
    user = args.user if args.user is not None else MQTT_USER
    password = args.password if args.password is not None else MQTT_PASS
    topic_pub = (args.topic_pub or MQTT_TOPIC_PUB).strip()
    topic_sub = (args.topic_sub or MQTT_TOPIC_SUB).strip()

    if not host:
        parser.error("MQTT_HOST im Script setzen oder --host angeben.")

    if args.smoke:
        raise SystemExit(
            run_smoke(host, port, user, password, topic_pub, topic_sub, args.timeout)
        )
    raise SystemExit(run_interactive(host, port, user, password, topic_pub, topic_sub))


if __name__ == "__main__":
    main()

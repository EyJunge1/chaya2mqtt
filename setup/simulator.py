#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MQTT-Gegenseite fuer das chaya2mqtt ESP32 E-Ink-Projekt.

Standard fuer ein ESP mit Default-Topics (Sende chaya/to_b, Empfang chaya/to_a):
dieser Simulator SUB auf chaya/to_b, PUB auf chaya/to_a, Payload exakt „chaya“.
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
except ImportError as e:  # pragma: no cover
    print("Fehlt Abhaengigkeit: pip install -r setup/requirements.txt", file=sys.stderr)
    raise e

# ----- Konfiguration (anpassen) -----
MQTT_HOST = ""  # z. B. "mqtt.example.com"
MQTT_PORT = 8883
MQTT_USER = ""
MQTT_PASS = ""

# Gegenueber ESP-Defaults: ESP publiziert chaya/to_b, subscribed chaya/to_a
MQTT_TOPIC_PUB = "chaya/to_a"
MQTT_TOPIC_SUB = "chaya/to_b"

CHAYA_PAYLOAD = b"chaya"


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
        "Konfiguration siehe Konstanten MQTT_* oben im Script; CLI ueberschreibt."
    )
    p.add_argument("--host", help="MQTT-Broker (Default: MQTT_HOST im Script)")
    p.add_argument("--port", type=int, help=f"MQTT-Port (Default: {MQTT_PORT})")
    p.add_argument("--user", help="MQTT-Benutzer (Default: MQTT_USER)")
    p.add_argument("--pass", dest="password", help="MQTT-Passwort (Default: MQTT_PASS)")
    p.add_argument("--topic-pub", dest="topic_pub", help="Sende-Topic (Default: MQTT_TOPIC_PUB)")
    p.add_argument("--topic-sub", dest="topic_sub", help="Empfangs-Topic (Default: MQTT_TOPIC_SUB)")
    return p


class SimulatorState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.received_hearts = 0
        self.esp_online: bool | None = None
        self.quit_event = threading.Event()

    def add_heart(self) -> None:
        with self._lock:
            self.received_hearts += 1

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


def main() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args()

    host = (args.host or MQTT_HOST).strip()
    port = args.port if args.port is not None else MQTT_PORT
    user = args.user if args.user is not None else MQTT_USER
    password = args.password if args.password is not None else MQTT_PASS
    topic_pub = (args.topic_pub or MQTT_TOPIC_PUB).strip()
    topic_sub = (args.topic_sub or MQTT_TOPIC_SUB).strip()
    lwt_topic = f"{topic_sub}/lwt"

    if not host:
        parser.error("MQTT_HOST ist leer — oben im Script setzen oder --host angeben.")

    state = SimulatorState()

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
        print(
            f"[OK] Verbunden. Warte auf ESP32-Herzen ({topic_sub})",
            flush=True,
        )
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
        if msg.topic == topic_sub and msg.payload == CHAYA_PAYLOAD:
            state.add_heart()
            n = state.hearts()
            print(f"Empfangene Herzen: {n}", flush=True)
            prompt()

    client.on_connect = on_connect  # type: ignore[method-assign]
    client.on_message = on_message  # type: ignore[method-assign]

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

    client.on_disconnect = on_disconnect  # type: ignore[method-assign]

    def publish_heart() -> bool:
        if not client.is_connected():
            print("[WARN] Nicht verbunden – kann nicht senden.", flush=True)
            return False
        ok = client.publish(topic_pub, CHAYA_PAYLOAD, qos=0).rc == mqtt.MQTT_ERR_SUCCESS
        if ok:
            print(f"-> Herz gesendet auf {topic_pub}", flush=True)
        else:
            print("[WARN] Publish fehlgeschlagen.", flush=True)
        return ok

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
                publish_heart()
            prompt()

    print(f"[chaya2mqtt Simulator] Verbinde mit {host}:{port} (TLS)...", flush=True)

    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        print(f"[Fehler] connect: {e}", file=sys.stderr, flush=True)
        sys.exit(1)

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
    except Exception:
        pass
    print("\n[Auf Wiedersehen]", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
MQTT counterpart for the chaya2mqtt ESP32 E-Ink project.

Interactive (default): SUB to the ESP's receive topic, PUB to its corresponding send topic.
Smoke mode (--smoke): deterministic pass/fail check without keyboard input.
Hardware smoke (--hardware-smoke): additionally waits for a manual device heart.

Enter local configuration below or override it via CLI.
Never commit real credentials.
"""

# paho-mqtt is optional and checked explicitly at runtime.
# pyright: reportMissingImports=false

from __future__ import annotations

import argparse
import os
import ssl
import sys
import threading
import time
from contextlib import suppress
from typing import Any

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover
    print("Missing dependency: pip install paho-mqtt", file=sys.stderr)
    raise

# ----- Local configuration (do not commit real secrets) -----
MQTT_HOST = ""
MQTT_PORT = 8883
MQTT_USER = ""
MQTT_PASS = ""

# Defaults follow the pairing model: each device publishes to chaya2mqtt/<own_id>
# and subscribes to chaya2mqtt/<partner_id>. Override via CLI for a real pair.
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
        description="Simulates the MQTT counterpart to the ESP32 (TLS). "
        "Defaults are defined at the top of the script; CLI options override them."
    )
    p.add_argument("--host", help="MQTT broker (default: MQTT_HOST in the script)")
    p.add_argument("--port", type=int, help=f"MQTT port (default: {MQTT_PORT})")
    p.add_argument("--user", help="MQTT user (default: MQTT_USER in the script)")
    p.add_argument(
        "--pass", dest="password", help="MQTT password (default: MQTT_PASS in the script)"
    )
    p.add_argument("--topic-pub", dest="topic_pub", help="Publish topic (default: MQTT_TOPIC_PUB)")
    p.add_argument(
        "--topic-sub", dest="topic_sub", help="Subscribe topic (default: MQTT_TOPIC_SUB)"
    )
    p.add_argument(
        "--smoke",
        action="store_true",
        help="Non-interactive smoke test: wait for ESP LWT/counter, send a heart, exit 0/1",
    )
    p.add_argument(
        "--hardware-smoke",
        action="store_true",
        help="Manual SKU 34586 smoke: verify broker PUBACK, then require a new ESP heart counter",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=45.0,
        help="Smoke timeout in seconds (default: 45)",
    )
    return p


class SimulatorState:
    """Store MQTT simulator state shared between callback and main threads."""

    def __init__(self) -> None:
        """Initialize counters, connection state, and synchronization primitives."""
        self._lock = threading.Lock()
        self.received_hearts = 0
        self.sent_hearts = 0
        self.esp_online: bool | None = None
        self.got_counter = False
        self.quit_event = threading.Event()

    def set_remote_counter(self, value: int) -> None:
        """Store the latest counter received from the ESP."""
        with self._lock:
            self.received_hearts = value
            self.got_counter = True

    def next_sent_value(self) -> int:
        """Increment and return the next outgoing heart counter."""
        with self._lock:
            self.sent_hearts += 1
            return self.sent_hearts

    def hearts(self) -> int:
        """Return the latest counter received from the ESP."""
        with self._lock:
            return self.received_hearts

    def remote_status(self) -> tuple[bool, bool]:
        """Return whether the ESP is online and whether a counter was received."""
        with self._lock:
            return self.esp_online is True, self.got_counter

    def set_lwt(self, payload: bytes) -> None:
        """Update the ESP online state from an MQTT last-will payload."""
        txt = payload.decode("utf-8", errors="replace").strip().lower()
        with self._lock:
            if txt == "online":
                self.esp_online = True
            elif txt == "offline":
                self.esp_online = False


def _make_client(
    user: str,
    password: str,
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
            print(f"[ERROR] Connection rejected: {reason_code}", flush=True)
            return
        print(f"[OK] Connected. Waiting for ESP32 hearts ({topic_sub})", flush=True)
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
                print("[WARN] Invalid counter payload, ignoring.", flush=True)
                return
            state.set_remote_counter(remote)
            print(f"Remote counter (sent by ESP): {remote}", flush=True)
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
        print(f"[MQTT] Disconnected: {reason_code}", flush=True)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    return client


def _publish_heart(client: mqtt.Client, state: SimulatorState, topic_pub: str) -> bool:
    if not client.is_connected():
        print("[WARN] Not connected — cannot publish.", flush=True)
        return False
    n = state.next_sent_value()
    payload = str(n).encode("utf-8")
    info = client.publish(topic_pub, payload, qos=1, retain=True)
    ok = info.rc == mqtt.MQTT_ERR_SUCCESS
    if ok:
        try:
            info.wait_for_publish(timeout=5.0)
            ok = info.is_published()
        except RuntimeError:
            ok = False
    if ok:
        print(f"-> Counter {n} (retained) to {topic_pub}", flush=True)
    else:
        print("[WARN] Publish failed.", flush=True)
    return ok


def run_smoke(
    host: str,
    port: int,
    user: str,
    password: str,
    topic_pub: str,
    topic_sub: str,
    timeout: float,
    *,
    require_device_counter_increment: bool = False,
) -> int:
    """Return 0 on PASS, 1 on FAIL."""
    state = SimulatorState()
    client = _make_client(user, password, topic_sub, state)
    print(f"[SMOKE] Connecting to {host}:{port} (TLS)…", flush=True)
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
        print("[FAIL] Broker connection not established", flush=True)
        client.loop_stop()
        return 1

    print("[SMOKE] Waiting for ESP LWT=online or counter payload…", flush=True)
    while time.monotonic() < deadline:
        online, got = state.remote_status()
        if online or got:
            break
        time.sleep(0.2)
    else:
        print(
            "[FAIL] Timeout: received neither LWT online nor a counter. "
            "Check topics/pairing/broker.",
            flush=True,
        )
        state.quit_event.set()
        client.loop_stop()
        with suppress(OSError):
            client.disconnect()
        return 1

    if not _publish_heart(client, state, topic_pub):
        print("[FAIL] Publish to ESP lacked QoS-1 PUBACK", flush=True)
        state.quit_event.set()
        client.loop_stop()
        return 1

    if require_device_counter_increment:
        if not state.got_counter:
            print("[FAIL] Hardware smoke requires an initial ESP counter", flush=True)
            state.quit_event.set()
            client.loop_stop()
            return 1
        baseline = state.hearts()
        print("[ACTION] Send one heart on the device button or Web Admin.", flush=True)
        while time.monotonic() < deadline and state.hearts() <= baseline:
            time.sleep(0.1)
        if state.hearts() <= baseline:
            print("[FAIL] ESP counter did not advance before timeout", flush=True)
            state.quit_event.set()
            client.loop_stop()
            return 1
    else:
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
    """Run the interactive MQTT counterpart until the user exits."""
    state = SimulatorState()

    def prompt() -> None:
        sys.stdout.flush()
        sys.stderr.flush()
        print(
            "\n─────────────────────────────",
            "\nEnter  → Send heart   |   q + Enter → Quit",
            "\n> ",
            end="",
            flush=True,
        )

    client = _make_client(user, password, topic_sub, state, interactive_prompt=prompt)

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

    print(f"[chaya2mqtt Simulator] Connecting to {host}:{port} (TLS)...", flush=True)
    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        print(f"[ERROR] connect: {e}", file=sys.stderr, flush=True)
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
    print("\n[Goodbye]", flush=True)
    return 0


def main() -> None:
    """Parse command-line options and run the selected simulator mode."""
    parser = _build_arg_parser()
    args = parser.parse_args()

    host = (args.host or MQTT_HOST).strip()
    port = args.port if args.port is not None else MQTT_PORT
    user = args.user if args.user is not None else MQTT_USER
    password = args.password if args.password is not None else MQTT_PASS
    topic_pub = (args.topic_pub or MQTT_TOPIC_PUB).strip()
    topic_sub = (args.topic_sub or MQTT_TOPIC_SUB).strip()

    if not host:
        parser.error("Set MQTT_HOST in the script or specify --host.")

    if args.smoke or args.hardware_smoke:
        raise SystemExit(
            run_smoke(
                host,
                port,
                user,
                password,
                topic_pub,
                topic_sub,
                args.timeout,
                require_device_counter_increment=args.hardware_smoke,
            )
        )
    raise SystemExit(run_interactive(host, port, user, password, topic_pub, topic_sub))


if __name__ == "__main__":
    main()

#pragma once

/** RC-WEB-13: only ENQUEUED is success; PARTIALLY_ENQUEUED must redirty. */
inline auto sseEnqueueAccepted(int sendStatus) -> bool {
    return sendStatus == 1; // AsyncEventSource::ENQUEUED
}

/** MQTT page/status line: connected only when a broker is configured (RC-WEB-16). */
inline auto mqttPageConn(bool configured, bool connected) -> bool {
    return configured && connected;
}

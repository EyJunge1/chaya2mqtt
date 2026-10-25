#pragma once

/** MQTT page/status line: connected only when a broker is configured (RC-WEB-16). */
inline auto mqttPageConn(bool configured, bool connected) -> bool { return configured && connected; }

#pragma once

#include <Arduino.h>

/** Wi-Fi scan poller (minified IIFE). */
static const char WIFI_SCAN_JS[] PROGMEM = R"chaya2mqtt_ws((function () { var ss = document.getElementById('ssid'), lst = document.getElementById('list'), st = document.getElementById('st'); function poll() { fetch('/wifi-scan') .then(function (r) { if (r.status === 202) { st.textContent = 'Scanning…'; return Promise.resolve(null); } return r.json(); }) .then(function (rows) { if (rows === null) { return; } lst.innerHTML = ''; if (!rows || !rows.length) { st.textContent = 'No networks found.'; return; } st.textContent = 'Click a network, enter password, press Connect.'; for (var i = 0; i < rows.length; i++) { var li = document.createElement('li'); var a = document.createElement('a'); a.href = '#'; (function (nm) { a.onclick = function (ev) { ev.preventDefault(); ss.value = nm; return false; }; })(rows[i].ssid); var o = rows[i].open ? ', open' : ''; a.textContent = rows[i].ssid + ' (' + rows[i].rssi + ' dBm' + o + ')'; li.appendChild(a); lst.appendChild(li); } }) .catch(function () { st.textContent = 'Scan error.'; }); } setInterval(poll, 1500); poll(); })();)chaya2mqtt_ws";

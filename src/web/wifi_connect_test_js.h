#pragma once

#include <Arduino.h>

/** Poll /wifi-connect-status during AP Wi-Fi test; on ok POST commitForm for reboot + fallback redirect after delay. Minified IIFE. */
static const char WIFI_CONNECT_TEST_JS[] PROGMEM =
    R"wc((function () {var st = document.getElementById('st'); var committed = false; var pollStopped = false; function poll() {if (pollStopped) {return;} fetch('/wifi-connect-status').then(function (r) {return r.json();}).then(function (d) {if (!d || !d.state) {st.textContent = 'Status error.'; return;} if (d.state === 'idle') {window.location.href = '/wifi'; return;} if (d.state === 'testing') {st.textContent = 'Testing connection to ' + (d.ssid || '') + '…'; return;} if (d.state === 'fail') {st.textContent = 'Could not connect to ' + (d.ssid || '') + '.'; var fa = document.getElementById('failActions'); if (fa) {fa.style.display = 'block';} return;} if (d.state === 'ok') {st.textContent = 'Connected! Saving and rebooting…'; if (!committed) {committed = true; pollStopped = true; setTimeout(function () {window.location.href = 'http://chaya2mqtt.local/';}, 10000); var f = document.getElementById('commitForm'); if (f) {f.submit();}} return;} }).catch(function () {if (!pollStopped) {st.textContent = 'Status error.';}}); } setInterval(poll, 2000); poll(); })();)wc";

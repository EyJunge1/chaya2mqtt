#pragma once

#include <Arduino.h>

/** Poll /wifi-connect-status and submit commit on success (AP setup flow). Minified IIFE. */
static const char WIFI_CONNECT_TEST_JS[] PROGMEM =
    R"wc((function () {var st = document.getElementById('st'); var committed = false; function poll() {fetch('/wifi-connect-status').then(function (r) {return r.json();}).then(function (d) {if (!d || !d.state) {st.textContent = 'Status error.'; return;} if (d.state === 'idle') {window.location.href = '/wifi'; return;} if (d.state === 'testing') {st.textContent = 'Testing connection to ' + (d.ssid || '') + '…'; return;} if (d.state === 'fail') {st.textContent = 'Could not connect to ' + (d.ssid || '') + '.'; var fa = document.getElementById('failActions'); if (fa) {fa.style.display = 'block';} return;} if (d.state === 'ok') {st.textContent = 'Connected. Saving and rebooting…'; if (!committed) {committed = true; var f = document.getElementById('commitForm'); if (f) {f.submit();}} } }).catch(function () {st.textContent = 'Status error.';}); } setInterval(poll, 2000); poll(); })();)wc";

#include <Arduino.h>
#include <cstdio>
#include <ESPAsyncWebServer.h>

#include "pages.h"
#include "pages_internal.h"

#include "constants.h"
#include "mqtt/config.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"
#include "wifi/test.h"
#include "web_utils.h"
#include "web/assets/wifi_scan_js.h"
#include "web/assets/wifi_status_js.h"
#include "web/assets/mqtt_status_js.h"
#include "web/assets/wifi_connect_test_js.h"
#include "web/assets/pairing_js.h"

void streamWifiCommitDonePage(AsyncWebServerRequest* req, const char* staIp) {
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "Wi-Fi");
    resp->print(F("<h1>Wi-Fi</h1>"
                  "<p class='ok'>Wi-Fi gespeichert. Ger&auml;t startet neu.</p>"
                  "<p class='hint'>Danach Setup hier fortsetzen:</p>"
                  "<p><a href='http://"));
    resp->print(staIp);
    resp->print(F("/'><strong>http://"));
    resp->print(staIp);
    resp->print(F("</strong></a></p>"
                  "<p class='hint'>Alternatively (same LAN): "
                  "<a href='"));
    resp->print(kDeviceHttpOrigin);
    resp->print(F("'><strong>"));
    resp->print(kDeviceHttpOrigin);
    resp->print(F("</strong></a></p>"
                  "<p class='hint'>Configure MQTT after reboot on the same Wi-Fi.</p>"
                  "</body></html>"));
    req->send(resp);
}

void streamWifiPage(AsyncWebServerRequest* req) {
    wlanRequestWifiScanRefresh();

    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "Wi-Fi");
    resp->print(F("<h1>Wi-Fi Setup</h1>"
                   "<p id='cs' class='hint'></p>"));
    resp->print(
        F("<p class='hint' id='st'>Scanning…</p><ul id='list'></ul>"
          "<form method='post' action='/wifi-connect' id='wf'>"));
    resp->print(F("<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*resp);
    resp->print(F("'/>"));
    resp->print(
        F("<label for='ssid'>SSID</label>"
          "<input name='ssid' id='ssid' required maxlength='32' autocomplete='off'/>"
          "<label for='pwd'>Password</label>"
          "<input name='password' id='pwd' type='password' maxlength='64' autocomplete='current-password'/>"
          "<button type='submit'>Connect</button></form>"
          "<script>"));
    resp->print(reinterpret_cast<const __FlashStringHelper*>(WIFI_STATUS_JS));
    resp->print(F("</script><script>"));
    resp->print(reinterpret_cast<const __FlashStringHelper*>(WIFI_SCAN_JS));
    resp->print(F("</script><a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(resp);
}

void streamWifiTestingPage(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        webRedirect(req, F("/"));
        return;
    }
    if (wlanGetWifiConnectionTestState() == WlanWifiConnectionTestState::Idle) {
        webRedirect(req, F("/wifi"));
        return;
    }

    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "Wi-Fi test");
    resp->print(F("<h1>Wi-Fi connection test</h1>"
                  "<p class='hint' id='st'>Starting…</p>"
                  "<div id='failActions' style='display:none'>"
                  "<form method='post' action='/wifi-connect-abort'>"
                  "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*resp);
    resp->print(F("'/>"
                  "<button type='submit'>Back to Wi-Fi setup</button>"
                  "</form></div>"
                  "<form id='commitForm' method='post' action='/wifi-connect-commit'>"
                  "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*resp);
    resp->print(F("'/>"
                  "</form>"
                  "<script>window.__CHAYA_HTTP_ORIGIN__=\""));
    appendHtmlEscaped(*resp, kDeviceHttpOrigin);
    resp->print(F("\";</script><script>"));
    resp->print(reinterpret_cast<const __FlashStringHelper*>(WIFI_CONNECT_TEST_JS));
    resp->print(F("</script>"
                  "<p class='hint'><a class='btn-back' href='/'>Dashboard</a></p></body></html>"));
    req->send(resp);
}

void handleWifiScanJson(AsyncWebServerRequest* req) {
    if (!wlanWifiScanCacheReady()) {
        webSendEmpty(req, 202);
        return;
    }

    AsyncResponseStream* resp = beginResponseStreamOr500(req, "application/json");
    if (resp == nullptr) {
        return;
    }
    const size_t n = wlanWifiScanCachedCount();
    resp->print('[');
    for (size_t i = 0; i < n; ++i) {
        WlanScanRow row{};
        if (!wlanWifiScanCopyRowAt(i, &row)) {
            break;
        }
        if (i > 0U) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedCStr(*resp, row.ssid);
        resp->print(F(",\"rssi\":"));
        resp->print(row.rssi);
        resp->print(row.open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    req->send(resp);

    wlanRequestWifiScanRefresh();
}

void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    MqttConfig cfg{};
    if (showSavedBanner && mqttCfgHasUnappliedPending()) {
        mqttCfgPendingSnapshot(&cfg);
    } else {
        mqttCfgSnapshot(&cfg);
    }
    char portBuf[8];
    snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(cfg.port));

    AsyncResponseStream* response = beginResponseStreamOr500(req, "text/html");
    if (response == nullptr) {
        return;
    }
    streamPageHeader(*response, "MQTT");
    response->print(F("<h1>MQTT Settings</h1>"));
    if (showSavedBanner) {
        response->print(F("<script>showToast('Gespeichert. MQTT verbindet neu.')</script>"));
    }
    if (cfg.server[0] != '\0') {
        response->print(F("<p id='ms' class='hint' data-broker='"));
        appendHtmlEscaped(*response, cfg.server);
        response->print(F("' data-port='"));
        response->print(portBuf);
        response->print(F("'>Checking\xe2\x80\xa6</p>"));
    }
    response->print(F("<form method='post' action='/mqtt'>"
                      "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*response);
    response->print(F("'/><label for='srv'>Broker (hostname or IP)</label>"
                      "<input id='srv' name='mqtt_server' maxlength='127' value='"));
    appendHtmlEscaped(*response, cfg.server);
    response->print(F("'/>"
                      "<label for='prt'>Port</label>"
                      "<input id='prt' name='mqtt_port' type='number' min='1' max='65535' value='"));
    response->print(portBuf);
    response->print(F("'/>"
                      "<label for='usr'>Username (optional)</label>"
                      "<input id='usr' name='mqtt_user' maxlength='63' value='"));
    appendHtmlEscaped(*response, cfg.username);
    response->print(F("'/>"
                      "<label for='pw'>Password (optional)</label>"
                      "<input id='pw' name='mqtt_pass' type='password' maxlength='63' "
                      "autocomplete='current-password' "
                      "placeholder='"));
    if (cfg.password[0] != '\0') {
        response->print(F("(saved — leave blank to keep)"));
    }
    response->print(F("'/>"
                      "<label for='tpub'>Publish topic</label>"
                      "<input id='tpub' name='mqtt_topic_pub' maxlength='127' value='"));
    appendHtmlEscaped(*response, cfg.topicPub);
    response->print(F("'/>"
                      "<label for='tsub'>Subscribe topic</label>"
                      "<input id='tsub' name='mqtt_topic_sub' maxlength='127' value='"));
    appendHtmlEscaped(*response, cfg.topicSub);
    response->print(F("'/>"
                      "<button type='submit'>Save</button></form>"
                      "<a class='btn-back' href='/'>Back</a>"));
    if (cfg.server[0] != '\0') {
        response->print(F("<script>"));
        response->print(reinterpret_cast<const __FlashStringHelper*>(MQTT_STATUS_JS));
        response->print(F("</script>"));
    }
    response->print(F("</body></html>"));
    req->send(response);
}

void streamPairingPage(AsyncWebServerRequest* req, bool showSavedBanner, bool invalidPartner) {
    MqttConfig cfg{};
    if (showSavedBanner && mqttCfgHasUnappliedPending()) {
        mqttCfgPendingSnapshot(&cfg);
    } else {
        mqttCfgSnapshot(&cfg);
    }

    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));

    AsyncResponseStream* response = beginResponseStreamOr500(req, "text/html");
    if (response == nullptr) {
        return;
    }
    streamPageHeader(*response, "Pairing");
    response->print(F("<h1>Device Pairing</h1>"
                      "<p class='hint'>Scan the QR code on the partner device or enter its "
                      "6-character device ID below. MQTT topics are generated automatically.</p>"));
    if (showSavedBanner) {
        response->print(F("<script>showToast('Gespeichert. MQTT verbindet neu.')</script>"));
    }
    if (invalidPartner) {
        response->print(
            F("<p class='hint' style='color:#b00'>Invalid partner ID. Use 6 lowercase hex "
              "characters (0-9, a-f) and not your own device ID.</p>"));
    }
    response->print(F("<div class='pairing-panel'><h2>This device</h2><div id='qrcode'></div>"
                      "<p class='device-id-label'>Device ID</p>"
                      "<p class='device-id-value' id='device-id'>"));
    if (deviceIdSyntaxOk(ownId)) {
        appendHtmlEscaped(*response, ownId);
    } else {
        response->print(F("&mdash;"));
    }
    response->print(F("</p></div>"
                      "<form method='post' action='/pairing'>"
                      "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*response);
    response->print(F("'/><label for='partner_id'>Partner device ID</label>"
                      "<input id='partner_id' name='partner_id' maxlength='6' "
                      "autocapitalize='off' autocorrect='off' spellcheck='false' "
                      "inputmode='text' pattern='[0-9a-fA-F]{6}' "
                      "placeholder='a1b2c3' value='"));
    appendHtmlEscaped(*response, cfg.partnerDeviceId);
    response->print(F("'/><button type='submit'>Save pairing</button></form>"));
    if (cfg.partnerDeviceId[0] != '\0') {
        response->print(F("<div class='pairing-topics hint'><p>Publish: <code>"));
        appendHtmlEscaped(*response, cfg.topicPub);
        response->print(F("</code></p><p>Subscribe: <code>"));
        appendHtmlEscaped(*response, cfg.topicSub);
        response->print(F("</code></p></div>"));
    }
    response->print(F("<a class='btn-back' href='/'>Back</a>"));
    if (deviceIdSyntaxOk(ownId)) {
        response->print(F("<script>"));
        response->print(reinterpret_cast<const __FlashStringHelper*>(PAIRING_JS));
        response->print(F("</script>"));
    }
    response->print(F("</body></html>"));
    req->send(response);
}

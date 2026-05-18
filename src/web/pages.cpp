#include <Arduino.h>
#include <cstdio>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "pages.h"

#include "config/app_config.h"
#include "constants.h"
#include "mqtt/config.h"
#include "heart/counter.h"
#include "version.h"
#include "wifi/wlan.h"
#include "wifi/test.h"
#include "web_utils.h"
#include "web/assets/styles.h"
#include "web/assets/wifi_scan_js.h"
#include "web/assets/wifi_status_js.h"
#include "web/assets/mqtt_status_js.h"
#include "web/assets/wifi_connect_test_js.h"
#include "web/assets/common_js.h"
#include "web/assets/chaya_js.h"

static void printCommonCss(Print& out) {
    out.print(reinterpret_cast<const __FlashStringHelper*>(WEB_COMMON_CSS));
}

static void streamPageHeader(Print& out, const char* title) {
    out.print(F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
    appendHtmlEscaped(out, title);
    out.print(F("</title>"));
    printCommonCss(out);
    out.print(F("<script>"));
    out.print(reinterpret_cast<const __FlashStringHelper*>(COMMON_JS));
    out.print(F("</script></head><body><div id='toast' class='toast'></div>"));
}

void streamAuthPage(AsyncWebServerRequest* req, bool wrongCode, unsigned lockoutRemainSec) {
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "Sign in");
    resp->print(F("<h1>Web access</h1>"));
    if (lockoutRemainSec > 0) {
        const unsigned mins = lockoutRemainSec / 60U;
        const unsigned secs = lockoutRemainSec % 60U;
        resp->print(
            F("<p class='hint' style='color:#b00'>Too many incorrect attempts. "
              "Locked for "));
        resp->print(mins);
        resp->print(F("m "));
        resp->print(secs);
        resp->print(F("s.</p>"));
    } else if (wrongCode) {
        resp->print(F("<p class='hint' style='color:#b00'>Wrong code. Try again.</p>"));
    }
    resp->print(
        F("<p class='hint'>Authenticate on the device: press the physical button "
          "within 10&nbsp;s while this page is shown to reveal the "
          "<strong>six-digit code</strong> on the display, then enter it here.</p>"));
    String next = F("/");
    if (req->hasParam("next", false)) {
        const AsyncWebParameter* np = req->getParam("next", false);
        if (np != nullptr) {
            const String& v = np->value();
            if (v.length() > 0 && v[0] == '/' && v.indexOf("..") < 0) {
                next = v;
            }
        }
    }
    resp->print(F("<form method='post' action='/auth' autocapitalize='off'>"
                   "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*resp);
    resp->print(F("'/><input type='hidden' name='next' value='"));
    appendHtmlEscaped(*resp, next.c_str());
    resp->print(F("'/><label for='code'>Code</label>"
                   "<input id='code' name='code' inputmode='numeric' pattern='[0-9]{6}' "
                   "maxlength='6' required placeholder='000000'/>"
                   "<button type='submit'>Unlock</button></form>"
                   "</body></html>"));
    req->send(resp);
}

void streamSimpleDonePage(AsyncWebServerRequest* req, const char* title, const char* message) {
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, title);
    resp->print(F("<h1>"));
    appendHtmlEscaped(*resp, title);
    resp->print(F("</h1><p class='ok'>"));
    appendHtmlEscaped(*resp, message);
    resp->print(F("</p></body></html>"));
    req->send(resp);
}

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

void streamDashboard(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "Dashboard");
    resp->print(F("<h1>Chaya2MQTT</h1><div class='grid'>"));
    if (configIsApMode()) {
        resp->print(F("<a class='card' href='/wifi'>Wi-Fi Setup</a></div>"));
        {
            char bootFailSsid[kWifiSsidMaxLen];
            if (wlanLastStaBootFailureSsidSnapshot(bootFailSsid, sizeof(bootFailSsid))) {
                resp->print(
                    F("<p class='hint' style='color:#b00'>Verbindung mit dem gespeicherten WLAN &quot;"));
                appendHtmlEscaped(*resp, bootFailSsid);
                resp->print(F("&quot; ist fehlgeschlagen. Bitte WLAN erneut einrichten.</p>"));
            }
        }
        resp->print(F("<p class='hint'>Set up Wi-Fi first, then use MQTT etc. via "
                       "<strong>"));
        resp->print(kDeviceHttpOrigin);
        resp->print(F("</strong></p>"));
    } else {
        resp->print(F("<a class='card' href='/wifi'>Wi-Fi</a>"
                      "<a class='card' href='/mqtt'>MQTT</a>"
                      "<a class='card' href='/settings'>Settings</a>"
                      "<a class='card' href='/update'>OTA Update</a>"));
        if (configGetWebAuthEnabled()) {
            resp->print(F("<a class='card danger' href='/logout'>Logout</a>"));
        }
        resp->print(F("<form method='post' action='/reboot'>"
                       "<input type='hidden' name='csrf_token' value='"));
        appendCurrentWebCsrfTokenEscaped(*resp);
        resp->print(F("'/><button type='submit' class='card danger'>Reboot</button></form>"
                      "</div>"
                      "<div class='chaya-panel'><h2>"));
    resp->print(kDeviceHostname);
    resp->print(F("</h2>"
                      "<div class='chaya-counters'><div class='chaya-counter-box'>"
                      "<div class='chaya-counter-label'>Empfangen</div>"
                      "<div class='chaya-counter-val' id='chaya-rx'>&hellip;</div></div>"
                      "<div class='chaya-counter-box'><div class='chaya-counter-label'>Gesendet</div>"
                      "<div class='chaya-counter-val' id='chaya-tx'>&hellip;</div></div></div>"
                      "<form id='chaya-form' autocomplete='off'>"
                      "<input type='hidden' name='csrf_token' value='"));
        appendCurrentWebCsrfTokenEscaped(*resp);
        resp->print(
            F("'/><button type='submit' id='chaya-send-btn'>Senden</button></form></div>"
              "<script>"));
        resp->print(reinterpret_cast<const __FlashStringHelper*>(CHAYA_JS));
        resp->print(F("</script>"));
    }
    resp->print(F("</body></html>"));
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
    if (!configIsApMode() && configGetWebAuthEnabled()) {
        resp->print(F("<input type='hidden' name='csrf_token' value='"));
        appendCurrentWebCsrfTokenEscaped(*resp);
        resp->print(F("'/>"));
    }
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
                  "<button type='submit'>Back to Wi-Fi setup</button>"
                  "</form></div>"
                  "<form id='commitForm' method='post' action='/wifi-connect-commit'></form>"
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

    WlanScanRow rows[kWlanWifiScanCacheMaxRows];
    const size_t n = wlanWifiScanCopySnapshot(rows, sizeof(rows) / sizeof(rows[0]));

    AsyncResponseStream* resp = beginResponseStreamOr500(req, "application/json");
    if (resp == nullptr) {
        return;
    }
    resp->print('[');
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedCStr(*resp, rows[i].ssid);
        resp->print(F(",\"rssi\":"));
        resp->print(rows[i].rssi);
        resp->print(rows[i].open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    req->send(resp);

    wlanRequestWifiScanRefresh();
}

void streamUpdatePage(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "text/html");
    if (resp == nullptr) {
        return;
    }
    streamPageHeader(*resp, "OTA Update");
    resp->print(F("<h1>OTA Update</h1>"
                  "<p class='hint'>Installed firmware version: <strong>"));
    resp->print(APP_VERSION);
    resp->print(F("</strong></p>"
                  "<form method='post' action='/update-check'>"
                  "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*resp);
    resp->print(F("'/><button type='submit'>Check for Update</button>"
                  "</form>"
                  "<a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(resp);
}

void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
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

void streamSettingsPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    AsyncResponseStream* response = beginResponseStreamOr500(req, "text/html");
    if (response == nullptr) {
        return;
    }
    streamPageHeader(*response, "Settings");
    response->print(F("<h1>Settings</h1>"));
    if (showSavedBanner) {
        response->print(F("<script>showToast('Gespeichert.')</script>"));
    }
    response->print(F("<form method='post' action='/settings'>"
                      "<input type='hidden' name='csrf_token' value='"));
    appendCurrentWebCsrfTokenEscaped(*response);
    response->print(F("'/><label for='reset_days'>Display counter reset (days)</label>"
                       "<input type='number' id='reset_days' name='reset_days' min='0' max='30' value='"));
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(configGetResetPeriodDays()));
        appendHtmlEscaped(*response, buf);
    }
    response->print(F("'/><p class='hint'>0 = no periodic reset; 1–30 = reset baseline every N UTC days "
                       "(default 7). MQTT totals unchanged. Each side shows 0 again when its "
                       "display value reaches 999 (or &quot;999+&quot;).</p>"
                       "<label class='checkbox-label'><input type='checkbox' "
                       "name='auth_enabled' value='1'"));
    if (configGetWebAuthEnabled()) {
        response->print(F(" checked"));
    }
    response->print(F("/> Require 6-digit code on device to open web settings</label>"
                       "<button type='submit'>Save</button></form>"
                       "<a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(response);
}

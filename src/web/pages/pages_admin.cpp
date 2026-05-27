#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "pages.h"
#include "pages_internal.h"

#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"
#include "web/assets/chaya_js.h"
#include "../web_utils.h"

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
                      "<a class='card' href='/pairing'>Pairing</a>"
                      "<a class='card' href='/settings'>Settings</a>"
                      "<a class='card' href='/update'>OTA Update</a>"));
        if (configGetWebAuthEnabled()) {
            resp->print(F("<form method='post' action='/logout'>"
                          "<input type='hidden' name='csrf_token' value='"));
            appendCurrentWebCsrfTokenEscaped(*resp);
            resp->print(F("'/><button type='submit' class='card danger'>Logout</button></form>"));
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

#include "web_pages.h"

#include "config.h"
#include "version.h"
#include "web_styles.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <cstdio>

static void appendHtmlEscaped(Print& out, const char* s) {
    if (s == nullptr) {
        return;
    }
    for (; *s != '\0'; ++s) {
        switch (*s) {
            case '&': out.print(F("&amp;")); break;
            case '"': out.print(F("&quot;")); break;
            case '<': out.print(F("&lt;")); break;
            case '>': out.print(F("&gt;")); break;
            default: out.print(*s); break;
        }
    }
}

static void appendJsonEscapedString(Print& out, const String& str) {
    out.print('"');
    for (size_t i = 0; i < str.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(str[i]);
        switch (c) {
            case '"': out.print(F("\\\"")); break;
            case '\\': out.print(F("\\\\")); break;
            case '\b': out.print(F("\\b")); break;
            case '\f': out.print(F("\\f")); break;
            case '\n': out.print(F("\\n")); break;
            case '\r': out.print(F("\\r")); break;
            case '\t': out.print(F("\\t")); break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out.print(buf);
                } else {
                    out.write(static_cast<char>(c));
                }
                break;
        }
    }
    out.print('"');
}

static void printCommonCss(Print& out) {
    out.print(reinterpret_cast<const __FlashStringHelper*>(WEB_COMMON_CSS));
}

static void streamPageHeader(Print& out, const char* title) {
    out.print(F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
    out.print(title);
    out.print(F("</title>"));
    printCommonCss(out);
    out.print(F("</head><body>"));
}

void streamSimpleDonePage(AsyncWebServerRequest* req, const char* title, const char* message) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, title);
    resp->print(F("<h1>"));
    resp->print(title);
    resp->print(F("</h1><p class='ok'>"));
    resp->print(message);
    resp->print(F("</p></body></html>"));
    req->send(resp);
}

void streamDashboard(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "Dashboard");
    resp->print(F("<h1>Chaya2MQTT</h1><div class='grid'><a class='card' href='/wifi'>Wi-Fi</a>"
                  "<a class='card' href='/mqtt'>MQTT</a>"
                  "<a class='card' href='/update'>Firmware Update</a>"
                  "<form method='post' action='/reboot'>"
                  "<button type='submit' class='card danger'>Reboot</button></form>"
                  "</div></body></html>"));
    req->send(resp);
}

void streamWifiPage(AsyncWebServerRequest* req) {
    WiFi.scanDelete();
#ifndef ESP8266
    WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
#else
    WiFi.scanNetworks(true);
#endif

    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "Wi-Fi");
    resp->print(F("<h1>Wi-Fi Setup</h1>"));
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        resp->print(F("<p class='hint'>Connected: <strong>"));
        appendHtmlEscaped(*resp, WiFi.SSID().c_str());
        resp->print(F("</strong>, IP "));
        resp->print(WiFi.localIP().toString());
        resp->print(F(", RSSI "));
        resp->print(static_cast<int>(WiFi.RSSI()));
        resp->print(F(" dBm</p>"));
    }
    resp->print(
        F("<p class='hint' id='st'>Scanning…</p><ul id='list'></ul>"
          "<form method='post' action='/wifi-connect' id='wf'>"
          "<label for='ssid'>SSID</label>"
          "<input name='ssid' id='ssid' required maxlength='32' autocomplete='off'/>"
          "<label for='pwd'>Password</label>"
          "<input name='password' id='pwd' type='password' maxlength='64' autocomplete='current-password'/>"
          "<button type='submit'>Connect</button></form>"
          "<script>"
          "(function(){"
          "var ss=document.getElementById('ssid'),lst=document.getElementById('list'),st=document.getElementById('st');"
          "function poll(){"
          "fetch('/wifi-scan').then(function(r){"
          "if(r.status===202){st.textContent='Scanning…';return Promise.resolve(null);}"
          "return r.json();"
          "}).then(function(rows){"
          "if(rows===null)return;"
          "lst.innerHTML='';"
          "if(!rows||!rows.length){st.textContent='No networks found.';return;}"
          "st.textContent='Click a network, enter password, press Connect.';"
          "for(var i=0;i<rows.length;i++){"
          "var li=document.createElement('li');"
          "var a=document.createElement('a');a.href='#';"
          "(function(nm){"
          "a.onclick=function(ev){ev.preventDefault();ss.value=nm;return false};"
          "})(rows[i].ssid);"
          "var o=rows[i].open?', open':'';"
          "a.textContent=rows[i].ssid+' ('+rows[i].rssi+' dBm'+o+')';"
          "li.appendChild(a);lst.appendChild(li);"
          "} "
          "}).catch(function(){st.textContent='Scan error.'});"
          "} setInterval(poll,1500);poll();})();"
          "</script><a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(resp);
}

void handleWifiScanJson(AsyncWebServerRequest* req) {
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        req->send(202);
        return;
    }
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
#ifndef ESP8266
        WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
#else
        WiFi.scanNetworks(true);
#endif
        req->send(202);
        return;
    }

    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    resp->print('[');
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedString(*resp, WiFi.SSID(i));
        resp->print(F(",\"rssi\":"));
        resp->print(static_cast<int>(WiFi.RSSI(i)));
        const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        resp->print(open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    WiFi.scanDelete();
    req->send(resp);
}

void streamUpdatePage(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "Firmware");
    resp->print(F("<h1>Firmware Update</h1>"
                  "<p class='hint'>Installed firmware (tag): <strong>"));
    resp->print(APP_VERSION);
    resp->print(F("</strong>.</p>"
                  "<form method='post' action='/update-check'>"
                  "<button type='submit'>Check for Update</button>"
                  "</form>"
                  "<h2>Custom</h2>"
                  "<form method='post' action='/update'><label for='url'>URL</label>"
                  "<input id='url' name='url' type='url' required placeholder='https://…'/>"
                  "<button type='submit'>Update</button></form>"
                  "<a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(resp);
}

void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    AsyncResponseStream* response = req->beginResponseStream("text/html");
    streamPageHeader(*response, "MQTT");
    response->print(F("<h1>MQTT Settings</h1>"));
    if (showSavedBanner) {
        response->print(F("<p class='ok'>&#10003; Saved. MQTT will reconnect.</p>"));
    }
    response->print(F("<form method='post' action='/mqtt'>"
                      "<label for='srv'>Broker (hostname or IP)</label>"
                      "<input id='srv' name='mqtt_server' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.server);
    char portBuf[8];
    snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(mqttCfg.port));
    response->print(F("'/>"
                      "<label for='prt'>Port</label>"
                      "<input id='prt' name='mqtt_port' type='number' min='1' max='65535' value='"));
    response->print(portBuf);
    response->print(F("'/>"
                      "<label for='usr'>Username (optional)</label>"
                      "<input id='usr' name='mqtt_user' maxlength='63' value='"));
    appendHtmlEscaped(*response, mqttCfg.username);
    response->print(F("'/>"
                      "<label for='pw'>Password (optional)</label>"
                      "<input id='pw' name='mqtt_pass' type='password' maxlength='63' "
                      "autocomplete='current-password' value='"));
    appendHtmlEscaped(*response, mqttCfg.password);
    response->print(F("'/>"
                      "<label for='tpub'>Publish topic</label>"
                      "<input id='tpub' name='mqtt_topic_pub' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.topicPub);
    response->print(F("'/>"
                      "<label for='tsub'>Subscribe topic</label>"
                      "<input id='tsub' name='mqtt_topic_sub' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.topicSub);
    response->print(F("'/>"
                      "<button type='submit'>Save</button></form>"
                      "<a class='btn-back' href='/'>Back</a></body></html>"));
    req->send(response);
}

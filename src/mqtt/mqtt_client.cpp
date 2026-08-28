#include "mqtt_internal.h"

#include "async/task_config.h"
#include "constants.h"
#include "device_identity.h"
#include "mqtt_config.h"
#include "tls/tls_bundle.h"
#include "tls/tls_bundle_setup.h"

#include <cstdio>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "async/task_handles.h"
#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("MQTT");

namespace {
constexpr uint64_t kMqttTeardownDeadlineUs = 30000000ULL;

void mqttTeardownDeadlineCb(void*) {
    ESP_LOGE(TAG, "MQTT teardown exceeded 30 s — restarting");
    esp_restart();
}
} // namespace

esp_mqtt_client_handle_t s_client = nullptr;
std::atomic<uint32_t>    s_clientGeneration{0};
std::atomic<bool>        s_connected{false};
std::atomic<bool>        s_connectPending{false};
std::atomic<bool>        s_disconnectIntentional{false};
std::atomic<bool>        s_mqttKillCoalesce{false};

char         s_clientIdBuf[24]{};
char         s_lwtTopicBuf[sizeof(MqttConfig::topicPub) + 16U]{};
char         s_mqttSubTopicCache[sizeof(MqttConfig::topicSub)]{};
size_t       s_mqttSubTopicLen = 0;
portMUX_TYPE s_mqttSubTopicMux = portMUX_INITIALIZER_UNLOCKED;

bool mqttClientLockTimed() {
    if (g_mqttClientMutex == nullptr) {
        return true;
    }
    return xSemaphoreTake(g_mqttClientMutex, kMqttClientLockTimeoutTicks) == pdTRUE;
}

void mqttClientLock() {
    if (g_mqttClientMutex != nullptr) {
        xSemaphoreTake(g_mqttClientMutex, portMAX_DELAY);
    }
}

void mqttClientUnlock() {
    if (g_mqttClientMutex != nullptr) {
        xSemaphoreGive(g_mqttClientMutex);
    }
}

static esp_err_t installCaBundleLocked() {
    return chayaTlsEnsureCaBundleInstalled() ? ESP_OK : ESP_FAIL;
}

static void mqttFillStableClientId() {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    if (deviceIdSyntaxOk(deviceId)) {
        static_cast<void>(snprintf(s_clientIdBuf, sizeof(s_clientIdBuf), "Chaya2MQTT-%s", deviceId));
    } else {
        static_cast<void>(snprintf(s_clientIdBuf, sizeof(s_clientIdBuf), "Chaya2MQTT-%04lX",
                                   static_cast<unsigned long>(esp_random() & 0xffffU)));
    }
}

bool mqttEnsureClientAllocated() {
    mqttClientLock();
    if (s_client != nullptr) {
        mqttClientUnlock();
        return true;
    }

    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    mqttFillStableClientId();

    snprintf(s_lwtTopicBuf, sizeof(s_lwtTopicBuf), "%s/lwt", cfg.topicPub);

    if (installCaBundleLocked() != ESP_OK) {
        ESP_LOGE(TAG, "esp_crt_bundle_set failed");
        mqttClientUnlock();
        return false;
    }

    const char* mqttUser = nullptr;
    if (cfg.username[0] != '\0') {
        mqttUser = cfg.username;
    } else if (cfg.password[0] != '\0') {
        mqttUser = "";
    }
    const char* mqttPassword = (cfg.password[0] != '\0') ? cfg.password : nullptr;

    constexpr const char kOffline[]  = "offline";
    constexpr int        kOfflineLen = sizeof(kOffline) - 1;

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.hostname = cfg.server;
    mqtt_cfg.broker.address.port     = cfg.port;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.address.uri       = nullptr;
    mqtt_cfg.broker.address.path      = nullptr;

    mqtt_cfg.broker.verification.use_global_ca_store        = false;
    mqtt_cfg.broker.verification.crt_bundle_attach          = esp_crt_bundle_attach;
    mqtt_cfg.broker.verification.certificate                = nullptr;
    mqtt_cfg.broker.verification.certificate_len            = 0;
    mqtt_cfg.broker.verification.skip_cert_common_name_check = false;

    mqtt_cfg.credentials.username                = mqttUser;
    mqtt_cfg.credentials.client_id               = s_clientIdBuf;
    mqtt_cfg.credentials.set_null_client_id      = false;
    mqtt_cfg.credentials.authentication.password = mqttPassword;

    mqtt_cfg.session.disable_clean_session        = false;
    mqtt_cfg.session.keepalive                    = kMqttKeepAliveSeconds;
    mqtt_cfg.session.disable_keepalive            = false;
    mqtt_cfg.session.protocol_ver                 = MQTT_PROTOCOL_UNDEFINED;
    mqtt_cfg.session.last_will.topic              = s_lwtTopicBuf;
    mqtt_cfg.session.last_will.msg                = kOffline;
    mqtt_cfg.session.last_will.msg_len              = kOfflineLen;
    mqtt_cfg.session.last_will.qos                  = 1;
    mqtt_cfg.session.last_will.retain               = true;
    mqtt_cfg.session.message_retransmit_timeout     = kMqttPublishAckWaitMs;

    mqtt_cfg.network.disable_auto_reconnect = true;
    mqtt_cfg.network.reconnect_timeout_ms   = kMqttReconnectTimeoutMs;
    mqtt_cfg.network.timeout_ms             = kMqttWifiNetworkTimeoutMs;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_enable  = true;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_idle     = 60;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_interval = 20;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_count    = 3;

    mqtt_cfg.task.priority   = 5;
    mqtt_cfg.task.stack_size = kMqttClientTaskStackBytes;

    mqtt_cfg.buffer.size     = 512;
    mqtt_cfg.buffer.out_size = 512;
    mqtt_cfg.outbox.limit    = kMqttOutboxLimitBytes;

    ESP_LOGI(TAG, "MQTT client init TLS… server %s:%u id %s", cfg.server,
             static_cast<unsigned>(cfg.port), s_clientIdBuf);

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        mqttClientUnlock();
        return false;
    }

    const esp_err_t regErr =
        esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
    if (regErr != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(regErr));
        mqttKillClientImpl();
        mqttClientUnlock();
        return false;
    }

    mqttClientUnlock();
    return true;
}

void mqttKillClientImpl() {
    if (s_client == nullptr) {
        return;
    }

    const uint32_t genBefore = s_clientGeneration.load(std::memory_order_acquire);
    const int64_t teardownStartUs = esp_timer_get_time();
    ESP_LOGI(TAG, "MQTT teardown begin gen=%u", static_cast<unsigned>(genBefore));

    s_disconnectIntentional.store(true, std::memory_order_release);

    esp_mqtt_client_handle_t cli = s_client;
    mqttAbortPendingPublish(genBefore);
    s_client                     = nullptr;
    s_clientGeneration.fetch_add(1U, std::memory_order_acq_rel);
    portENTER_CRITICAL(&s_mqttSubTopicMux);
    s_mqttSubTopicLen      = 0;
    s_mqttSubTopicCache[0] = '\0';
    portEXIT_CRITICAL(&s_mqttSubTopicMux);

    // stop/destroy may legitimately exceed the default TWDT while TLS unwinds.
    // A separate deadline still recovers a true deadlock.
    esp_timer_handle_t teardownDeadline = nullptr;
    esp_timer_create_args_t deadlineArgs{};
    deadlineArgs.callback = mqttTeardownDeadlineCb;
    deadlineArgs.dispatch_method = ESP_TIMER_TASK;
    deadlineArgs.name = "mqtt_teardown";
    deadlineArgs.skip_unhandled_events = true;
    bool deadlineArmed =
        esp_timer_create(&deadlineArgs, &teardownDeadline) == ESP_OK
        && esp_timer_start_once(teardownDeadline, kMqttTeardownDeadlineUs) == ESP_OK;
    if (!deadlineArmed && teardownDeadline != nullptr) {
        (void)esp_timer_delete(teardownDeadline);
        teardownDeadline = nullptr;
    }

    // Preserve the caller's subscription state: setup may call this outside
    // the subscribed network task. Without a deadline, retain normal TWDT coverage.
    const bool watchdogWasSubscribed = esp_task_wdt_status(nullptr) == ESP_OK;
    if (watchdogWasSubscribed && deadlineArmed) {
        chayaTaskWatchdogUnsubscribe(TAG);
    }
    const esp_err_t st = esp_mqtt_client_stop(cli);
    if (st != ESP_OK && st != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_mqtt_client_stop: %s", esp_err_to_name(st));
    }
    mqttResetFragmentState();

    const esp_err_t de = esp_mqtt_client_destroy(cli);
    if (de != ESP_OK) {
        ESP_LOGW(TAG, "esp_mqtt_client_destroy: %s", esp_err_to_name(de));
    }
    if (deadlineArmed) {
        (void)esp_timer_stop(teardownDeadline);
        (void)esp_timer_delete(teardownDeadline);
    }
    if (watchdogWasSubscribed && deadlineArmed) {
        chayaTaskWatchdogSubscribe(TAG);
        chayaTaskWatchdogReset();
    }

    s_disconnectIntentional.store(false, std::memory_order_release);
    s_connected.store(false, std::memory_order_release);
    s_connectPending.store(false, std::memory_order_release);

    const unsigned long durMs =
        static_cast<unsigned long>((esp_timer_get_time() - teardownStartUs) / 1000LL);
    ESP_LOGI(TAG, "MQTT teardown end gen=%u dur=%lu ms stop=%s destroy=%s",
             static_cast<unsigned>(genBefore), durMs, esp_err_to_name(st), esp_err_to_name(de));
}

void mqttKillClient() {
    if (!mqttClientLockTimed()) {
        ESP_LOGW(TAG, "mqttKillClient: mutex timeout");
        s_mqttKillCoalesce.store(true, std::memory_order_release);
        return;
    }
    mqttKillClientImpl();
    mqttClientUnlock();
}

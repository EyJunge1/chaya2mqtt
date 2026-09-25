#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "async/system_lifecycle.h"
#include "web/admin_restart_pure.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <variant>

extern std::atomic<bool> g_webAdminRebootRequested;
extern std::atomic<bool> g_webAdminWifiReconnectRequested;
extern std::atomic<uint32_t> g_webAdminMqttApplyVersion;
extern std::atomic<uint32_t> g_webAdminSettingsApplyVersion;
extern std::atomic<bool> g_webAdminSettingsApplyPending;
extern std::atomic<bool> g_webAdminSettingsNvsWriteFailed;
extern std::atomic<uint32_t> g_webAdminApplyInFlight;

/** Holds apply-in-flight so restart waits; fails closed if shutdown wins the race. */
class ScopedWebAdminApplyInFlight {
  public:
    ScopedWebAdminApplyInFlight() {
        if (g_systemShutdownInProgress.load(std::memory_order_acquire) || g_factoryResetQueued.load(std::memory_order_acquire)) {
            armed_ = false;
            return;
        }
        g_webAdminApplyInFlight.fetch_add(1U, std::memory_order_acq_rel);
        if (g_systemShutdownInProgress.load(std::memory_order_acquire) || g_factoryResetQueued.load(std::memory_order_acquire)) {
            g_webAdminApplyInFlight.fetch_sub(1U, std::memory_order_acq_rel);
            armed_ = false;
            return;
        }
        armed_ = true;
    }
    ~ScopedWebAdminApplyInFlight() {
        if (armed_) {
            g_webAdminApplyInFlight.fetch_sub(1U, std::memory_order_acq_rel);
        }
    }
    explicit operator bool() const { return armed_; }

    bool commitAllowed() const {
        return webAdminApplyCommitAllowed(g_systemShutdownInProgress.load(std::memory_order_acquire), armed_,
                                          g_factoryResetQueued.load(std::memory_order_acquire));
    }

    ScopedWebAdminApplyInFlight(const ScopedWebAdminApplyInFlight &) = delete;
    ScopedWebAdminApplyInFlight &operator=(const ScopedWebAdminApplyInFlight &) = delete;
    ScopedWebAdminApplyInFlight(ScopedWebAdminApplyInFlight &&) = delete;
    ScopedWebAdminApplyInFlight &operator=(ScopedWebAdminApplyInFlight &&) = delete;

  private:
    bool armed_ = false;
};

extern uint8_t g_webAdminPendingResetDays;
extern char g_webAdminPendingUiLang[3];
extern char g_webAdminPendingUiTheme[8];
extern bool g_webAdminPendingLedEnabled;
extern bool g_webAdminPendingAudioTxEnabled;
extern bool g_webAdminPendingAudioRxEnabled;
extern uint8_t g_webAdminPendingAudioTxVolume;
extern uint8_t g_webAdminPendingAudioRxVolume;
extern uint8_t g_webAdminPendingQuiet0;
extern uint8_t g_webAdminPendingQuiet1;
extern uint16_t g_webAdminPendingTxHz;
extern uint16_t g_webAdminPendingTxMs;
extern uint16_t g_webAdminPendingRxHz;
extern uint16_t g_webAdminPendingRxMs;
extern portMUX_TYPE g_webAdminSettingsPendingMux;

/** Present-but-invalid optional JSON field. */
enum class AdminJsonError : uint8_t { Invalid };

/** nullopt = absent, value = ok, unexpected = invalid. */
template <typename T> using AdminJsonResult = std::expected<std::optional<T>, AdminJsonError>;

bool adminJsonHasField(JsonVariantConst obj, const char *name);

[[nodiscard]] auto adminOptionalJsonInt(JsonVariantConst obj, const char *name) -> AdminJsonResult<int>;
[[nodiscard]] auto adminOptionalJsonBool(JsonVariantConst obj, const char *name) -> AdminJsonResult<bool>;
/** Absent if missing; Ok if copied into out; Invalid if present but not a fitting string. */
[[nodiscard]] auto adminOptionalJsonString(JsonVariantConst obj, const char *name, char *out, size_t outLen)
    -> AdminJsonResult<std::monostate>;

/**
 * Apply optional JSON fields (Absent leaves *out unchanged).
 * Returns false when the field is present but invalid or fails predicate — caller sendErr(name).
 */
bool adminApplyOptionalInt(JsonVariantConst obj, const char *name, bool (*inRange)(int), int *out);
bool adminApplyOptionalU8(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint8_t *out);
bool adminApplyOptionalU16(JsonVariantConst obj, const char *name, bool (*inRange)(int), uint16_t *out);
bool adminApplyOptionalBool(JsonVariantConst obj, const char *name, bool *out);
/** syntaxOk may be nullptr (any fitting string accepted). */
bool adminApplyOptionalString(JsonVariantConst obj, const char *name, char *out, size_t outLen,
                              bool (*syntaxOk)(const char *) = nullptr);

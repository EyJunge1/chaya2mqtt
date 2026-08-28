#include "audio.h"

#include "audio_config.h"
#include "audio_pure.h"

#include "async/task_config.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"
#include "hw/pins.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("AUDIO");

namespace {

bool     s_codecPresent = false;
uint32_t s_stackLog     = 0;
std::atomic<bool> s_txPending{false};
std::atomic<bool> s_rxPending{false};

/** Waveshare 07_Audio_out: PA_EN LOW = audio power on; PA_CTRL HIGH = amp on. */
void paAudioPowerOff() {
    digitalWrite(pins::kPaCtrl, LOW);
    digitalWrite(pins::kPaEn, HIGH);
}

void paAudioPowerOnAmpOff() {
    digitalWrite(pins::kPaCtrl, LOW);
    digitalWrite(pins::kPaEn, LOW);
}

/** Same levels as Waveshare 07_Audio_out setup(). */
void paAudioPowerAndAmpOn() {
    digitalWrite(pins::kPaEn, LOW);
    digitalWrite(pins::kPaCtrl, HIGH);
}

bool es8311Write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kEs8311I2cAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

/** Mic/ADC off without clearing SYSTEM_REG0E (analog path needed for DAC). */
void es8311MuteMicOnly() {
    (void)es8311Write(0x14, 0x00);
    (void)es8311Write(0x16, 0x00);
    (void)es8311Write(0x17, 0x00);
}

bool es8311ProbeAndMuteMic() {
    paAudioPowerOnAmpOff();
    delay(8);
    if (!es8311Write(0x00, 0x1F)) {
        paAudioPowerOff();
        return false;
    }
    delay(20);
    (void)es8311Write(0x00, 0x00);
    es8311MuteMicOnly();
    paAudioPowerOff();
    return true;
}

/** Espressif/Waveshare volume: ((vol * 256) / 100) - 1 for vol 1..100. */
uint8_t dacVolumeRegister(uint8_t volume0to100) {
    if (volume0to100 == 0U) {
        return 0;
    }
    return static_cast<uint8_t>((static_cast<uint16_t>(volume0to100) * 256U) / 100U - 1U);
}

/**
 * Register sequence aligned with Waveshare ESP32-S3-ePaper-1.54G 07_Audio_out
 * (es8311_init + clock coeffs for MCLK = 256×fs).
 */
bool es8311PreparePlayback(uint8_t volume0to100) {
    if (!es8311Write(0x00, 0x1F)) {
        return false;
    }
    delay(20);
    (void)es8311Write(0x00, 0x00);
    (void)es8311Write(0x00, 0x80);  // CSM on before clock/fmt (Waveshare es8311_init)
    // Clock: MCLK from pin, all clocks on; 24 kHz @ MCLK 6.144 MHz (256×fs).
    (void)es8311Write(0x01, 0x3F);
    (void)es8311Write(0x02, 0x00);  // pre_div=1, pre_multi=1x
    (void)es8311Write(0x03, 0x10);  // adc_osr
    (void)es8311Write(0x04, 0x10);  // dac_osr
    (void)es8311Write(0x05, 0x00);  // adc/dac div = 1
    (void)es8311Write(0x06, 0x03);  // bclk_div = 4
    (void)es8311Write(0x07, 0x00);
    (void)es8311Write(0x08, 0xFF);  // lrck divider
    // SDP 16-bit I2S slave (resolution bits = 3 << 2).
    (void)es8311Write(0x09, 0x0C);
    (void)es8311Write(0x0A, 0x0C);
    (void)es8311Write(0x0D, 0x01);  // analog power up
    (void)es8311Write(0x0E, 0x02);  // analog PGA / path (must not clear for DAC)
    es8311MuteMicOnly();
    (void)es8311Write(0x12, 0x00);  // DAC power up
    (void)es8311Write(0x13, 0x10);  // HP drive
    (void)es8311Write(0x1C, 0x6A);
    (void)es8311Write(0x37, 0x08);
    (void)es8311Write(0x32, dacVolumeRegister(volume0to100));
    (void)es8311Write(0x31, 0x00);  // unmute
    return true;
}

void es8311MuteAndSleep() {
    (void)es8311Write(0x31, 0x60);
    es8311MuteMicOnly();
}

bool i2sStart(i2s_chan_handle_t* outTx) {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_handle_t tx      = nullptr;
    if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
        return false;
    }
    i2s_std_config_t stdCfg = {};
    stdCfg.clk_cfg               = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRateHz);
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    // Waveshare 07_Audio_out: mono left (not stereo).
    stdCfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    stdCfg.gpio_cfg.mclk      = static_cast<gpio_num_t>(pins::kI2sMclk);
    stdCfg.gpio_cfg.bclk      = static_cast<gpio_num_t>(pins::kI2sSclk);
    stdCfg.gpio_cfg.ws        = static_cast<gpio_num_t>(pins::kI2sLrck);
    stdCfg.gpio_cfg.dout      = static_cast<gpio_num_t>(pins::kI2sDsdin);
    stdCfg.gpio_cfg.din       = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK) {
        (void)i2s_del_channel(tx);
        return false;
    }
    if (i2s_channel_enable(tx) != ESP_OK) {
        (void)i2s_del_channel(tx);
        return false;
    }
    *outTx = tx;
    return true;
}

void i2sStop(i2s_chan_handle_t tx) {
    if (tx == nullptr) {
        return;
    }
    (void)i2s_channel_disable(tx);
    (void)i2s_del_channel(tx);
}

void writeTone(i2s_chan_handle_t tx, uint32_t durationMs, float hz, float amplitude) {
    static constexpr size_t kChunkFrames = 128;
    int16_t                 mono[kChunkFrames];
    const float             twoPi = 6.28318530718f;
    const uint32_t          total = (kAudioSampleRateHz * durationMs) / 1000U;
    uint32_t                done  = 0;
    while (done < total) {
        const size_t n = ((total - done) < kChunkFrames) ? (total - done) : kChunkFrames;
        for (size_t i = 0; i < n; ++i) {
            const float t   = static_cast<float>(done + i) / static_cast<float>(kAudioSampleRateHz);
            const float env = 1.0f - (static_cast<float>(done + i) / static_cast<float>(total));
            const float s   = sinf(twoPi * hz * t) * amplitude * env;
            mono[i]         = static_cast<int16_t>(s * 28000.0f);
        }
        size_t written = 0;
        (void)i2s_channel_write(tx, mono, n * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
        done += static_cast<uint32_t>(n);
    }
}

void writeSilence(i2s_chan_handle_t tx, uint32_t durationMs) {
    static constexpr size_t kChunkFrames = 128;
    int16_t                 mono[kChunkFrames]{};
    const uint32_t          total = (kAudioSampleRateHz * durationMs) / 1000U;
    uint32_t                done  = 0;
    while (done < total) {
        const size_t n = ((total - done) < kChunkFrames) ? (total - done) : kChunkFrames;
        size_t       written = 0;
        (void)i2s_channel_write(tx, mono, n * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
        done += static_cast<uint32_t>(n);
    }
}

void playBuiltinKind(i2s_chan_handle_t tx, AudioMsg::Kind /*kind*/, float amp) {
    // Amp/DAC need a short silent buffer after power-up or the first samples are lost.
    writeSilence(tx, 80);
    writeTone(tx, 120, 880.0f, 0.95f * amp);
}

void playConfiguredKind(i2s_chan_handle_t tx, AudioMsg::Kind kind, float amp) {
    writeSilence(tx, 80);
    if (kind == AudioMsg::Kind::Tx) {
        writeTone(tx, configGetAudioTxMs(), static_cast<float>(configGetAudioTxHz()), 0.95f * amp);
    } else {
        writeTone(tx, configGetAudioRxMs(), static_cast<float>(configGetAudioRxHz()), 0.95f * amp);
    }
}

bool playbackAllowedNow() {
    uint8_t hour = 0;
    const bool synced = wlanNtpSynced();
    if (synced) {
        const time_t now = time(nullptr);
        struct tm    t{};
        if (localtime_r(&now, &t) != nullptr) {
            hour = static_cast<uint8_t>(t.tm_hour);
        }
    }
    return audioPlaybackAllowed(configGetAudioMuted(), configGetAudioVolume(), synced, hour,
                                configGetAudioQuietStart(), configGetAudioQuietEnd());
}

void playKind(AudioMsg::Kind kind) {
    if (!s_codecPresent) {
        ESP_LOGD(TAG, "skip %s — codec absent", kind == AudioMsg::Kind::Tx ? "Tx" : "Rx");
        return;
    }
    if (!playbackAllowedNow()) {
        ESP_LOGI(TAG, "skip %s — muted/vol0/quiet hours",
                 kind == AudioMsg::Kind::Tx ? "Tx" : "Rx");
        return;
    }
    ESP_LOGI(TAG, "play %s", kind == AudioMsg::Kind::Tx ? "Tx" : "Rx");
    const uint8_t vol = configGetAudioVolume();
    // Waveshare: PA_EN LOW + PA_CTRL HIGH before codec/I2S use.
    paAudioPowerAndAmpOn();
    delay(40);
    i2s_chan_handle_t tx = nullptr;
    if (!i2sStart(&tx)) {
        ESP_LOGW(TAG, "I2S start failed");
        paAudioPowerOff();
        return;
    }
    if (!es8311PreparePlayback(vol)) {
        ESP_LOGW(TAG, "ES8311 playback setup failed");
        i2sStop(tx);
        paAudioPowerOff();
        return;
    }
    delay(30);
    const float amp = static_cast<float>(vol) / 100.0f;
    if (configGetAudioCustom()) {
        playConfiguredKind(tx, kind, amp);
    } else {
        playBuiltinKind(tx, kind, amp);
    }
    es8311MuteAndSleep();
    delay(4);
    paAudioPowerOff();
    i2sStop(tx);
    ESP_LOGI(TAG, "done %s", kind == AudioMsg::Kind::Tx ? "Tx" : "Rx");
}

void audioTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    for (;;) {
        AudioMsg msg{};
        if (xQueueReceive(g_audioCmdQueue, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            playKind(msg.kind);
        }
        if (s_txPending.exchange(false, std::memory_order_acq_rel)) {
            playKind(AudioMsg::Kind::Tx);
        }
        if (s_rxPending.exchange(false, std::memory_order_acq_rel)) {
            playKind(AudioMsg::Kind::Rx);
        }
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLog, 120);
    }
}

}  // namespace

void audioInit() {
    pinMode(pins::kPaEn, OUTPUT);
    pinMode(pins::kPaCtrl, OUTPUT);
    paAudioPowerOff();
    Wire.begin(pins::kI2cSda, pins::kI2cScl);
    Wire.setClock(100000);
    s_codecPresent = es8311ProbeAndMuteMic();
    if (!s_codecPresent) {
        ESP_LOGW(TAG, "ES8311 not found — audio disabled");
        paAudioPowerOff();
        return;
    }
    ESP_LOGI(TAG, "ES8311 capture path off; amp idle");
}

void audioStartTask() {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(audioTaskFn, "audio", kAudioTaskStackBytes, nullptr, 3, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        abort();
    }
}

void audioRequest(AudioMsg::Kind kind) {
    if (g_audioCmdQueue == nullptr) {
        return;
    }
    AudioMsg msg{kind};
    if (xQueueSend(g_audioCmdQueue, &msg, 0) != pdTRUE) {
        std::atomic<bool>& pending = kind == AudioMsg::Kind::Tx ? s_txPending : s_rxPending;
        pending.store(true, std::memory_order_release);
        ESP_LOGD(TAG, "audio queue full — coalescing pending tone");
    }
}

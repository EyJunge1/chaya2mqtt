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

constexpr uint8_t kDacVolRegMax = 0xBF;

bool     s_codecPresent = false;
uint32_t s_stackLog     = 0;

void paPinsLow() {
    digitalWrite(pins::kPaCtrl, LOW);
    digitalWrite(pins::kPaEn, LOW);
}

void paCodecOnAmpOff() {
    digitalWrite(pins::kPaCtrl, LOW);
    digitalWrite(pins::kPaEn, HIGH);
}

void paCodecAndAmpOn() {
    digitalWrite(pins::kPaEn, HIGH);
    digitalWrite(pins::kPaCtrl, HIGH);
}

bool es8311Write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kEs8311I2cAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void es8311DisableCapture() {
    (void)es8311Write(0x14, 0x00);
    (void)es8311Write(0x0E, 0x00);
    (void)es8311Write(0x16, 0x00);
    (void)es8311Write(0x17, 0x00);
}

bool es8311ProbeAndMuteMic() {
    paCodecOnAmpOff();
    delay(8);
    if (!es8311Write(0x00, 0x1F)) {
        paPinsLow();
        return false;
    }
    delay(5);
    (void)es8311Write(0x00, 0x00);
    es8311DisableCapture();
    paPinsLow();
    return true;
}

uint8_t dacVolumeRegister(uint8_t volume0to100) {
    if (volume0to100 == 0U) {
        return 0;
    }
    return static_cast<uint8_t>((static_cast<uint16_t>(volume0to100) * kDacVolRegMax) / 100U);
}

bool es8311PreparePlayback(uint8_t volume0to100) {
    if (!es8311Write(0x00, 0x1F)) {
        return false;
    }
    delay(5);
    (void)es8311Write(0x00, 0x00);
    // 16 kHz, MCLK = 256×fs = 4.096 MHz (ESPHome / ESP-ADF coefficients).
    (void)es8311Write(0x01, 0x3F);
    (void)es8311Write(0x02, 0x08);
    (void)es8311Write(0x03, 0x10);
    (void)es8311Write(0x04, 0x20);
    (void)es8311Write(0x05, 0x00);
    (void)es8311Write(0x06, 0x03);
    (void)es8311Write(0x07, 0x00);
    (void)es8311Write(0x08, 0xFF);
    (void)es8311Write(0x09, 0x0C);
    (void)es8311Write(0x0A, 0x0C);
    (void)es8311Write(0x0D, 0x01);
    es8311DisableCapture();
    (void)es8311Write(0x12, 0x00);
    (void)es8311Write(0x13, 0x10);
    (void)es8311Write(0x1C, 0x6A);
    (void)es8311Write(0x37, 0x08);
    (void)es8311Write(0x32, dacVolumeRegister(volume0to100));
    (void)es8311Write(0x31, 0x00);
    (void)es8311Write(0x00, 0x80);
    return true;
}

void es8311MuteAndSleep() {
    (void)es8311Write(0x31, 0x60);
    es8311DisableCapture();
}

bool i2sStart(i2s_chan_handle_t* outTx) {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_handle_t tx      = nullptr;
    if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
        return false;
    }
    i2s_std_config_t stdCfg = {};
    stdCfg.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRateHz);
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    stdCfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    stdCfg.gpio_cfg.mclk = static_cast<gpio_num_t>(pins::kI2sMclk);
    stdCfg.gpio_cfg.bclk = static_cast<gpio_num_t>(pins::kI2sSclk);
    stdCfg.gpio_cfg.ws   = static_cast<gpio_num_t>(pins::kI2sLrck);
    stdCfg.gpio_cfg.dout = static_cast<gpio_num_t>(pins::kI2sDsdin);
    stdCfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
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
    int16_t                 stereo[kChunkFrames * 2];
    const float             twoPi = 6.28318530718f;
    const uint32_t          total = (kAudioSampleRateHz * durationMs) / 1000U;
    uint32_t                done  = 0;
    while (done < total) {
        const size_t n = ((total - done) < kChunkFrames) ? (total - done) : kChunkFrames;
        for (size_t i = 0; i < n; ++i) {
            const float t   = static_cast<float>(done + i) / static_cast<float>(kAudioSampleRateHz);
            const float env = 1.0f - (static_cast<float>(done + i) / static_cast<float>(total));
            const float s   = sinf(twoPi * hz * t) * amplitude * env;
            const auto  v   = static_cast<int16_t>(s * 28000.0f);
            stereo[i * 2U]     = v;
            stereo[i * 2U + 1] = v;
        }
        size_t written = 0;
        (void)i2s_channel_write(tx, stereo, n * 2U * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
        done += static_cast<uint32_t>(n);
    }
}

void writeSilence(i2s_chan_handle_t tx, uint32_t durationMs) {
    static constexpr size_t kChunkFrames = 128;
    int16_t                 stereo[kChunkFrames * 2]{};
    const uint32_t          total = (kAudioSampleRateHz * durationMs) / 1000U;
    uint32_t                done  = 0;
    while (done < total) {
        const size_t n = ((total - done) < kChunkFrames) ? (total - done) : kChunkFrames;
        size_t       written = 0;
        (void)i2s_channel_write(tx, stereo, n * 2U * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
        done += static_cast<uint32_t>(n);
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
    if (!s_codecPresent || !playbackAllowedNow()) {
        return;
    }
    const uint8_t vol = configGetAudioVolume();
    paCodecOnAmpOff();
    delay(8);
    i2s_chan_handle_t tx = nullptr;
    if (!i2sStart(&tx)) {
        ESP_LOGW(TAG, "I2S start failed");
        paPinsLow();
        return;
    }
    if (!es8311PreparePlayback(vol)) {
        ESP_LOGW(TAG, "ES8311 playback setup failed");
        i2sStop(tx);
        paPinsLow();
        return;
    }
    delay(4);
    paCodecAndAmpOn();
    delay(6);
    const float amp = static_cast<float>(vol) / 100.0f;
    if (kind == AudioMsg::Kind::Tx) {
        writeTone(tx, 80, 95.0f, 0.95f * amp);
        writeSilence(tx, 70);
        writeTone(tx, 100, 80.0f, 0.75f * amp);
    } else {
        writeTone(tx, 140, 88.0f, 0.55f * amp);
    }
    es8311MuteAndSleep();
    delay(4);
    paPinsLow();
    i2sStop(tx);
}

void audioTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    for (;;) {
        AudioMsg msg{};
        if (xQueueReceive(g_audioCmdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            playKind(msg.kind);
        }
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLog, 120);
    }
}

}  // namespace

void audioInit() {
    pinMode(pins::kPaEn, OUTPUT);
    pinMode(pins::kPaCtrl, OUTPUT);
    paPinsLow();
    Wire.begin(pins::kI2cSda, pins::kI2cScl);
    Wire.setClock(100000);
    s_codecPresent = es8311ProbeAndMuteMic();
    if (!s_codecPresent) {
        ESP_LOGW(TAG, "ES8311 not found — audio disabled");
        paPinsLow();
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
        ESP_LOGD(TAG, "audio queue full — drop");
    }
}

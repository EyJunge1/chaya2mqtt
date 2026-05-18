#include "epd_driver.h"

#include "log_tag.h"

#include <cstring>

#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

#pragma GCC diagnostic ignored "-Wunused-parameter"

#if defined(ESP32)
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

DEFINE_LOG_TAG("EPD");

EpdDriver154Z90c::EpdDriver154Z90c(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    : Adafruit_GFX(static_cast<int16_t>(kWidth), static_cast<int16_t>(kHeight)), _cs(cs), _dc(dc),
      _rst(rst), _busy(busy) {
    _page_height = kPageHeight;
    _pages   = static_cast<uint16_t>((HEIGHT / _page_height) + ((HEIGHT % _page_height) > 0));
    _current_page = 0;
}

void EpdDriver154Z90c::init(uint32_t serial_diag_bitrate) {
    init(serial_diag_bitrate, true, 10, false);
}

void EpdDriver154Z90c::init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration,
                            bool pulldown_rst_mode) {
    _initial_write            = initial;
    _pulldown_rst_mode        = pulldown_rst_mode;
    _power_is_on              = false;
    _hibernating              = false;
    _panel_controller_ready   = false;
    _reset_duration           = reset_duration;
    if (serial_diag_bitrate > 0) {
        Serial.begin(serial_diag_bitrate);
        _diag_enabled = true;
    }
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
    }
    _reset();
    _p_spi->begin();
    if (_rst >= 0) {
        digitalWrite(_rst, HIGH);
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, HIGH);
    }
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
    }
    if (_dc >= 0) {
        digitalWrite(_dc, HIGH);
        pinMode(_dc, OUTPUT);
        digitalWrite(_dc, HIGH);
    }
    if (_busy >= 0) {
        pinMode(_busy, INPUT);
    }
    _current_page = 0;
}

void EpdDriver154Z90c::end() {
    _p_spi->end();
    if (_cs >= 0) {
        pinMode(_cs, INPUT);
    }
    if (_dc >= 0) {
        pinMode(_dc, INPUT);
    }
    if (_rst >= 0) {
        pinMode(_rst, INPUT);
    }
}

void EpdDriver154Z90c::firstPage() {
    fillScreen(EPD_WHITE);
    _current_page = 0;
}

bool EpdDriver154Z90c::nextPage() {
    const uint16_t page_ys = static_cast<uint16_t>(_current_page) * _page_height;
    const uint16_t span =
        minU16(_page_height, static_cast<uint16_t>(static_cast<uint16_t>(HEIGHT) - page_ys));
    writeImage(_black_buffer, _color_buffer, 0, static_cast<int16_t>(page_ys),
               static_cast<int16_t>(kWidth), static_cast<int16_t>(span), false, false, false);
    _current_page++;
    if (_current_page == static_cast<int16_t>(_pages)) {
        _current_page = 0;
        refreshFull();
        powerOff();
        return false;
    }
    fillScreen(EPD_WHITE);
    return true;
}

void EpdDriver154Z90c::fillScreen(uint16_t color) {
    uint8_t black = 0xFF;
    uint8_t red   = 0xFF;
    if (color == EPD_WHITE) {
        // keep both white
    } else if (color == EPD_BLACK) {
        black = 0x00;
    } else if (color == EPD_RED) {
        red = 0x00;
    }
    memset(_black_buffer, black, sizeof(_black_buffer));
    memset(_color_buffer, red, sizeof(_color_buffer));
}

void EpdDriver154Z90c::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) {
        return;
    }
    switch (getRotation()) {
    case 1:
        swapVar(x, y);
        x = WIDTH - x - 1;
        break;
    case 2:
        x = WIDTH - x - 1;
        y = HEIGHT - y - 1;
        break;
    case 3:
        swapVar(x, y);
        y = HEIGHT - y - 1;
        break;
    default:
        break;
    }
    y -= static_cast<int16_t>(_current_page * _page_height);
    if ((y < 0) || (y >= static_cast<int16_t>(_page_height))) {
        return;
    }
    const uint32_t i   = static_cast<uint32_t>(x) / 8U +
                         static_cast<uint32_t>(y) * static_cast<uint32_t>(kWidth / 8U);
    const uint32_t bit = 7U - (static_cast<uint32_t>(x) % 8U);
    _black_buffer[i]   = static_cast<uint8_t>(_black_buffer[i] | (1U << bit));
    _color_buffer[i]   = static_cast<uint8_t>(_color_buffer[i] | (1U << bit));
    if (color == EPD_WHITE) {
        return;
    }
    if (color == EPD_BLACK) {
        _black_buffer[i] = static_cast<uint8_t>(_black_buffer[i] & static_cast<uint8_t>(0xFF ^ (1U << bit)));
    } else if (color == EPD_RED) {
        _color_buffer[i] = static_cast<uint8_t>(_color_buffer[i] & static_cast<uint8_t>(0xFF ^ (1U << bit)));
    }
}

void EpdDriver154Z90c::hibernate() {
    powerOff();
    if (_rst >= 0) {
        _writeCommand(0x10);
        _writeData(0x01);
        _hibernating = true;
    }
    _panel_controller_ready = false;
}

void EpdDriver154Z90c::powerOff() {
    _powerOffEp();
    _panel_controller_ready = false;
}

/* --- SSD1682 / GDEH0154Z90 --- */

void EpdDriver154Z90c::writeScreenBuffer(uint8_t black_value, uint8_t color_value) {
    _initial_write = false;
    _initPart();
    _setPartialRamArea(0, 0, kWidth, kHeight);
    _writeCommand(0x24);
    const uint32_t planeBytes = uint32_t(kWidth) * uint32_t(kHeight) / 8U;
    _writeConstantDataBytes(black_value, planeBytes);
    _writeCommand(0x26);
    _writeConstantDataBytes(static_cast<uint8_t>(~color_value), planeBytes);
}

void EpdDriver154Z90c::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y,
                                  int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm) {
    if (_initial_write) {
        writeScreenBuffer(0xFF, 0xFF);
    }
    delay(1);
    const int16_t wb = (w + 7) / 8;
    x -= x % 8;
    w = wb * 8;
    int16_t x1 = x < 0 ? 0 : x;
    int16_t y1 = y < 0 ? 0 : y;
    int16_t w1 = x + w < static_cast<int16_t>(kWidth) ? w : static_cast<int16_t>(kWidth) - x;
    int16_t h1 = y + h < static_cast<int16_t>(kHeight) ? h : static_cast<int16_t>(kHeight) - y;
    const int16_t dx = x1 - x;
    const int16_t dy = y1 - y;
    w1 -= dx;
    h1 -= dy;
    if ((w1 <= 0) || (h1 <= 0)) {
        return;
    }
    _initPart();
    _setPartialRamArea(static_cast<uint16_t>(x1), static_cast<uint16_t>(y1),
                       static_cast<uint16_t>(w1), static_cast<uint16_t>(h1));
    _writeCommand(0x24);
    {
        uint8_t       row[32];
        const int16_t rowBytes = w1 / 8;
        for (int16_t i = 0; i < h1; i++) {
            for (int16_t j = 0; j < rowBytes; j++) {
                uint8_t data = 0xFF;
                if (black != nullptr) {
                    const int16_t idx =
                        mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
                    if (pgm) {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
                        data = pgm_read_byte(&black[idx]);
#else
                        data = black[idx];
#endif
                    } else {
                        data = black[idx];
                    }
                    if (invert) {
                        data = static_cast<uint8_t>(~data);
                    }
                }
                row[static_cast<size_t>(j)] = data;
            }
            _writeDataBulk(row, static_cast<size_t>(rowBytes));
        }
    }
    _writeCommand(0x26);
    {
        uint8_t       row[32];
        const int16_t rowBytes = w1 / 8;
        for (int16_t i = 0; i < h1; i++) {
            for (int16_t j = 0; j < rowBytes; j++) {
                uint8_t data = 0xFF;
                if (color != nullptr) {
                    const int16_t idx =
                        mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
                    if (pgm) {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
                        data = pgm_read_byte(&color[idx]);
#else
                        data = color[idx];
#endif
                    } else {
                        data = color[idx];
                    }
                    if (invert) {
                        data = static_cast<uint8_t>(~data);
                    }
                }
                row[static_cast<size_t>(j)] = static_cast<uint8_t>(~data);
            }
            _writeDataBulk(row, static_cast<size_t>(rowBytes));
        }
    }
    delay(1);
}

void EpdDriver154Z90c::refreshFull() {
    _updateFull();
}

void EpdDriver154Z90c::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t xe = (x + w - 1);
    const uint16_t ye = y + h - 1;
    _writeCommand(0x44);
    _writeData(static_cast<uint8_t>(x / 8));
    _writeData(static_cast<uint8_t>(xe / 8));
    _writeCommand(0x45);
    _writeData(static_cast<uint8_t>(y % 256));
    _writeData(static_cast<uint8_t>(y / 256));
    _writeData(static_cast<uint8_t>(ye % 256));
    _writeData(static_cast<uint8_t>(ye / 256));
    _writeCommand(0x4E);
    _writeData(static_cast<uint8_t>(x / 8));
    _writeCommand(0x4F);
    _writeData(static_cast<uint8_t>(y % 256));
    _writeData(static_cast<uint8_t>(y / 256));
}

void EpdDriver154Z90c::_powerOffEp() {
    if (_power_is_on) {
        _writeCommand(0x22);
        _writeData(0xc3);
        _writeCommand(0x20);
        _waitWhileBusy("_PowerOff", kPowerOffMs);
        _power_is_on = false;
    }
}

void EpdDriver154Z90c::_initDisplay() {
    if (_hibernating) {
        _reset();
    }
    _writeCommand(0x12);
    _waitWhileBusy(nullptr, kPowerOnMs);
    _writeCommand(0x01);
    _writeData(0xC7);
    _writeData(0x00);
    _writeData(0x00);
    _writeCommand(0x11);
    _writeData(0x03);
    _writeCommand(0x3C);
    _writeData(0x05);
    _writeCommand(0x18);
    _writeData(0x80);
    _setPartialRamArea(0, 0, kWidth, kHeight);
    _power_is_on = true;
}

void EpdDriver154Z90c::_initPart() {
    if (!_panel_controller_ready) {
        _initDisplay();
        _panel_controller_ready = true;
    }
}

void EpdDriver154Z90c::_updateFull() {
    _writeCommand(0x22);
    _writeData(0xF7);
    _writeCommand(0x20);
    _waitWhileBusy("_Update_Full", kFullRefreshMs);
}

void EpdDriver154Z90c::_reset() {
    if (_rst < 0) {
        return;
    }
    if (_pulldown_rst_mode) {
        digitalWrite(_rst, LOW);
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(_reset_duration);
        pinMode(_rst, INPUT_PULLUP);
        delay(_reset_duration > 10 ? _reset_duration : 10);
    } else {
        digitalWrite(_rst, HIGH);
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, HIGH);
        delay(10);
        digitalWrite(_rst, LOW);
        delay(_reset_duration);
        digitalWrite(_rst, HIGH);
        delay(_reset_duration > 10 ? _reset_duration : 10);
    }
    _hibernating = false;
}

void EpdDriver154Z90c::_waitWhileBusy(const char* comment, uint16_t busy_time) {
    if (_busy >= 0) {
#if defined(ESP32)
        vTaskDelay(pdMS_TO_TICKS(1));
#else
        delay(1);
#endif
        const unsigned long start = micros();
        while (true) {
            if (digitalRead(_busy) != kBusyLevel) {
                break;
            }
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(1));
#else
            delay(1);
#endif
            if (digitalRead(_busy) != kBusyLevel) {
                break;
            }
            if (micros() - start > _busy_timeout) {
#if defined(ESP32)
                ESP_LOGW(TAG, "Busy Timeout!");
#else
                Serial.println(F("Busy Timeout!"));
#endif
                break;
            }
#if defined(ESP8266) || defined(ESP32)
            yield();
#endif
        }
#if !defined(DISABLE_DIAGNOSTIC_OUTPUT)
        if (comment != nullptr && _diag_enabled) {
#if defined(ESP32)
            ESP_LOGD(TAG, "%s : %lu us", comment, static_cast<unsigned long>(micros() - start));
#else
            const unsigned long elapsed = micros() - start;
            Serial.print(comment);
            Serial.print(F(" : "));
            Serial.println(elapsed);
#endif
        }
#else
        (void)comment;
#endif
    } else {
        delay(busy_time);
    }
}

void EpdDriver154Z90c::_writeCommand(uint8_t c) {
    _p_spi->beginTransaction(_spi_settings);
    if (_dc >= 0) {
        digitalWrite(_dc, LOW);
    }
    if (_cs >= 0) {
        digitalWrite(_cs, LOW);
    }
    _p_spi->transfer(c);
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
    }
    if (_dc >= 0) {
        digitalWrite(_dc, HIGH);
    }
    _p_spi->endTransaction();
}

void EpdDriver154Z90c::_writeData(uint8_t d) {
    _p_spi->beginTransaction(_spi_settings);
    if (_cs >= 0) {
        digitalWrite(_cs, LOW);
    }
    _p_spi->transfer(d);
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
    }
    _p_spi->endTransaction();
}

void EpdDriver154Z90c::_writeDataBulk(const uint8_t* data, size_t len) {
    if (len == 0U || data == nullptr) {
        return;
    }
    _p_spi->beginTransaction(_spi_settings);
    if (_cs >= 0) {
        digitalWrite(_cs, LOW);
    }
    _p_spi->transferBytes(data, nullptr, len);
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
    }
    _p_spi->endTransaction();
}

void EpdDriver154Z90c::_writeConstantDataBytes(uint8_t value, size_t count) {
    if (count == 0U) {
        return;
    }
    constexpr size_t kChunk = 256U;
    uint8_t          block[kChunk];
    memset(block, value, sizeof(block));
    while (count > 0U) {
        const size_t n = (count > kChunk) ? kChunk : count;
        _writeDataBulk(block, n);
        count -= n;
    }
}

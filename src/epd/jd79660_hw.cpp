#include "jd79660_hw.hpp"

#include <Arduino.h>
#include <cstring>

Jd79660Hw::Jd79660Hw(int16_t cs, int16_t dc, int16_t rst, int16_t busy) : cs_(cs), dc_(dc), rst_(rst), busy_(busy) {}

void Jd79660Hw::beginPins() {
    if (cs_ >= 0) {
        pinMode(cs_, OUTPUT);
        digitalWrite(cs_, HIGH);
    }
    if (dc_ >= 0) {
        pinMode(dc_, OUTPUT);
        digitalWrite(dc_, HIGH);
    }
    if (busy_ >= 0) {
        pinMode(busy_, INPUT);
    }
    if (rst_ >= 0) {
        pinMode(rst_, OUTPUT);
        digitalWrite(rst_, HIGH);
    }
}

void Jd79660Hw::setBusyCallback(BusyCallback cb, const void *param) {
    busyCb_ = cb;
    busyCbParam_ = param;
}

void Jd79660Hw::reset(uint16_t resetDurationMs) {
    if (rst_ < 0) {
        return;
    }
    // Waveshare "clever" reset: brief LOW pulse after rail is up.
    digitalWrite(rst_, HIGH);
    delay(10);
    digitalWrite(rst_, LOW);
    delay(resetDurationMs);
    digitalWrite(rst_, HIGH);
    delay(resetDurationMs > 10 ? resetDurationMs : 10);
}

void Jd79660Hw::waitWhileBusy(uint32_t timeoutUs) {
    if (busy_ < 0) {
        return;
    }
    delay(1);
    const unsigned long start = micros();
    // Busy while pin is LOW.
    while (digitalRead(busy_) == LOW) {
        if (busyCb_ != nullptr) {
            busyCb_(busyCbParam_);
        } else {
            delay(1);
        }
        if ((micros() - start) > timeoutUs) {
            break;
        }
        yield();
    }
}

void Jd79660Hw::writeCommand(uint8_t cmd) {
    spi_->beginTransaction(spiSettings_);
    if (dc_ >= 0) {
        digitalWrite(dc_, LOW);
    }
    if (cs_ >= 0) {
        digitalWrite(cs_, LOW);
    }
    spi_->transfer(cmd);
    if (cs_ >= 0) {
        digitalWrite(cs_, HIGH);
    }
    if (dc_ >= 0) {
        digitalWrite(dc_, HIGH);
    }
    spi_->endTransaction();
}

void Jd79660Hw::writeData(uint8_t data) {
    spi_->beginTransaction(spiSettings_);
    if (cs_ >= 0) {
        digitalWrite(cs_, LOW);
    }
    spi_->write(data);
    if (cs_ >= 0) {
        digitalWrite(cs_, HIGH);
    }
    spi_->endTransaction();
}

void Jd79660Hw::writeData(const uint8_t *data, uint32_t n) {
    if (data == nullptr || n == 0) {
        return;
    }
    spi_->beginTransaction(spiSettings_);
    if (cs_ >= 0) {
        digitalWrite(cs_, LOW);
    }
    spi_->writeBytes(data, n);
    if (cs_ >= 0) {
        digitalWrite(cs_, HIGH);
    }
    spi_->endTransaction();
}

void Jd79660Hw::writeDataRepeat(uint8_t value, uint32_t n) {
    if (n == 0) {
        return;
    }
    uint8_t chunk[64];
    memset(chunk, value, sizeof(chunk));
    spi_->beginTransaction(spiSettings_);
    if (cs_ >= 0) {
        digitalWrite(cs_, LOW);
    }
    while (n > 0) {
        const uint32_t m = n < sizeof(chunk) ? n : sizeof(chunk);
        spi_->writeBytes(chunk, m);
        n -= m;
    }
    if (cs_ >= 0) {
        digitalWrite(cs_, HIGH);
    }
    spi_->endTransaction();
}

void Jd79660Hw::startTransfer() {
    spi_->beginTransaction(spiSettings_);
    if (cs_ >= 0) {
        digitalWrite(cs_, LOW);
    }
}

void Jd79660Hw::transfer(uint8_t data) { spi_->write(data); }

void Jd79660Hw::transfer(const uint8_t *data, uint32_t n) {
    if (data == nullptr || n == 0) {
        return;
    }
    spi_->writeBytes(data, n);
}

void Jd79660Hw::endTransfer() {
    if (cs_ >= 0) {
        digitalWrite(cs_, HIGH);
    }
    spi_->endTransaction();
}

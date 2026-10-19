#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <cstdint>

/**
 * Low-level SPI / GPIO for JD79660-based panels (CS, DC, RST, BUSY).
 * Busy polarity: pin LOW while controller is busy.
 */
class Jd79660Hw {
  public:
    using BusyCallback = void (*)(const void *);

    Jd79660Hw(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    /** Configure CS/DC/RST/BUSY pins only (no reset pulse). */
    void beginPins();
    void setBusyCallback(BusyCallback cb, const void *param = nullptr);

    void reset(uint16_t resetDurationMs);
    void waitWhileBusy(uint32_t timeoutUs = 50'000'000UL);

    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    /** Bulk data write (single transaction). */
    void writeData(const uint8_t *data, uint32_t n);
    /** Fill n bytes with the same value (chunked bulk SPI). */
    void writeDataRepeat(uint8_t value, uint32_t n);

    void startTransfer();
    void transfer(uint8_t data);
    void transfer(const uint8_t *data, uint32_t n);
    void endTransfer();

  private:
    int16_t cs_;
    int16_t dc_;
    int16_t rst_;
    int16_t busy_;
    SPIClass *spi_ = &SPI;
    SPISettings spiSettings_{4'000'000, MSBFIRST, SPI_MODE0};
    BusyCallback busyCb_ = nullptr;
    const void *busyCbParam_ = nullptr;
};

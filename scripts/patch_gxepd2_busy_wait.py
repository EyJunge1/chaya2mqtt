#!/usr/bin/env python3
"""Align GxEPD2 GDEM0154F51H timing with Waveshare 08_E_paper_test.

Official Arduino demo (EPD_1in54g.cpp):
  Reset:  RST HIGH 200 ms, LOW 2 ms, HIGH 200 ms
  Busy:   delay(100) then wait until BUSY is HIGH (idle)

GxEPD2 uses busy_level=LOW for this panel, so "wait while LOW" == wait for HIGH.
"""

from __future__ import annotations

from pathlib import Path
from typing import Protocol


class PioEnvironment(Protocol):
    """Minimal PlatformIO/SCons environment interface used by this hook."""

    def __getitem__(self, key: str) -> object: ...

BUSY_MARKER = "CHAYA_BUSY_WAIT_WAVESHARE"
RESET_MARKER = "CHAYA_EPD_RESET_WAVESHARE"

BUSY_STOCK = """void GxEPD2_EPD::_waitWhileBusy(const char* comment, uint16_t busy_time)
{
  if (_busy >= 0)
  {
    delay(1); // add some margin to become active
    unsigned long start = micros();
    while (1)
    {
      if (digitalRead(_busy) != _busy_level) break;
      if (_busy_callback) _busy_callback(_busy_callback_parameter);
      else delay(1);
      if (digitalRead(_busy) != _busy_level) break;
      if (micros() - start > _busy_timeout)
      {
        Serial.println("Busy Timeout!");
        break;
      }
#if defined(ESP8266) || defined(ESP32)
      yield(); // avoid wdt
#endif
    }"""

BUSY_OLD_PATCH = """void GxEPD2_EPD::_waitWhileBusy(const char* comment, uint16_t busy_time)
{
  if (_busy >= 0)
  {
    // CHAYA_BUSY_WAIT_ASSERT_PATCH: wait for busy to assert, then for release.
    delay(1); // add some margin to become active
    unsigned long start = micros();
    while (digitalRead(_busy) != _busy_level)
    {
      delay(1);
      if (micros() - start > 200000UL) // 200 ms to assert
      {
        break;
      }
#if defined(ESP8266) || defined(ESP32)
      yield();
#endif
    }
    start = micros();
    while (1)
    {
      if (digitalRead(_busy) != _busy_level) break;
      if (_busy_callback) _busy_callback(_busy_callback_parameter);
      else delay(1);
      if (digitalRead(_busy) != _busy_level) break;
      if (micros() - start > _busy_timeout)
      {
        Serial.println("Busy Timeout!");
        break;
      }
#if defined(ESP8266) || defined(ESP32)
      yield(); // avoid wdt
#endif
    }"""

BUSY_NEW = """void GxEPD2_EPD::_waitWhileBusy(const char* comment, uint16_t busy_time)
{
  if (_busy >= 0)
  {
    // CHAYA_BUSY_WAIT_WAVESHARE: EPD_1IN54G_ReadBusyH — 100 ms then wait until idle (HIGH).
    delay(100);
    unsigned long start = micros();
    while (1)
    {
      if (digitalRead(_busy) != _busy_level) break;
      if (_busy_callback) _busy_callback(_busy_callback_parameter);
      else delay(1);
      if (digitalRead(_busy) != _busy_level) break;
      if (micros() - start > _busy_timeout)
      {
        Serial.println("Busy Timeout!");
        break;
      }
#if defined(ESP8266) || defined(ESP32)
      yield(); // avoid wdt
#endif
    }"""

RESET_STOCK = """    digitalWrite(_rst, HIGH);
    delay(20); // At least 20ms delay
    digitalWrite(_rst, LOW); // Module reset
    delay(2);  // At least 40ms delay, 2ms for WS "clever" reset
    digitalWrite(_rst, HIGH);
    delay(2); // At least 50ms delay (32ms measured)"""

RESET_NEW = """    digitalWrite(_rst, HIGH);
    delay(200); // CHAYA_EPD_RESET_WAVESHARE: EPD_1IN54G_Reset
    digitalWrite(_rst, LOW); // Module reset
    delay(2);  // Waveshare "clever" reset — do not hold RST long
    digitalWrite(_rst, HIGH);
    delay(200); // EPD_1IN54G_Reset"""


def patch_busy(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    if BUSY_NEW in text:
        print(f"GxEPD2 busy-wait already patched: {path}")
        return False
    if BUSY_MARKER in text:
        message = f"existing GxEPD2 busy-wait patch is outdated: {path}"
        raise RuntimeError(message)
    if BUSY_OLD_PATCH in text:
        path.write_text(text.replace(BUSY_OLD_PATCH, BUSY_NEW, 1), encoding="utf-8")
        print(f"Updated GxEPD2 busy-wait (Waveshare): {path}")
        return True
    if BUSY_STOCK in text:
        path.write_text(text.replace(BUSY_STOCK, BUSY_NEW, 1), encoding="utf-8")
        print(f"Patched GxEPD2 busy-wait (Waveshare): {path}")
        return True
    message = f"GxEPD2 busy-wait pattern not found: {path}"
    raise RuntimeError(message)


def patch_reset(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    if RESET_NEW in text:
        print(f"GxEPD2 reset already patched: {path}")
        return False
    if RESET_MARKER in text:
        message = f"existing GxEPD2 reset patch is outdated: {path}"
        raise RuntimeError(message)
    if RESET_STOCK not in text:
        message = f"GxEPD2 reset pattern not found: {path}"
        raise RuntimeError(message)
    path.write_text(text.replace(RESET_STOCK, RESET_NEW, 1), encoding="utf-8")
    print(f"Patched GxEPD2 reset (Waveshare): {path}")
    return True


def main(environment_name: str | None = None, project_root: Path | None = None) -> int:
    root = project_root or Path(__file__).resolve().parents[1]
    environment = environment_name or "*"
    busy_paths = list(root.glob(f".pio/libdeps/{environment}/GxEPD2/src/GxEPD2_EPD.cpp"))
    reset_paths = list(
        root.glob(
            f".pio/libdeps/{environment}/GxEPD2/src/epd4c/GxEPD2_154c_GDEM0154F51H.cpp"
        )
    )
    if not busy_paths or not reset_paths:
        raise RuntimeError("GxEPD2 sources are not installed; cannot apply required patches")
    for path in busy_paths:
        patch_busy(path)
    for path in reset_paths:
        patch_reset(path)
    return 0


if "Import" in globals():
    globals()["Import"]("env")
    _pio_env = globals()["env"]
    _configure_project_lib_builder = _pio_env.ConfigureProjectLibBuilder

    def _configure_and_patch(environment: PioEnvironment):
        """Install dependencies, patch GxEPD2, then let PlatformIO compile them."""
        project = _configure_project_lib_builder()
        main(str(environment["PIOENV"]), Path(str(environment["PROJECT_DIR"])))
        return project

    _pio_env.AddMethod(_configure_and_patch, "ConfigureProjectLibBuilder")
elif __name__ == "__main__":
    raise SystemExit(main())

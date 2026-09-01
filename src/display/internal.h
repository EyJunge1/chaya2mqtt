#pragma once

#include <GxEPD2_4C.h>
#include <epd4c/GxEPD2_154c_GDEM0154F51H.h>

using ChayaEpdPanel = GxEPD2_4C<GxEPD2_154c_GDEM0154F51H, GxEPD2_154c_GDEM0154F51H::HEIGHT>;

ChayaEpdPanel &displayPanel();

void displayHwInitPins();
void displayInitGxEpd();
void displayResumeSpiForDraw();
void displaySuspendSpiLowPower();

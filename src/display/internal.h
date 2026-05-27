#pragma once

#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_154_Z90c.h>

using ChayaEpdPanel = GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT>;

ChayaEpdPanel& displayPanel();

void displayResumeSpiForDraw();
void displaySuspendSpiLowPower();

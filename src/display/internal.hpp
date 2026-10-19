#pragma once

#include "epd/chaya_epd4c.hpp"

using ChayaEpdPanel = ChayaEpd4c;

ChayaEpdPanel &displayPanel();

void displayHwInitPins();
void displayInitEpd();
void displayResumeSpiForDraw();
void displaySuspendSpiLowPower();

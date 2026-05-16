#pragma once

#include <epd_driver.h>

using ChayaEpdPanel = EpdDriver154Z90c;

ChayaEpdPanel& displayPanel();

void displayResumeSpiForDraw();
void displaySuspendSpiLowPower();

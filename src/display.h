#pragma once

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>

extern GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display;
extern int counter;

void displayInit();
void drawHeartWithNumber();

#pragma once

void mqttSetup();
void mqttDisconnect();
void mqttLoop();

void mqttPostponeConnect(unsigned long delayMs);

bool mqttIsConnected();

bool mqttPublishChaya();

bool mqttPublishChayaAndApplySentCounters();

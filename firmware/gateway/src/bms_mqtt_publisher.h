#ifndef SOLMAR_BMS_MQTT_PUBLISHER_H
#define SOLMAR_BMS_MQTT_PUBLISHER_H

#include "felicity.h"

bool bmsMqttPublisherBegin();
void bmsMqttPublisherLoop();
bool bmsMqttPublisherHandleMessage(const BmsMessage &msg);
bool bmsMqttPublisherPublishRaw(const char *relativeTopic, const char *payload, bool retain);
bool bmsMqttPublisherIsReady();

#endif // SOLMAR_BMS_MQTT_PUBLISHER_H

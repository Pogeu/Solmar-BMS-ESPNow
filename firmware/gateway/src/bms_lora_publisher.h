#ifndef SOLMAR_BMS_LORA_PUBLISHER_H
#define SOLMAR_BMS_LORA_PUBLISHER_H

#include "felicity.h"

bool bmsLoraPublisherBegin();
void bmsLoraPublisherLoop();
bool bmsLoraPublisherHandleMessage(const BmsMessage &msg);
bool bmsLoraPublisherIsReady();

#endif // SOLMAR_BMS_LORA_PUBLISHER_H

#ifndef MESSAGE_TASKS_H
#define MESSAGE_TASKS_H

#include "FreeRTOS.h"

BaseType_t message_tasks_create(void);
void message_tasks_wait_for_sd_probe(void);
void message_tasks_wait_for_wifi_ready(void);

#endif
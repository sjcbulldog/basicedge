#ifndef SD_CARD_INIT_H
#define SD_CARD_INIT_H

#include <stdbool.h>

#include "message_protocol.h"

void sd_card_init_and_report(void);
bool sd_card_filesystem_ready(void);
bool sd_card_get_status(message_sd_card_status_t *status);

#endif
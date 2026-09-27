#ifndef SD_CARD_INIT_H
#define SD_CARD_INIT_H

#include <stdbool.h>

void sd_card_init_and_report(void);
bool sd_card_filesystem_ready(void);

#endif
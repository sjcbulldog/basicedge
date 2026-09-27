#ifndef MESSAGE_LOG_H
#define MESSAGE_LOG_H

#include <stdbool.h>
#include <stddef.h>

#include "message_protocol.h"

bool message_log_write(message_log_subsystem_t subsystem,
                       const char *text,
                       size_t length);
int message_log_printf(message_log_subsystem_t subsystem,
                       const char *format,
                       ...);

#endif
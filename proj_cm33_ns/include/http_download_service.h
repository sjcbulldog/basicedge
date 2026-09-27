#ifndef HTTP_DOWNLOAD_SERVICE_H
#define HTTP_DOWNLOAD_SERVICE_H

#include <stdbool.h>

#include "message_protocol.h"

bool http_download_service_handle_message(message_handle_t handle,
                                          message_t *message);

#endif
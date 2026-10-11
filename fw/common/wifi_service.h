#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "message_protocol.h"

#define WIFI_SERVICE_MAX_SCAN_RECORDS (107U)
#define WIFI_SERVICE_STATUS_OK (0)
#define WIFI_SERVICE_STATUS_ERROR (-1)
#define WIFI_SERVICE_STATUS_CONNECTED (-2)
#define WIFI_SERVICE_STATUS_DISCONNECTED (-3)
#define WIFI_SERVICE_STATUS_NOT_READY (-4)
#define WIFI_SERVICE_STATUS_CONNECTED_NOT_SAVED (-5)

void wifi_service_initialize(void);
bool wifi_service_handle_message(message_handle_t handle, message_t *message);
bool wifi_service_http_ready(void);

#if defined(CORE_NAME_CM55_0)
bool wifi_service_scan(message_wifi_scan_record_t *records,
                       size_t capacity,
                       size_t *count);
bool wifi_service_stored(message_wifi_scan_record_t *records,
                         size_t capacity,
                         size_t *count);
bool wifi_service_clear_stored(void);
bool wifi_service_status(char *ssid, size_t capacity);
bool wifi_service_connect(const char *ssid,
                          const char *password,
                          bool *credentials_saved);
bool wifi_service_disconnect(void);
bool wifi_service_download(const char *url,
                          uint8_t *destination,
                          size_t capacity,
                          size_t *downloaded_length);
#else
void wifi_task(void *argument);
#endif

#endif
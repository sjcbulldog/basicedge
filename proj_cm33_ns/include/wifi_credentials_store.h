#ifndef WIFI_CREDENTIALS_STORE_H
#define WIFI_CREDENTIALS_STORE_H

#include <stdbool.h>
#include <stddef.h>

#define WIFI_CREDENTIALS_MAX_ENTRIES (5U)
#define WIFI_CREDENTIALS_SSID_CAPACITY (33U)
#define WIFI_CREDENTIALS_PASSWORD_CAPACITY (65U)

bool wifi_credentials_store_initialize(void);
size_t wifi_credentials_store_count(void);
bool wifi_credentials_store_has_entries(void);
bool wifi_credentials_store_get_ssid(size_t index,
                                     char *ssid,
                                     size_t ssid_capacity);
bool wifi_credentials_store_get_password(const char *ssid,
                                         char *password,
                                         size_t password_capacity);
bool wifi_credentials_store_save(const char *ssid, const char *password);
bool wifi_credentials_store_clear(void);

#endif
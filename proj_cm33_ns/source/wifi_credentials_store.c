/******************************************************************************
 * File: wifi_credentials_store.c
 * Description: CM33_NS Em_EEPROM store for saved WiFi credentials.
 ******************************************************************************/

#include "wifi_credentials_store.h"

#include <stdio.h>
#include <string.h>

#include "cy_em_eeprom.h"
#include "cycfg_memory.h"
#include "cy_utils.h"
#include "cybsp.h"
#include "cy_log.h"
#include "mtb_block_storage.h"

#define WIFI_CREDENTIALS_MAGIC (0x57494649UL)
#define WIFI_CREDENTIALS_VERSION (1U)
#define WIFI_CREDENTIALS_SIMPLE_MODE (0U)
#define WIFI_CREDENTIALS_WEAR_LEVELING (2U)
#define WIFI_CREDENTIALS_REDUNDANT_COPY (1U)
#define WIFI_CREDENTIALS_BLOCKING_WRITE (1U)

typedef struct
{
    char ssid[WIFI_CREDENTIALS_SSID_CAPACITY];
    char password[WIFI_CREDENTIALS_PASSWORD_CAPACITY];
} wifi_credential_entry_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    wifi_credential_entry_t entries[WIFI_CREDENTIALS_MAX_ENTRIES];
} wifi_credential_store_t;

#define WIFI_CREDENTIALS_DATA_SIZE (sizeof(wifi_credential_store_t))
#define WIFI_CREDENTIALS_STORAGE_SIZE \
    CY_EM_EEPROM_GET_PHYSICAL_SIZE(WIFI_CREDENTIALS_DATA_SIZE, \
                                   WIFI_CREDENTIALS_SIMPLE_MODE, \
                                   WIFI_CREDENTIALS_WEAR_LEVELING, \
                                   WIFI_CREDENTIALS_REDUNDANT_COPY)

_Static_assert(WIFI_CREDENTIALS_STORAGE_SIZE <= CYMEM_CM33_0_cy_em_eeprom_SIZE,
               "WiFi credential store exceeds its reserved flash region");

static mtb_block_storage_t wifi_credentials_block_storage;
static cy_stc_eeprom_context_t wifi_credentials_eeprom_context;
static wifi_credential_store_t wifi_credentials;
static bool wifi_credentials_store_ready;

static bool wifi_credentials_valid(const wifi_credential_store_t *store)
{
    if ((store->magic != WIFI_CREDENTIALS_MAGIC) ||
        (store->version != WIFI_CREDENTIALS_VERSION) ||
        (store->count > WIFI_CREDENTIALS_MAX_ENTRIES))
    {
        return false;
    }

    for (size_t index = 0U; index < store->count; ++index)
    {
        const wifi_credential_entry_t *entry = &store->entries[index];
        if ((entry->ssid[0] == '\0') ||
            (memchr(entry->ssid, '\0', sizeof(entry->ssid)) == NULL) ||
            (memchr(entry->password, '\0', sizeof(entry->password)) == NULL))
        {
            return false;
        }
        for (size_t earlier = 0U; earlier < index; ++earlier)
        {
            if (strcmp(entry->ssid, store->entries[earlier].ssid) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

static bool wifi_credentials_write(const wifi_credential_store_t *store)
{
    return CY_EM_EEPROM_SUCCESS ==
           Cy_Em_EEPROM_Write(0U,
                              store,
                              sizeof(*store),
                              &wifi_credentials_eeprom_context);
}

bool wifi_credentials_store_initialize(void)
{
    cy_stc_eeprom_config2_t config =
    {
        .eepromSize = WIFI_CREDENTIALS_DATA_SIZE,
        .simpleMode = WIFI_CREDENTIALS_SIMPLE_MODE,
        .wearLevelingFactor = WIFI_CREDENTIALS_WEAR_LEVELING,
        .redundantCopy = WIFI_CREDENTIALS_REDUNDANT_COPY,
        .blockingWrite = WIFI_CREDENTIALS_BLOCKING_WRITE,
        .userNvmStartAddr = CYMEM_CM33_0_cy_em_eeprom_C_START
    };
    cy_en_em_eeprom_status_t eeprom_status;
    cy_rslt_t block_storage_status;

    wifi_credentials_store_ready = false;
    block_storage_status = mtb_block_storage_nvm_create(&wifi_credentials_block_storage);
    if (CY_RSLT_SUCCESS != block_storage_status)
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi credential storage backend unavailable\n");
        return false;
    }
    eeprom_status = Cy_Em_EEPROM_Init_BD(&config,
                                         &wifi_credentials_eeprom_context,
                                         &wifi_credentials_block_storage);
    if (CY_EM_EEPROM_SUCCESS != eeprom_status)
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi credential storage initialization failed\n");
        return false;
    }

    eeprom_status = Cy_Em_EEPROM_Read(0U,
                                      &wifi_credentials,
                                      sizeof(wifi_credentials),
                                      &wifi_credentials_eeprom_context);
    if (((eeprom_status == CY_EM_EEPROM_SUCCESS) ||
         (eeprom_status == CY_EM_EEPROM_REDUNDANT_COPY_USED)) &&
        wifi_credentials_valid(&wifi_credentials))
    {
        wifi_credentials_store_ready = true;
        return true;
    }

    memset(&wifi_credentials, 0, sizeof(wifi_credentials));
    wifi_credentials.magic = WIFI_CREDENTIALS_MAGIC;
    wifi_credentials.version = WIFI_CREDENTIALS_VERSION;
    if ((CY_EM_EEPROM_SUCCESS !=
         Cy_Em_EEPROM_Erase(&wifi_credentials_eeprom_context)) ||
        !wifi_credentials_write(&wifi_credentials))
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi credential storage could not be initialized\n");
        return false;
    }

    wifi_credentials_store_ready = true;
    return true;
}

bool wifi_credentials_store_has_entries(void)
{
    return wifi_credentials_store_ready && (wifi_credentials.count > 0U);
}

bool wifi_credentials_store_get_password(const char *ssid,
                                         char *password,
                                         size_t password_capacity)
{
    if (!wifi_credentials_store_ready || (ssid == NULL) || (password == NULL))
    {
        return false;
    }
    for (size_t index = 0U; index < wifi_credentials.count; ++index)
    {
        const wifi_credential_entry_t *entry = &wifi_credentials.entries[index];
        if ((strcmp(entry->ssid, ssid) == 0) &&
            (strlen(entry->password) < password_capacity))
        {
            (void)snprintf(password, password_capacity, "%s", entry->password);
            return true;
        }
    }
    return false;
}

bool wifi_credentials_store_save(const char *ssid, const char *password)
{
    wifi_credential_store_t updated;
    size_t ssid_length;
    size_t password_length;
    size_t found = WIFI_CREDENTIALS_MAX_ENTRIES;

    if (!wifi_credentials_store_ready || (ssid == NULL) || (password == NULL))
    {
        return false;
    }
    ssid_length = strlen(ssid);
    password_length = strlen(password);
    if ((ssid_length == 0U) ||
        (ssid_length >= WIFI_CREDENTIALS_SSID_CAPACITY) ||
        (password_length == 0U) ||
        (password_length >= WIFI_CREDENTIALS_PASSWORD_CAPACITY))
    {
        return false;
    }

    updated = wifi_credentials;
    for (size_t index = 0U; index < updated.count; ++index)
    {
        if (strcmp(updated.entries[index].ssid, ssid) == 0)
        {
            found = index;
            break;
        }
    }

    if (found < updated.count)
    {
        if ((found == (updated.count - 1U)) &&
            (strcmp(updated.entries[found].password, password) == 0))
        {
            return true;
        }
        for (size_t index = found; index + 1U < updated.count; ++index)
        {
            updated.entries[index] = updated.entries[index + 1U];
        }
        --updated.count;
    }
    else if (updated.count == WIFI_CREDENTIALS_MAX_ENTRIES)
    {
        for (size_t index = 0U; index + 1U < updated.count; ++index)
        {
            updated.entries[index] = updated.entries[index + 1U];
        }
        --updated.count;
    }

    wifi_credential_entry_t *new_entry = &updated.entries[updated.count];
    memset(new_entry, 0, sizeof(*new_entry));
    memcpy(new_entry->ssid, ssid, ssid_length);
    memcpy(new_entry->password, password, password_length);
    ++updated.count;

    if (!wifi_credentials_write(&updated))
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi credential could not be saved to Em_EEPROM\n");
        return false;
    }
    wifi_credentials = updated;
    return true;
}
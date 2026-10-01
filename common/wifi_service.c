#include "wifi_service.h"

#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "message_log.h"
#include "message_transport.h"
#include "task.h"

#if defined(CORE_NAME_CM33_0)
#include "cy_log.h"
#include "cy_sd_host.h"
#include "cy_secure_sockets.h"
#include "cy_wcm.h"
#include "cybsp.h"
#include "mtb_hal_sdio.h"
#include "wifi_credentials_store.h"

#define WIFI_TASK_STACK_SIZE (10240U)
#define WIFI_TASK_PRIORITY (5U)
#define WIFI_SCAN_COMPLETE_BIT (1U << 0U)
#define WIFI_SCAN_TIMEOUT_TICKS pdMS_TO_TICKS(15000U)

static EventGroupHandle_t wifi_scan_events;
static message_wifi_scan_record_t wifi_scan_records[WIFI_SERVICE_MAX_SCAN_RECORDS];
static size_t wifi_scan_count;
static cy_rslt_t wifi_scan_result;
static bool wifi_ready;
static mtb_hal_sdio_t wifi_sdio_instance;
static cy_stc_sd_host_context_t wifi_sdhc_host_context;
static cy_wcm_config_t wifi_wcm_config;
static TaskHandle_t wifi_reconnect_task_handle;
static bool wifi_socket_ready;
static bool wifi_boot_scan_active;
static bool wifi_boot_candidate_available;
static char wifi_boot_candidate_ssid[WIFI_CREDENTIALS_SSID_CAPACITY];
static char wifi_boot_candidate_password[WIFI_CREDENTIALS_PASSWORD_CAPACITY];

static cy_rslt_t wifi_connect_to_ap(const char *ssid,
                                    const char *password,
                                    bool save_credentials,
                                    bool *credentials_saved)
{
    cy_wcm_connect_params_t params = {0};
    cy_wcm_ip_address_t ip_address;
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);

    if ((ssid_length == 0U) || (ssid_length > 32U) ||
        (password_length == 0U) || (password_length > 64U))
    {
        return CY_RSLT_WCM_BAD_ARG;
    }
    memcpy(params.ap_credentials.SSID, ssid, ssid_length);
    memcpy(params.ap_credentials.password, password, password_length);
    params.ap_credentials.security = CY_WCM_SECURITY_UNKNOWN;
    params.itwt_profile = CY_WCM_ITWT_PROFILE_NONE;

    cy_rslt_t result = cy_wcm_connect_ap(&params, &ip_address);
    if (CY_RSLT_SUCCESS == result)
    {
        if (ip_address.version == CY_WCM_IP_VER_V4)
        {
            const uint8_t *ip_bytes = (const uint8_t *)&ip_address.ip.v4;
            cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                       "WiFi connected, IP address: %u.%u.%u.%u\n",
                       (unsigned int)ip_bytes[0],
                       (unsigned int)ip_bytes[1],
                       (unsigned int)ip_bytes[2],
                       (unsigned int)ip_bytes[3]);
        }
        else
        {
            cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                       "WiFi connected, IP address is not IPv4\n");
        }
    }

    if ((CY_RSLT_SUCCESS == result) && save_credentials)
    {
        bool saved = wifi_credentials_store_save(ssid, password);
        if (credentials_saved != NULL)
        {
            *credentials_saved = saved;
        }
        if (!saved)
        {
            cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                       "WiFi connected, but credentials were not saved\n");
        }
    }
    else if (credentials_saved != NULL)
    {
        *credentials_saved = false;
    }
    return result;
}

static void wifi_event_callback(cy_wcm_event_t event,
                                cy_wcm_event_data_t *event_data)
{
    (void)event_data;
    if (event == CY_WCM_EVENT_DISCONNECTED)
    {
        if (wifi_reconnect_task_handle != NULL)
        {
            xTaskNotifyGive(wifi_reconnect_task_handle);
        }
    }
}

static void wifi_reconnect_task(void *argument)
{
    (void)argument;
    wifi_reconnect_task_handle = xTaskGetCurrentTaskHandle();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (!cy_wcm_is_connected_to_ap())
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi link restored after disconnect\n");
    }
}

static bool wifi_send_response(int32_t status,
                               const message_wifi_scan_record_t *records,
                               size_t count)
{
    message_t *message;
    size_t payload_length = sizeof(message_wifi_response_t) +
                            count * sizeof(message_wifi_scan_record_t);
    message_wifi_response_t response =
    {
        .status = status,
        .record_count = (uint16_t)count,
        .reserved = 0U
    };

    if ((payload_length > MESSAGE_MAX_PAYLOAD_SIZE) ||
        !message_transport_allocate(MESSAGE_OPCODE_M33_WIFI_RESPONSE,
                                     (uint16_t)payload_length,
                                     &message))
    {
        return false;
    }

    memcpy(message->payload, &response, sizeof(response));
    if (count > 0U)
    {
        memcpy(&message->payload[sizeof(response)], records,
               count * sizeof(message_wifi_scan_record_t));
    }
    if (!message_transport_send(message))
    {
        (void)message_transport_release_local(message);
        return false;
    }
    return true;
}

static void wifi_send_ready_notification(void)
{
    message_t *message;

    if (message_transport_allocate(MESSAGE_OPCODE_M33_WIFI_READY,
                                    0U,
                                    &message))
    {
        if (!message_transport_send(message))
        {
            (void)message_transport_release_local(message);
        }
    }
}

static int wifi_log_output(CY_LOG_FACILITY_T facility,
                           CY_LOG_LEVEL_T level,
                           char *log_message)
{
    (void)facility;
    (void)level;
    size_t length = strlen(log_message);
    return message_log_write(MESSAGE_LOG_SUBSYSTEM_WIFI,
                             log_message,
                             length)
               ? (int)length
               : 0;
}

static void wifi_scan_callback(cy_wcm_scan_result_t *result,
                               void *user_data,
                               cy_wcm_scan_status_t status)
{
    (void)user_data;
    if ((status == CY_WCM_SCAN_INCOMPLETE) && (result != NULL))
    {
        char ssid[WIFI_CREDENTIALS_SSID_CAPACITY];
        size_t ssid_length = 0U;
        while ((ssid_length < sizeof(ssid) - 1U) &&
               (result->SSID[ssid_length] != '\0'))
        {
            ++ssid_length;
        }
        memcpy(ssid, result->SSID, ssid_length);
        ssid[ssid_length] = '\0';

        if (wifi_boot_scan_active)
        {
            size_t credential_count = wifi_credentials_store_count();
            for (size_t index = 0U; index < credential_count; ++index)
            {
                char stored_ssid[WIFI_CREDENTIALS_SSID_CAPACITY];
                if (wifi_credentials_store_get_ssid(index,
                                                    stored_ssid,
                                                    sizeof(stored_ssid)) &&
                    (strcmp(stored_ssid, ssid) == 0) &&
                    wifi_credentials_store_get_password(stored_ssid,
                                                        wifi_boot_candidate_password,
                                                        sizeof(wifi_boot_candidate_password)))
                {
                    memcpy(wifi_boot_candidate_ssid, ssid, ssid_length + 1U);
                    wifi_boot_candidate_available = true;
                }
            }
        }

        if (wifi_scan_count < WIFI_SERVICE_MAX_SCAN_RECORDS)
        {
            message_wifi_scan_record_t *record = &wifi_scan_records[wifi_scan_count++];
            memcpy(record->ssid, ssid, ssid_length + 1U);
            record->signal_strength = result->signal_strength;
            record->channel = result->channel;
            record->security = (uint8_t)result->security;
        }
    }
    else if (status == CY_WCM_SCAN_COMPLETE)
    {
        wifi_scan_result = CY_RSLT_SUCCESS;
        (void)xEventGroupSetBits(wifi_scan_events, WIFI_SCAN_COMPLETE_BIT);
    }
}

static void wifi_connect_saved_network(void)
{
    size_t credential_count = wifi_credentials_store_count();

    cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
               "WiFi credentials stored: %u\n",
               (unsigned int)credential_count);
    if (credential_count == 0U)
    {
        return;
    }

    wifi_scan_count = 0U;
    wifi_boot_candidate_available = false;
    wifi_boot_scan_active = true;
    wifi_scan_result = CY_RSLT_WCM_SCAN_IN_PROGRESS;
    if (CY_RSLT_SUCCESS != cy_wcm_start_scan(wifi_scan_callback, NULL, NULL))
    {
        wifi_boot_scan_active = false;
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi startup scan failed\n");
        return;
    }

    EventBits_t scan_events = xEventGroupWaitBits(wifi_scan_events,
                                                   WIFI_SCAN_COMPLETE_BIT,
                                                   pdTRUE,
                                                   pdFALSE,
                                                   WIFI_SCAN_TIMEOUT_TICKS);
    wifi_boot_scan_active = false;
    if ((scan_events & WIFI_SCAN_COMPLETE_BIT) == 0U)
    {
        (void)cy_wcm_stop_scan();
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "WiFi startup scan timed out\n");
        return;
    }
    if (wifi_boot_candidate_available)
    {
        bool credentials_saved;
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "Connecting to SSID '%s' ...\n",
                   wifi_boot_candidate_ssid);
        cy_rslt_t result = wifi_connect_to_ap(wifi_boot_candidate_ssid,
                                              wifi_boot_candidate_password,
                                              true,
                                              &credentials_saved);
        (void)credentials_saved;
        if (CY_RSLT_SUCCESS == result)
        {
            cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                       "Connected to SSID '%s'\n",
                       wifi_boot_candidate_ssid);
        }
        else
        {
            cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                       "Failed to connect to SSID '%s'\n",
                       wifi_boot_candidate_ssid);
        }
    }
}

static void wifi_sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&wifi_sdio_instance);
}

static void wifi_host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wifi_wcm_config.wifi_host_wake_pin);
}

static void wifi_sdio_initialize(void)
{
    cy_stc_sysint_t sdio_interrupt =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = 7U
    };
    cy_stc_sysint_t host_wake_interrupt =
    {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = 2U
    };
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_config;

    CY_ASSERT(CY_SYSINT_SUCCESS == Cy_SysInt_Init(&sdio_interrupt,
                                                   wifi_sdio_interrupt_handler));
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);
    result = mtb_hal_sdio_setup(&wifi_sdio_instance,
                                &CYBSP_WIFI_SDIO_sdio_hal_config,
                                NULL,
                                &wifi_sdhc_host_context);
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                    CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &wifi_sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);
    sdio_config.frequencyhal_hz = 25000000U;
    sdio_config.block_size = 64U;
    CY_ASSERT(CY_RSLT_SUCCESS == mtb_hal_sdio_configure(&wifi_sdio_instance,
                                                         &sdio_config));
    mtb_hal_gpio_setup(&wifi_wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM,
                       CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wifi_wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM,
                       CYBSP_WIFI_HOST_WAKE_PIN);
    CY_ASSERT(CY_SYSINT_SUCCESS == Cy_SysInt_Init(&host_wake_interrupt,
                                                   wifi_host_wake_interrupt_handler));
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

void wifi_service_initialize(void)
{
    wifi_scan_events = xEventGroupCreate();
}

bool wifi_service_http_ready(void)
{
    return wifi_socket_ready;
}

void wifi_task(void *argument)
{
    (void)argument;
    wifi_wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;

    CY_ASSERT(wifi_scan_events != NULL);
    (void)cy_log_init(CY_LOG_WARNING, wifi_log_output, NULL);
    if (!wifi_credentials_store_initialize())
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_WARNING,
                   "Persistent WiFi credentials are unavailable\n");
    }
    CY_ASSERT(pdPASS == xTaskCreate(wifi_reconnect_task,
                                    "WiFi Reconnect",
                                    configMINIMAL_STACK_SIZE * 2U,
                                    NULL,
                                    4U,
                                    &wifi_reconnect_task_handle));
    wifi_sdio_initialize();
    wifi_wcm_config.wifi_interface_instance = &wifi_sdio_instance;
    CY_ASSERT(CY_RSLT_SUCCESS == cy_wcm_init(&wifi_wcm_config));
    CY_ASSERT(CY_RSLT_SUCCESS == cy_wcm_register_event_callback(wifi_event_callback));
    cy_rslt_t socket_result = cy_socket_init();
    wifi_socket_ready = (CY_RSLT_SUCCESS == socket_result);
    if (!wifi_socket_ready)
    {
        cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_ERR,
                   "Secure sockets initialization failed (0x%08lx); HTTP downloads unavailable\n",
                   (unsigned long)socket_result);
    }
    wifi_connect_saved_network();
    wifi_ready = true;
    wifi_send_ready_notification();
    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static bool wifi_handle_request(const message_wifi_request_t *request,
                                const uint8_t *data)
{
    if (!wifi_ready)
    {
        return wifi_send_response(WIFI_SERVICE_STATUS_NOT_READY, NULL, 0U);
    }
    if (request->operation == MESSAGE_WIFI_SCAN)
    {
        if (cy_wcm_is_connected_to_ap())
        {
            return wifi_send_response(WIFI_SERVICE_STATUS_CONNECTED, NULL, 0U);
        }
        wifi_scan_count = 0U;
        wifi_scan_result = CY_RSLT_WCM_SCAN_IN_PROGRESS;
        if (CY_RSLT_SUCCESS != cy_wcm_start_scan(wifi_scan_callback, NULL, NULL))
        {
            return wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
        }
        EventBits_t scan_events = xEventGroupWaitBits(wifi_scan_events,
                                                       WIFI_SCAN_COMPLETE_BIT,
                                                       pdTRUE,
                                                       pdFALSE,
                                                       WIFI_SCAN_TIMEOUT_TICKS);
        if ((scan_events & WIFI_SCAN_COMPLETE_BIT) == 0U)
        {
            wifi_scan_result = CY_RSLT_WCM_SCAN_IN_PROGRESS;
            (void)cy_wcm_stop_scan();
            return wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
        }
        return wifi_send_response((wifi_scan_result == CY_RSLT_SUCCESS)
                                      ? WIFI_SERVICE_STATUS_OK
                                      : WIFI_SERVICE_STATUS_ERROR,
                                  wifi_scan_records, wifi_scan_count);
    }
    if (request->operation == MESSAGE_WIFI_STORED)
    {
        message_wifi_scan_record_t stored_records[WIFI_CREDENTIALS_MAX_ENTRIES] = {0};
        size_t stored_count = wifi_credentials_store_count();
        if (stored_count > WIFI_CREDENTIALS_MAX_ENTRIES)
        {
            stored_count = WIFI_CREDENTIALS_MAX_ENTRIES;
        }
        for (size_t index = 0U; index < stored_count; ++index)
        {
            if (!wifi_credentials_store_get_ssid(index,
                                                 stored_records[index].ssid,
                                                 sizeof(stored_records[index].ssid)))
            {
                return wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
            }
        }
        return wifi_send_response(WIFI_SERVICE_STATUS_OK,
                                  stored_records,
                                  stored_count);
    }
    if (request->operation == MESSAGE_WIFI_CLEAR)
    {
        return wifi_send_response(wifi_credentials_store_clear()
                                      ? WIFI_SERVICE_STATUS_OK
                                      : WIFI_SERVICE_STATUS_ERROR,
                                  NULL,
                                  0U);
    }
    if (request->operation == MESSAGE_WIFI_STATUS)
    {
        cy_wcm_associated_ap_info_t ap_info = {0};
        message_wifi_scan_record_t record = {0};

        if (!cy_wcm_is_connected_to_ap())
        {
            return wifi_send_response(WIFI_SERVICE_STATUS_DISCONNECTED,
                                      NULL,
                                      0U);
        }
        if (CY_RSLT_SUCCESS != cy_wcm_get_associated_ap_info(&ap_info))
        {
            return wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
        }
        memcpy(record.ssid, ap_info.SSID, sizeof(record.ssid) - 1U);
        return wifi_send_response(WIFI_SERVICE_STATUS_OK, &record, 1U);
    }
    if (request->operation == MESSAGE_WIFI_DISCONNECT)
    {
        return wifi_send_response((CY_RSLT_SUCCESS == cy_wcm_disconnect_ap())
                                      ? WIFI_SERVICE_STATUS_OK
                                      : WIFI_SERVICE_STATUS_ERROR,
                                  NULL, 0U);
    }
    if ((request->operation == MESSAGE_WIFI_CONNECT) &&
        (request->ssid_length <= 32U) &&
        (request->password_length <= 64U) &&
        (request->ssid_length > 0U) && (request->password_length > 0U))
    {
         char ssid[WIFI_CREDENTIALS_SSID_CAPACITY] = {0};
         char password[WIFI_CREDENTIALS_PASSWORD_CAPACITY] = {0};
         bool credentials_saved = false;
         memcpy(ssid, data, request->ssid_length);
         memcpy(password, &data[request->ssid_length], request->password_length);
         cy_rslt_t result = wifi_connect_to_ap(ssid,
                               password,
                               true,
                               &credentials_saved);
         return wifi_send_response((CY_RSLT_SUCCESS != result)
                           ? WIFI_SERVICE_STATUS_ERROR
                           : (credentials_saved
                               ? WIFI_SERVICE_STATUS_OK
                               : WIFI_SERVICE_STATUS_CONNECTED_NOT_SAVED),
                                  NULL, 0U);
    }
    return wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
}

#else

#define WIFI_RESPONSE_READY_BIT (1U << 1U)
#define WIFI_HTTP_RESPONSE_READY_BIT (1U << 2U)
#define WIFI_HTTP_RESPONSE_TIMEOUT_TICKS pdMS_TO_TICKS(60000U)
static EventGroupHandle_t wifi_events;
static int32_t wifi_status;
static message_wifi_scan_record_t wifi_records[WIFI_SERVICE_MAX_SCAN_RECORDS];
static size_t wifi_record_count;
static int32_t wifi_http_status;
static uint32_t wifi_http_total_length;
static uint16_t wifi_http_data_length;
static uint8_t wifi_http_data[MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_http_response_t)];

void wifi_service_initialize(void)
{
    wifi_events = xEventGroupCreate();
}

static bool wifi_request(uint8_t operation,
                         const char *ssid,
                         const char *password,
                         message_wifi_scan_record_t *records,
                         size_t capacity,
                         size_t *count)
{
    message_t *message;
    message_wifi_request_t request =
    {
        .operation = operation,
        .ssid_length = (uint8_t)((ssid != NULL) ? strlen(ssid) : 0U),
        .password_length = (uint8_t)((password != NULL) ? strlen(password) : 0U),
        .reserved = 0U
    };
    size_t payload_length = sizeof(request) + request.ssid_length + request.password_length;

    if ((wifi_events == NULL) || (request.ssid_length > 32U) ||
        (request.password_length > 64U) ||
        !message_transport_allocate(MESSAGE_OPCODE_M55_WIFI_REQUEST,
                                     (uint16_t)payload_length, &message))
    {
        return false;
    }
    memcpy(message->payload, &request, sizeof(request));
    if (request.ssid_length > 0U)
    {
        memcpy(&message->payload[sizeof(request)], ssid, request.ssid_length);
    }
    if (request.password_length > 0U)
    {
        memcpy(&message->payload[sizeof(request) + request.ssid_length],
               password, request.password_length);
    }
    if (!message_transport_send(message))
    {
        (void)message_transport_release_local(message);
        return false;
    }
    (void)xEventGroupWaitBits(wifi_events, WIFI_RESPONSE_READY_BIT, pdTRUE,
                              pdFALSE, portMAX_DELAY);
    if ((wifi_status != WIFI_SERVICE_STATUS_OK) &&
        !((operation == MESSAGE_WIFI_CONNECT) &&
          (wifi_status == WIFI_SERVICE_STATUS_CONNECTED_NOT_SAVED)))
    {
        return false;
    }
    if (count == NULL)
    {
        return true;
    }
    *count = (wifi_record_count < capacity) ? wifi_record_count : capacity;
    memcpy(records, wifi_records, *count * sizeof(wifi_records[0]));
    return true;
}

bool wifi_service_scan(message_wifi_scan_record_t *records,
                       size_t capacity,
                       size_t *count)
{
    return wifi_request(MESSAGE_WIFI_SCAN, NULL, NULL, records, capacity, count);
}

bool wifi_service_stored(message_wifi_scan_record_t *records,
                         size_t capacity,
                         size_t *count)
{
    return wifi_request(MESSAGE_WIFI_STORED, NULL, NULL, records, capacity, count);
}

bool wifi_service_clear_stored(void)
{
    return wifi_request(MESSAGE_WIFI_CLEAR, NULL, NULL, NULL, 0U, NULL);
}

bool wifi_service_status(char *ssid, size_t capacity)
{
    message_wifi_scan_record_t record;
    size_t count = 0U;
    size_t length;

    if ((ssid == NULL) || (capacity == 0U) ||
        !wifi_request(MESSAGE_WIFI_STATUS, NULL, NULL, &record, 1U, &count) ||
        (count != 1U))
    {
        return false;
    }
    length = strnlen(record.ssid, sizeof(record.ssid));
    if (length >= capacity)
    {
        return false;
    }
    memcpy(ssid, record.ssid, length);
    ssid[length] = '\0';
    return true;
}

bool wifi_service_connect(const char *ssid,
                          const char *password,
                          bool *credentials_saved)
{
    bool connected = wifi_request(MESSAGE_WIFI_CONNECT, ssid, password, NULL, 0U, NULL);
    if (credentials_saved != NULL)
    {
        *credentials_saved = connected && (wifi_status == WIFI_SERVICE_STATUS_OK);
    }
    return connected;
}

bool wifi_service_disconnect(void)
{
    return wifi_request(MESSAGE_WIFI_DISCONNECT, NULL, NULL, NULL, 0U, NULL);
}

static bool wifi_http_exchange(uint8_t operation,
                              const char *url,
                              uint32_t offset,
                              uint16_t requested_length,
                              uint32_t *total_length,
                              uint8_t *data,
                              uint16_t data_capacity,
                              uint16_t *received_length)
{
    message_t *message;
    size_t url_length = (url != NULL) ? strlen(url) : 0U;
    size_t payload_length = sizeof(message_http_request_t) + url_length;
    message_http_request_t request =
    {
        .operation = operation,
        .reserved = 0U,
        .url_length = (uint16_t)url_length,
        .offset = offset,
        .data_length = requested_length,
        .reserved2 = 0U
    };

    if ((wifi_events == NULL) ||
        (url_length > MESSAGE_HTTP_MAX_URL_LENGTH) ||
        (payload_length > MESSAGE_MAX_PAYLOAD_SIZE) ||
        !message_transport_allocate(MESSAGE_OPCODE_M55_HTTP_REQUEST,
                                     (uint16_t)payload_length,
                                     &message))
    {
        return false;
    }

    memcpy(message->payload, &request, sizeof(request));
    if (url_length > 0U)
    {
        memcpy(&message->payload[sizeof(request)], url, url_length);
    }
    (void)xEventGroupClearBits(wifi_events, WIFI_HTTP_RESPONSE_READY_BIT);
    if (!message_transport_send(message))
    {
        (void)message_transport_release_local(message);
        return false;
    }
    EventBits_t response_bits = xEventGroupWaitBits(wifi_events,
                                                    WIFI_HTTP_RESPONSE_READY_BIT,
                                                    pdTRUE,
                                                    pdFALSE,
                                                    WIFI_HTTP_RESPONSE_TIMEOUT_TICKS);
    if ((response_bits & WIFI_HTTP_RESPONSE_READY_BIT) == 0U)
    {
        return false;
    }

    if (wifi_http_status != WIFI_SERVICE_STATUS_OK)
    {
        return false;
    }
    if (NULL != total_length)
    {
        *total_length = wifi_http_total_length;
    }
    if (wifi_http_data_length > data_capacity)
    {
        return false;
    }
    if ((wifi_http_data_length > 0U) && (data != NULL))
    {
        memcpy(data, wifi_http_data, wifi_http_data_length);
    }
    if (NULL != received_length)
    {
        *received_length = wifi_http_data_length;
    }
    return true;
}

bool wifi_service_download(const char *url,
                           uint8_t *destination,
                           size_t capacity,
                           size_t *downloaded_length)
{
    uint32_t total_length;
    uint32_t offset = 0U;
    uint16_t received_length;

    if ((url == NULL) || (destination == NULL) || (downloaded_length == NULL) ||
        (strlen(url) == 0U) || (strlen(url) > MESSAGE_HTTP_MAX_URL_LENGTH) ||
        !wifi_http_exchange(MESSAGE_HTTP_BEGIN, url, 0U, 0U,
                            &total_length, NULL, 0U, NULL) ||
        (total_length > capacity) ||
        (total_length > MESSAGE_HTTP_MAX_BODY_SIZE))
    {
        return false;
    }

    while (offset < total_length)
    {
        uint32_t remaining = total_length - offset;
        uint16_t chunk_length = (uint16_t)((remaining > sizeof(wifi_http_data))
                                               ? sizeof(wifi_http_data)
                                               : remaining);
        if (!wifi_http_exchange(MESSAGE_HTTP_READ, NULL, offset, chunk_length,
                                NULL, &destination[offset], chunk_length,
                                &received_length) ||
            (received_length == 0U))
        {
            return false;
        }
        offset += received_length;
    }
    *downloaded_length = total_length;
    return true;
}

#endif

bool wifi_service_handle_message(message_handle_t handle, message_t *message)
{
    if ((message == NULL) || (message->opcode == 0U))
    {
        return false;
    }
#if defined(CORE_NAME_CM33_0)
    if ((message->opcode == MESSAGE_OPCODE_M55_WIFI_REQUEST) &&
        (message->payload_length >= sizeof(message_wifi_request_t)))
    {
        message_wifi_request_t request;
        memcpy(&request, message->payload, sizeof(request));
        if ((sizeof(request) + request.ssid_length + request.password_length) <=
            message->payload_length)
        {
            (void)wifi_handle_request(&request, &message->payload[sizeof(request)]);
        }
        else
        {
            (void)wifi_send_response(WIFI_SERVICE_STATUS_ERROR, NULL, 0U);
        }
        (void)message_transport_destroy(handle);
        return true;
    }
#else
    if (message->opcode == MESSAGE_OPCODE_M33_WIFI_RESPONSE)
    {
        message_wifi_response_t response;
        if (message->payload_length >= sizeof(response))
        {
            memcpy(&response, message->payload, sizeof(response));
            wifi_status = response.status;
            wifi_record_count = (response.record_count <= WIFI_SERVICE_MAX_SCAN_RECORDS)
                                    ? response.record_count : 0U;
            if ((sizeof(response) + wifi_record_count * sizeof(wifi_records[0])) <=
                message->payload_length)
            {
                memcpy(wifi_records, &message->payload[sizeof(response)],
                       wifi_record_count * sizeof(wifi_records[0]));
            }
            (void)xEventGroupSetBits(wifi_events, WIFI_RESPONSE_READY_BIT);
        }
        (void)message_transport_destroy(handle);
        return true;
    }
    if (message->opcode == MESSAGE_OPCODE_M33_HTTP_RESPONSE)
    {
        message_http_response_t response;
        if (message->payload_length >= sizeof(response))
        {
            memcpy(&response, message->payload, sizeof(response));
            if ((response.data_length <= (message->payload_length - sizeof(response))) &&
                (response.data_length <= sizeof(wifi_http_data)))
            {
                wifi_http_status = response.status;
                wifi_http_total_length = response.total_length;
                wifi_http_data_length = response.data_length;
                memcpy(wifi_http_data,
                       &message->payload[sizeof(response)],
                       response.data_length);
                (void)xEventGroupSetBits(wifi_events, WIFI_HTTP_RESPONSE_READY_BIT);
            }
        }
        (void)message_transport_destroy(handle);
        return true;
    }
#endif
    return false;
}

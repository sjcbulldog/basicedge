#include "message_tasks.h"

#include <stdio.h>

#include "cybsp.h"
#include "file_service.h"
#include "message_transport.h"
#include "wifi_service.h"
#include "task.h"
#include "event_groups.h"

#if defined(CORE_NAME_CM33_0)
#include "http_download_service.h"
#endif

#define MESSAGE_RX_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4U)
#define MESSAGE_RX_TASK_PRIORITY   (configMAX_PRIORITIES - 1U)
#define MESSAGE_RX_POLL_INTERVAL_TICKS pdMS_TO_TICKS(10U)
#define MESSAGE_SD_PROBE_COMPLETE_BIT (1U << 0U)
#define MESSAGE_WIFI_READY_BIT (1U << 1U)
#define MESSAGE_LOG_DEFAULT_ENABLED_MASK \
    ((1UL << MESSAGE_LOG_SUBSYSTEM_GENERAL) | \
     (1UL << MESSAGE_LOG_SUBSYSTEM_SD_CARD) | \
     (1UL << MESSAGE_LOG_SUBSYSTEM_WIFI))

#if CY_SYSTEM_CPU_M55
static EventGroupHandle_t message_events;
static uint32_t message_log_enabled_mask = MESSAGE_LOG_DEFAULT_ENABLED_MASK;
#endif

static void message_receive_task(void *argument)
{
    CY_UNUSED_PARAMETER(argument);

    for (;;)
    {
        message_handle_t handle;
        message_t *message = NULL;

        if (!message_transport_receive(0U, &handle, &message))
        {
            vTaskDelay(MESSAGE_RX_POLL_INTERVAL_TICKS);
            continue;
        }

        if (file_service_handle_message(handle, message))
        {
            continue;
        }

#if defined(CORE_NAME_CM33_0)
        if (http_download_service_handle_message(handle, message))
        {
            continue;
        }
#endif

        if (wifi_service_handle_message(handle, message))
        {
            continue;
        }

#if CY_SYSTEM_CPU_M55
        if ((message->opcode == MESSAGE_OPCODE_M33_SD_PROBE_COMPLETE) &&
            (message->payload_length == 0U))
        {
            (void)xEventGroupSetBits(message_events, MESSAGE_SD_PROBE_COMPLETE_BIT);
        }
        else if ((message->opcode == MESSAGE_OPCODE_M33_WIFI_READY) &&
                 (message->payload_length == 0U))
        {
            (void)xEventGroupSetBits(message_events, MESSAGE_WIFI_READY_BIT);
        }
        else if ((message->opcode == MESSAGE_OPCODE_M33_LOG) &&
                 (message->payload_length >= sizeof(message_log_header_t)))
        {
            message_log_header_t header;
            memcpy(&header, message->payload, sizeof(header));
            if ((header.subsystem < MESSAGE_LOG_SUBSYSTEM_COUNT) &&
                (header.reserved == 0U) &&
                (header.text_length == (message->payload_length - sizeof(header))) &&
                ((message_log_enabled_mask & (1UL << header.subsystem)) != 0U))
            {
                (void)fwrite(&message->payload[sizeof(header)],
                             1U,
                             header.text_length,
                             stdout);
                (void)fflush(stdout);
            }
        }
#else
        if ((message->opcode == MESSAGE_OPCODE_M55_INIT_COMPLETE) &&
            (message->payload_length == 0U))
        {
            Cy_GPIO_Write(CYBSP_LED_GREEN_PORT, CYBSP_LED_GREEN_PIN, 1UL);
        }
#endif

        if (!message_transport_destroy(handle))
        {
            CY_ASSERT(0);
        }
    }
}

BaseType_t message_tasks_create(void)
{
#if CY_SYSTEM_CPU_M55
    message_events = xEventGroupCreate();
    if (message_events == NULL)
    {
        return pdFAIL;
    }
#endif

    wifi_service_initialize();

    return xTaskCreate(message_receive_task,
                       "Message RX",
                       MESSAGE_RX_TASK_STACK_SIZE,
                       NULL,
                       MESSAGE_RX_TASK_PRIORITY,
                       NULL);
}

void message_tasks_wait_for_sd_probe(void)
{
#if CY_SYSTEM_CPU_M55
    (void)xEventGroupWaitBits(message_events,
                              MESSAGE_SD_PROBE_COMPLETE_BIT,
                              pdTRUE,
                              pdFALSE,
                              portMAX_DELAY);
#endif
}

void message_tasks_wait_for_wifi_ready(void)
{
#if CY_SYSTEM_CPU_M55
    (void)xEventGroupWaitBits(message_events,
                              MESSAGE_WIFI_READY_BIT,
                              pdTRUE,
                              pdFALSE,
                              portMAX_DELAY);
#endif
}
#include "sd_card_task.h"

#include "sd_card_init.h"
#include "message_protocol.h"
#include "message_transport.h"
#include "task.h"

#define SD_CARD_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4U)
#define SD_CARD_TASK_PRIORITY   (tskIDLE_PRIORITY + 2U)

static void sd_card_task(void *argument)
{
    message_t *message;

    CY_UNUSED_PARAMETER(argument);

    sd_card_init_and_report();
    if (message_transport_allocate(MESSAGE_OPCODE_M33_SD_PROBE_COMPLETE,
                                   0U,
                                   &message))
    {
        if (!message_transport_send(message))
        {
            (void)message_transport_release_local(message);
        }
    }
    vTaskDelete(NULL);
}

BaseType_t sd_card_task_create(void)
{
    return xTaskCreate(sd_card_task,
                       "SD Card Init",
                       SD_CARD_TASK_STACK_SIZE,
                       NULL,
                       SD_CARD_TASK_PRIORITY,
                       NULL);
}
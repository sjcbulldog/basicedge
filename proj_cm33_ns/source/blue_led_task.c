#include "blue_led_task.h"

#include "cybsp.h"
#include "task.h"

static void blue_led_task(void *argument)
{
    CY_UNUSED_PARAMETER(argument);

    Cy_GPIO_Write(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_PIN, 1UL);

    vTaskDelete(NULL);
}

BaseType_t blue_led_task_create(void)
{
    return xTaskCreate(blue_led_task,
                       "Blue LED",
                       configMINIMAL_STACK_SIZE,
                       NULL,
                       tskIDLE_PRIORITY + 1U,
                       NULL);
}

void vApplicationMallocFailedHook(void)
{
    CY_ASSERT(0);
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    CY_UNUSED_PARAMETER(task);
    CY_UNUSED_PARAMETER(task_name);
    CY_ASSERT(0);
    for (;;)
    {
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_tcb,
                                   StackType_t **task_stack,
                                   uint32_t *stack_size)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

    *task_tcb = &idle_task_tcb;
    *task_stack = idle_task_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

#if (configUSE_TIMERS == 1)
void vApplicationGetTimerTaskMemory(StaticTask_t **task_tcb,
                                    StackType_t **task_stack,
                                    uint32_t *stack_size)
{
    static StaticTask_t timer_task_tcb;
    static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

    *task_tcb = &timer_task_tcb;
    *task_stack = timer_task_stack;
    *stack_size = configTIMER_TASK_STACK_DEPTH;
}
#endif
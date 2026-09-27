#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__GNUC__)
#include "cy_utils.h"
#include "cycfg_system.h"
#include "cy_device_headers.h"
#endif

#if defined(__ICCARM__) || defined(__GNUC__)
extern uint32_t SystemCoreClock;
#endif

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configCPU_CLOCK_HZ                      SystemCoreClock
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                256
#define configMINIMAL_SECURE_STACK_SIZE         256
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5

#if defined(COMPONENT_CYW20829) || defined(COMPONENT_CYW89829) || defined(MTB_SOFTFLOAT)
#define configENABLE_FPU                        0
#else
#define configENABLE_FPU                        1
#endif
#define configENABLE_MPU                        0
#if defined(COMPONENT_FREERTOS_TZ)
#define configENABLE_TRUSTZONE                  1
#else
#define configENABLE_TRUSTZONE                  0
#endif

#if defined(COMPONENT_SECURE_DEVICE) && !defined(COMPONENT_FREERTOS_TZ)
#define configRUN_FREERTOS_SECURE_ONLY          1
#else
#define configRUN_FREERTOS_SECURE_ONLY          0
#endif

#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(50 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

#if defined(COMPONENT_SECURE_DEVICE) || defined(COMPONENT_CYW20829) || defined(COMPONENT_CYW89829)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x20
#else
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x40
#endif
#define configMAX_API_CALL_INTERRUPT_PRIORITY   configMAX_SYSCALL_INTERRUPT_PRIORITY

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              1

#if defined(NDEBUG)
#define configASSERT(x) CY_UNUSED_PARAMETER(x)
#else
#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); CY_HALT(); }
#endif

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#define HEAP_ALLOCATION_TYPE1                   1
#define HEAP_ALLOCATION_TYPE2                   2
#define HEAP_ALLOCATION_TYPE3                   3
#define HEAP_ALLOCATION_TYPE4                   4
#define HEAP_ALLOCATION_TYPE5                   5
#define NO_HEAP_ALLOCATION                      0
#define configHEAP_ALLOCATION_SCHEME            HEAP_ALLOCATION_TYPE3

#if defined(CY_CFG_PWR_SYS_IDLE_MODE) && \
    ((CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_SLEEP) || \
     (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) || \
     (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP_RAM))
extern void vApplicationSleep(uint32_t expected_idle_time);
#define portSUPPRESS_TICKS_AND_SLEEP(idle_time) vApplicationSleep(idle_time)
#define configUSE_TICKLESS_IDLE                 2
#else
#define configUSE_TICKLESS_IDLE                 0
#endif

#if (CY_CFG_PWR_DEEPSLEEP_LATENCY > 0)
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   CY_CFG_PWR_DEEPSLEEP_LATENCY
#endif

#if defined(__llvm__) && !defined(__ARMCC_VERSION)
#define configUSE_PICOLIBC_TLS                  1
#else
#define configUSE_NEWLIB_REENTRANT              1
#endif

#endif
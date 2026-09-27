# FreeRTOS Configuration Guide

Detailed scope questions, parameter tables, profile defaults, and required hook stubs.
Referenced from [SKILL.md](../SKILL.md).

---

## Q3: Total Heap Size

Only relevant when heap scheme is **not** `heap_3` and **not** `NO_HEAP_ALLOCATION`.
For `heap_3`, the heap size is controlled by the linker script — `configTOTAL_HEAP_SIZE` is ignored.

| Profile | Suggested Default | Rationale |
|---------|------------------|-----------|
| IoT / Networking | 51200 (50 KB) | WiFi stack and networking buffers are large consumers |
| Low-Power | 10240 (10 KB) | Minimal task set; tight SRAM budget |
| General Embedded | 20480 (20 KB) | Comfortable for 3–8 tasks with moderate objects |
| Real-Time Control | 16384 (16 KB) | Predictable allocation; heap_3 default |

Offer the suggested value as the default. Accept a custom size. Warn if the value exceeds ~75% of available SRAM for the detected BSP.

---

## Q5: Max Task Priorities — Details

| Choice | `configMAX_PRIORITIES` | Notes |
|--------|----------------------|-------|
| 3 levels (low / normal / high) | 3 | Simple applications; lowest overhead |
| 5 levels | 5 | Moderate applications with ISR-deferred tasks |
| **7 levels** (default) | 7 | Sufficient for most designs including WiFi/networking stacks |
| Custom | User-entered | Practical max ~32; each additional level adds minimal RAM |

Note: The timer daemon task uses `configTIMER_TASK_PRIORITY`; ensure it is within `0` to `configMAX_PRIORITIES - 1`.

---

## Q6: Tickless Idle — Details

| Choice | `configUSE_TICKLESS_IDLE` | Notes |
|--------|--------------------------|-------|
| **Auto-detect from Device Configurator** (default) | 2 if `CY_CFG_PWR_SYS_IDLE_MODE` is Sleep/DeepSleep | Preserved as conditional block in generated config; no manual choice needed |
| Always enable | 2 | Requires `abstraction-rtos` for compatible `vApplicationSleep` hook; offer to install |
| Disabled | 0 | Standard tick; no sleep hook needed |

> **⚠️ PSOC Edge (PSE84) WARNING:** Tickless idle causes deep sleep, which **corrupts SCB peripheral clock state** (UART, SPI, I2C). Symptoms: garbled UART output (`HýHý...` pattern) after first `vTaskDelay()`. **Disable tickless idle** (`configUSE_TICKLESS_IDLE 0`) unless SysPm callbacks are implemented to save/restore all active SCB peripheral state. This is the #1 PSE84 FreeRTOS failure mode.

---

## Template Selection by CPU Core

After `make getlibs`, copy the appropriate template:

| CPU Core | Template Path |
|----------|--------------|
| CM4 | `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_CM4/FreeRTOSConfig.h` |
| CM33 | `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_CM33/FreeRTOSConfig.h` |
| CM0P | `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_CM0P/FreeRTOSConfig.h` |
| CM7 | `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_CM7/FreeRTOSConfig.h` |
| CM55 | `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_CM55/FreeRTOSConfig.h` |

Copy to the **project root** (alongside `main.c` / `Makefile`).

**Remove this line from the copied file immediately:**
```c
#warning This is a template. Copy this file to your project and remove this line. Refer to FreeRTOS README.md for usage details.
```

---

## Parameter Modification Table

Apply the values from the user's scope question answers. Rows marked **Keep** should not be changed from the template default unless the user explicitly requests it.

| Parameter | Template Default | Action |
|-----------|-----------------|--------|
| `configUSE_PREEMPTION` | `1` | Keep |
| `configTICK_RATE_HZ` | 1000 | Set per Q4 |
| `configMAX_PRIORITIES` | `7` | Set per Q5 |
| `configMINIMAL_STACK_SIZE` | 128 (CM4), 256 (CM33) | Keep template default; warn if DS-RAM is enabled (must be ≥256) |
| `configMINIMAL_SECURE_STACK_SIZE` | 256 (CM33 only) | Keep |
| `configTOTAL_HEAP_SIZE` | varies | Set per Q3; skip for heap_3 |
| `configHEAP_ALLOCATION_SCHEME` | `HEAP_ALLOCATION_TYPE3` | Set per Q2; default heap_3 for all profiles |
| `configUSE_TIMERS` | `1` | Set per Q7 |
| `configTIMER_TASK_PRIORITY` | `3` | Set to `configMAX_PRIORITIES - 2` for IoT profile; else keep `3` |
| `configTIMER_QUEUE_LENGTH` | `10` | Keep |
| `configTIMER_TASK_STACK_DEPTH` | `configMINIMAL_STACK_SIZE * 2` | Keep |
| `configUSE_TICKLESS_IDLE` | conditional | Set per Q6; preserve auto-detect block if "Auto" |
| `configCHECK_FOR_STACK_OVERFLOW` | `2` | Set per Q8 |
| `configUSE_MALLOC_FAILED_HOOK` | `1` | Keep `1`; user must provide hook (generated below) |
| `configUSE_IDLE_HOOK` | `0` | Keep unless user requests it |
| `configUSE_TICK_HOOK` | `0` | Keep unless user requests it |
| `configUSE_NEWLIB_REENTRANT` | `1` (non-LLVM) | Keep `1` for GCC/ARM; set `configUSE_PICOLIBC_TLS 1` for LLVM |
| `configUSE_TRACE_FACILITY` | `1` | Keep; required for `vTaskList()` and `uxTaskGetSystemState()` |
| `configGENERATE_RUN_TIME_STATS` | `0` | Keep; note available for profiling if user asks |
| `configSUPPORT_STATIC_ALLOCATION` | `1` | Keep `1`; set `0` only if user explicitly opts out of static APIs |
| `configSUPPORT_DYNAMIC_ALLOCATION` | `1` | Set `0` only if `NO_HEAP_ALLOCATION` selected |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | device-specific | Keep template value; explain to user that ISRs calling FreeRTOS API must have priority ≤ this value |

---

## Application-Profile Defaults Summary

| Parameter | IoT / Networking | Low-Power | General Embedded | Real-Time Control | Static / Safety |
|-----------|:---:|:---:|:---:|:---:|:---:|
| `configTICK_RATE_HZ` | 1000 | 200 | 1000 | 1000 | 1000 |
| `configMAX_PRIORITIES` | 7 | 5 | 7 | 7 | 5 |
| `configTOTAL_HEAP_SIZE` | 51200 | 10240 | 20480 | 16384 | N/A |
| `configHEAP_ALLOCATION_SCHEME` | heap_3 | heap_3 | heap_3 | heap_3 | NO_HEAP |
| `configUSE_TIMERS` | 1 | 1 | 1 | 0 | 0 |
| `configUSE_TICKLESS_IDLE` | 0 | 2 (auto) | 0 | 0 | 0 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 2 | 2 | 2 | 2 |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | 1 | 1 | 1 | 0 |
| `configSUPPORT_STATIC_ALLOCATION` | 1 | 1 | 1 | 1 | 1 |
| `configSUPPORT_DYNAMIC_ALLOCATION` | 1 | 1 | 1 | 1 | 0 |

---

## Required Hook Function Stubs

Generate these stubs in `freertos_integration.c` (or inform the user they must be provided) based on which hooks are enabled.

### `vApplicationMallocFailedHook` — required when `configUSE_MALLOC_FAILED_HOOK = 1`

```c
void vApplicationMallocFailedHook(void)
{
    CY_ASSERT(0);
}
```

### `vApplicationStackOverflowHook` — required when `configCHECK_FOR_STACK_OVERFLOW` = 1 or 2

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    CY_ASSERT(0);
}
```

### `vApplicationGetIdleTaskMemory` — required when `configSUPPORT_STATIC_ALLOCATION = 1`

```c
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer  = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize  = configMINIMAL_STACK_SIZE;
}
```

### `vApplicationGetTimerTaskMemory` — required when `configSUPPORT_STATIC_ALLOCATION = 1` and `configUSE_TIMERS = 1`

```c
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *puxTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer  = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *puxTimerTaskStackSize  = configTIMER_TASK_STACK_DEPTH;
}
```

---

## ⚠️ Hook Signature Variants by Device Family

FreeRTOS hook function signatures differ between device families due to type aliasing. Using the wrong signature causes **compile errors** (type mismatch) or **linker warnings** (weak symbol not overridden).

### `vApplicationGetIdleTaskMemory` and `vApplicationGetTimerTaskMemory`

The third parameter type varies:

| Device Family | `configSTACK_DEPTH_TYPE` resolves to | Use this signature |
|---|---|---|
| **PSOC 6** (CM4, CM0+) | `uint32_t` | `uint32_t *puxIdleTaskStackSize` |
| **PSE84** (CM33, CM55) | `configSTACK_DEPTH_TYPE` | `configSTACK_DEPTH_TYPE *puxIdleTaskStackSize` |
| **PSOC 4** (CM0+) | `uint16_t` | `uint16_t *puxIdleTaskStackSize` |

> **Best practice:** Always use `configSTACK_DEPTH_TYPE` in the signature (as shown in the templates above). This typedef resolves correctly on all platforms. If the FreeRTOS template from `COMPONENT_<CORE>` uses `uint32_t` directly, match it exactly — do not mix.

### `vApplicationStackOverflowHook`

Signature is consistent across all Infineon device families:
```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
```

### Common Pitfall: GCC `-Werror` and Signature Mismatch

When `configCHECK_FOR_STACK_OVERFLOW` or `configSUPPORT_STATIC_ALLOCATION` is enabled and GCC `-Wall -Werror` is active:
- A mismatched pointer type (e.g., `uint32_t*` where `configSTACK_DEPTH_TYPE*` is expected) triggers `-Wincompatible-pointer-types` → build failure
- **Fix:** Copy the exact signature from the BSP's FreeRTOS template (`../mtb_shared/freertos/<version>/Source/portable/COMPONENT_<CORE>/FreeRTOSConfig.h`) — it contains the correct types for that core

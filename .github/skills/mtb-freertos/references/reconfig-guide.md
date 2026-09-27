# FreeRTOS Reconfiguration & Troubleshooting Guide

How to diagnose, reconfigure, and fix an existing FreeRTOS installation.
Referenced from [SKILL.md](../SKILL.md).

---

## Reconfiguration Steps

### 1. Read Current Configuration

Parse `FreeRTOSConfig.h` from the project root and extract current values of all managed parameters.

### 2. Surface Settings to the User

Present a concise summary before asking what to change. Example:

> *Here is your current FreeRTOS configuration:*
> - *Tick rate: 1000 Hz*
> - *Max priorities: 7*
> - *Heap scheme: heap_3 (system malloc/free)*
> - *Total heap: N/A (heap_3 — size is in the linker script)*
> - *Stack overflow detection: level 2*
> - *Tickless idle: disabled*
> - *Software timers: enabled (priority 3)*

### 3. Ask What to Change

> *What would you like to change? You can describe a problem (e.g., "tasks are crashing", "I need more heap", "I want low-power sleep") or ask me to walk through all settings again.*

Accept free-form problem descriptions and map them to targeted fixes using the table below. Only re-run the full scope question sequence if the user asks to "reconfigure from scratch".

### 4. Confirm Before Modifying

Always show a diff-style summary of proposed changes before touching any file:

> *I'll make the following changes to your `FreeRTOSConfig.h`:*
> - *`configTOTAL_HEAP_SIZE`: 10240 → 32768*
> - *`configCHECK_FOR_STACK_OVERFLOW`: 0 → 2*
>
> *Proceed?*

### 5. Apply and Rebuild

Modify `FreeRTOSConfig.h` in place. Run `make build`. Report any new errors.

---

## Problem-to-Fix Mapping

| User complaint | Targeted action |
|---------------|----------------|
| "Stack overflow detected" / "task crashes" / "task corrupts memory" | Increase the affected task's stack size; if `configCHECK_FOR_STACK_OVERFLOW` is < 2, enable level 2; enable `INCLUDE_uxTaskGetStackHighWaterMark 1` to diagnose minimum stack needed |
| "malloc failed" / "pvPortMalloc returns NULL" / "heap exhausted" / "out of heap" | For heap_3: increase the system heap in the BSP linker script; for other schemes: increase `configTOTAL_HEAP_SIZE`; verify `clib-support` is installed if using GCC and heap_3 |
| "ISR causes assert" / "ISR triggers hard fault" / "FreeRTOS assert from interrupt" | Explain `configMAX_SYSCALL_INTERRUPT_PRIORITY`: any ISR that calls a FreeRTOS API function must have its NVIC priority set to `configMAX_SYSCALL_INTERRUPT_PRIORITY` or lower (numerically higher); show the user how to set `NVIC_SetPriority()` correctly |
| "Tick count is wrong" / "vTaskDelay wrong duration" / "delays too long or too short" | Verify `configCPU_CLOCK_HZ` resolves to `SystemCoreClock` at the point the header is included; confirm `configTICK_RATE_HZ` matches the intended tick period |
| "Want low-power sleep" / "deep sleep between tasks" | Enable tickless idle (`configUSE_TICKLESS_IDLE 2`); offer to install `abstraction-rtos` for a compatible `vApplicationSleep` hook; remind the user to also configure the Device Configurator power personality |
| "Want software timers" | Set `configUSE_TIMERS 1`; add `vApplicationGetTimerTaskMemory` stub if `configSUPPORT_STATIC_ALLOCATION 1`; ensure `configTIMER_TASK_PRIORITY` < `configMAX_PRIORITIES` |
| "Want to profile CPU usage per task" | Enable `configGENERATE_RUN_TIME_STATS 1`; explain that the user must provide `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()` and `portGET_RUN_TIME_COUNTER_VALUE()` macros pointing to a free-running timer; offer to set up the required TCPWM timer via the `mtb-device-configurator` skill |
| "`vApplicationGetIdleTaskMemory` undefined" / "`vApplicationGetTimerTaskMemory` undefined" | `configSUPPORT_STATIC_ALLOCATION` is enabled; generate the missing hook stubs from [config-guide.md](./config-guide.md#required-hook-function-stubs) |
| "System seems non-deterministic" / "heap fragmentation" | Clarify: heap_3 delegates fragmentation to the toolchain allocator — fragmentation is a property of the application's allocation pattern, not FreeRTOS itself; recommend auditing which code calls `malloc`/`free` at runtime; if the user is certain they need FreeRTOS-controlled heap, explain heap_4 trade-offs and warn it is incompatible with direct `malloc()`/`free()` in application code |
| "Switching to static allocation" / "want no dynamic allocation" | Set `configSUPPORT_DYNAMIC_ALLOCATION 0`, `configHEAP_ALLOCATION_SCHEME NO_HEAP_ALLOCATION`; update all task/queue/semaphore/timer creation calls to their `...Static()` variants; generate required static memory hook stubs |
| "Multiple definitions of SysTick_Handler / PendSV_Handler" | The FreeRTOS port aliases in `FreeRTOSConfig.h` (`#define xPortSysTickHandler SysTick_Handler`) conflict with another definition; remove the duplicate from the application or BSP startup file, keeping only the FreeRTOS alias |
| "Scheduler never starts" / `vTaskStartScheduler()` returns | Most likely cause: insufficient heap for the idle task and timer task stacks; for heap_3 increase the system heap in the linker script; for other schemes increase `configTOTAL_HEAP_SIZE`; verify `configMINIMAL_STACK_SIZE` ≥ 128 (CM4) or ≥ 256 (CM33 / DS-RAM enabled) |

---

## Diagnosing Stack Size with High Water Mark

If the user experiences stack overflows or crashes, suggest enabling the high water mark API to identify the minimum stack needed:

1. Set `INCLUDE_uxTaskGetStackHighWaterMark 1` in `FreeRTOSConfig.h`
2. Add a call inside the affected task:
   ```c
   UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
   /* hwm is the minimum free stack words remaining since task started.
    * If hwm is 0, overflow has already occurred.
    * Increase the task stack size until hwm is comfortably > 0. */
   ```
3. Build and run; observe `hwm` via debugger or printf
4. Increase the task's stack size by approximately `(observed minimum hwm) * 2` words as a safety margin

---

## clib-support Verification

When the user reports malloc-related issues and the project uses GCC with heap_3, verify `clib-support` is properly installed:

1. Check `deps/clib-support.mtb` exists
2. Check `../mtb_shared/clib-support/<version>/` is present
3. Confirm `configUSE_NEWLIB_REENTRANT 1` is set in `FreeRTOSConfig.h`
4. Confirm `CY_RTOS_AWARE` is in `DEFINES` in the `Makefile`

If any of these are missing, install `clib-support` via `mtb-library-installer` and add the missing items.

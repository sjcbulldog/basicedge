# FreeRTOS Interrupt Priority — BASEPRI Gotchas

> **Cross-cutting concern.** This applies to EVERY peripheral that uses interrupts
> in a project that links FreeRTOS libraries — even if the scheduler is never started.

## The Problem

FreeRTOS port code (`port.c` / `portmacro.h`) sets the ARM `BASEPRI` register to
`configMAX_SYSCALL_INTERRUPT_PRIORITY` during initialization. On Cortex-M devices,
BASEPRI masks all interrupts at that priority level or numerically higher (lower urgency).

**Critical:** This happens even if `vTaskStartScheduler()` is never called. Linking the
FreeRTOS library is sufficient to trigger the port init code on some BSPs.

## Priority Bit Width — How to Determine

ARM Cortex-M implements only the top N bits of the 8-bit priority register.
The mapping between "logical priority" and "register value" depends on `__NVIC_PRIO_BITS`.

### ⛔ Do NOT assume `__NVIC_PRIO_BITS` from the core architecture. ALWAYS read it from the project.

**Lookup procedure:**

1. Search for `#define __NVIC_PRIO_BITS` in the BSP device headers:
   ```
   bsps/TARGET_*/cy_device_headers_ns.h    (for NS core)
   bsps/TARGET_*/cy_device_headers_s.h     (for secure core)
   bsps/TARGET_*/cy_device_headers.h       (single-core devices)
   ```

2. Search for `configMAX_SYSCALL_INTERRUPT_PRIORITY` in the target sub-project's `FreeRTOSConfig.h`:
   ```
   <project>/FreeRTOSConfig.h
   ```
   Note: this file may contain conditional `#ifdef` blocks — find the active definition for your core.

3. Calculate: `shift = 8 - __NVIC_PRIO_BITS`, `register_value = priority << shift`

### Reference table (for verification after reading the header — NOT as a shortcut):

| `__NVIC_PRIO_BITS` | Shift | Priority 1 → reg | Priority 3 → reg |
|---|---|---|---|
| 3 | `<< 5` | 0x20 | 0x60 |
| 4 | `<< 4` | 0x10 | 0x30 |

## What Gets Masked

`configMAX_SYSCALL_INTERRUPT_PRIORITY` is the BASEPRI threshold. Read it from
`FreeRTOSConfig.h` — do not assume a value.

**Example (for illustration, not universal) — PSE84 with `__NVIC_PRIO_BITS = 3`:**

| Priority | Register Value | Masked by BASEPRI=0x40? |
|---|---|---|
| 0 | 0x00 | ❌ Never maskable |
| 1 | 0x20 | ❌ Below threshold |
| 2 | 0x40 | ✅ **YES** — equals threshold |
| 3+ | 0x60+ | ✅ **YES** — above threshold |

**Result:** Only priorities with register value < `configMAX_SYSCALL_INTERRUPT_PRIORITY` fire.

## Scenario: FreeRTOS Linked but Scheduler Not Started

This is the most insidious case. Projects that:
- Use FreeRTOS-aware middleware (e.g., retarget-io with mutex support)
- Link `freertos.lib` for future RTOS use
- Include BSP configs that pull in FreeRTOS port code

...will have BASEPRI set to a non-zero value during `init_cycfg_all()` or BSP init,
even in what appears to be a bare-metal `main()` loop.

**Symptoms:**
- All interrupts appear correctly configured (NVIC enabled, priority set, edge configured)
- ISR never fires
- Polling the raw interrupt flag (INTR register) shows the hardware event IS occurring
- No compiler errors, no runtime errors — just silence

## Fixes

### Fix 1: Clear BASEPRI after init (bare-metal with FreeRTOS libs)

```c
int main(void)
{
    cybsp_init();
    init_peripherals();

    /* FreeRTOS port code may have set BASEPRI during init.
     * Clear it so all configured interrupts can fire. */
    __set_BASEPRI(0U);

    // ... rest of main
}
```

### Fix 2: Assign priority below BASEPRI threshold (if scheduler IS running)

When the FreeRTOS scheduler is active and BASEPRI is intentionally non-zero:
- ISRs that call FreeRTOS APIs (`xSemaphoreGiveFromISR`, `xQueueSendFromISR`, etc.)
  MUST use priorities at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`
- ISRs that do NOT call FreeRTOS APIs can use priority 1 to escape BASEPRI

### Fix 3: Startup self-test (recommended)

Verify interrupts actually fire during init:

```c
volatile bool isr_test_fired = false;

void GPIO_ISR(void) {
    isr_test_fired = true;
    Cy_GPIO_ClearInterrupt(BTN_PORT, BTN_PIN);
    __DSB();
    NVIC_ClearPendingIRQ(BTN_IRQ);
}

/* After full interrupt setup: */
NVIC_SetPendingIRQ(BTN_IRQ);    /* Software-trigger the ISR */
__DSB();
__ISB();
assert(isr_test_fired);         /* If this fails, BASEPRI is masking you */
```

## Priority Decision Rule

After looking up `__NVIC_PRIO_BITS` and `configMAX_SYSCALL_INTERRUPT_PRIORITY`:

| ISR calls FreeRTOS `*FromISR` APIs? | Required priority register value | Masked by BASEPRI? |
|---|---|---|
| Yes | Must be **≥** `configMAX_SYSCALL_INTERRUPT_PRIORITY` | ✅ Yes — this is intentional and required |
| No, and must NOT be masked | Must be **<** `configMAX_SYSCALL_INTERRUPT_PRIORITY` | ❌ No — fires even in critical sections |
| No preference | Any valid priority | Depends on value |

**Rule:** If BASEPRI ≠ 0 and you haven't cleared it, only ISRs with register value < BASEPRI will fire.

## How to Check Your Project

```c
uint32_t bp = __get_BASEPRI();
if (bp != 0) {
    /* Priorities with register value >= bp are masked */
}
```

## Tickless Idle + BASEPRI Interaction

When `configUSE_TICKLESS_IDLE` is enabled (1 or 2), the FreeRTOS port enters a low-power
sleep via `WFI` when no tasks are ready. **Before `WFI`, the port sets BASEPRI** to
`configMAX_SYSCALL_INTERRUPT_PRIORITY` as part of its critical section.

### The `portMAX_DELAY` Gotcha

If a task blocks on a semaphore/queue with `portMAX_DELAY` and the only wake source is a
peripheral ISR at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`:

1. Scheduler sees no deadline → programs maximum sleep duration
2. Port enters critical section → BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY` (masks ISRs at that level and below)
3. `WFI` executes → CPU sleeps
4. Peripheral interrupt pends → but BASEPRI blocks ISR delivery during the port's
   critical section exit window
5. **Result:** ISR appears to never fire; task stays blocked indefinitely

**Fix:** Use a finite timeout instead of `portMAX_DELAY`:

```c
// WRONG — ISR may never deliver with tickless idle:
xSemaphoreTake(sem, portMAX_DELAY);

// CORRECT — periodic wake ensures ISR delivery window:
xSemaphoreTake(sem, pdMS_TO_TICKS(<wake_interval_ms>));
// Choose interval based on latency tolerance (e.g., 500-5000 ms)
```

The finite timeout causes the scheduler to program a shorter sleep, and the periodic
LPTimer wake provides a window where BASEPRI is cleared and the pending ISR fires.

### Detection

During environment detection (Step 2), always check:
- Search for `configUSE_TICKLESS_IDLE` in `FreeRTOSConfig.h`
- If value is 1 or 2: warn the user and use finite timeouts for ISR-signaled semaphores

See [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §3 for the full tickless interaction pattern.

## Related

- [`pre-coding-verification.md`](./pre-coding-verification.md) §4 — FreeRTOS ISR Checklist (uses this file's lookup procedure)
- [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) — universal gotchas (clock trees, pin mux, BSP repurposing)
- [`pse84-gotchas.md`](./pse84-gotchas.md) — device-specific priority bits (PSE84 only)
- [`timer-counter-patterns.md`](./timer-counter-patterns.md) — TCPWM ISR patterns that depend on correct priority assignment

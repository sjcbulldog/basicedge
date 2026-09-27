# GPIO Interrupt Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### Mask-based single-button ISR
Use the port IRQ as a hardware fan-in, but enable the interrupt mask only on the button pin you own.

1. Set the edge on the button pin.
2. Clear masks on unrelated pins that share the same port.
3. Clear the button's pending GPIO flag and NVIC pending bit.
4. Enable the button mask.
5. In the ISR: disable the NVIC line, clear the button flag, issue `__DSB()`, clear NVIC pending, then notify the application.

```c
/* One button owns the port IRQ because only its mask is 1 */
NVIC_DisableIRQ(BTN_IRQ);
Cy_GPIO_ClearInterrupt(BTN_PORT, BTN_PIN);
__DSB();
NVIC_ClearPendingIRQ(BTN_IRQ);
xSemaphoreGiveFromISR(btn_sem, &xHigherPriorityTaskWoken);
```

### Debounce + re-enable-from-task
Do not re-enable the GPIO NVIC line inside the ISR. Let a task or timer callback handle debounce, then re-arm the interrupt.

```c
/* task context */
vTaskDelay(pdMS_TO_TICKS(50));   /* 50-200 ms depending on switch quality */
Cy_GPIO_ClearInterrupt(BTN_PORT, BTN_PIN);
__DSB();
NVIC_ClearPendingIRQ(BTN_IRQ);
NVIC_EnableIRQ(BTN_IRQ);
```

### Same-port multi-button discrimination
If two buttons share one GPIO port, set masks on both pins and branch on masked status in the ISR. Clear each asserted pin before leaving.

```c
if (Cy_GPIO_GetInterruptStatusMasked(BTN1_PORT, BTN1_PIN)) { /* handle BTN1 */ }
if (Cy_GPIO_GetInterruptStatusMasked(BTN2_PORT, BTN2_PIN)) { /* handle BTN2 */ }
__DSB();
NVIC_ClearPendingIRQ(PORT_IRQ);
```

### Bare-metal variant
Without FreeRTOS, replace the semaphore with a volatile flag. Debounce in the super-loop, then clear the GPIO/NVIC state and re-enable the IRQ after the delay.

### Priority cheat sheet

> ⚠️ **BASEPRI WARNING:** These priorities are only valid if BASEPRI has been cleared.
> See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) for the full
> explanation of how FreeRTOS masks interrupts — even without starting the scheduler.

| Use case | Recommended CM33 priority |
|---|---:|
| DMA completion | 1 |
| UART / SPI / I2C | 2-3 |
| Timer / periodic work | 3-4 |
| GPIO buttons / user input | 5-6 |
| Background / low urgency | 7 |

### Shared-port cleanup on KIT_PSE84_EVAL_EPC2
Port 8 is the common trap. Before enabling a button interrupt, verify which Port 8 pins already have masks enabled by the BSP.

| Port | Pins | Shared functions |
|---|---|---|
| Port 8 | P8.0-P8.7 | I2C SCL (P8.0), I2C SDA (P8.1), BTN1/SW2 (P8.3), PDM CLK (P8.4), PDM DATA (P8.5), BTN2/SW4 (P8.7) |

BSPs often ship with SW2/SW4 interrupt masks enabled. Disable masks on buttons you do not own before enabling your ISR, or unrelated presses will retrigger your handler.

## Gotchas

### FreeRTOS BASEPRI masking (CRITICAL)

⚠️ **If your project links FreeRTOS libraries (even without starting the scheduler), BASEPRI may
silently mask ALL GPIO interrupts.** See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md)
for the full explanation and fix (`__set_BASEPRI(0U)` after init).

**Symptoms:** ISR never fires, NVIC is enabled, edge is configured, hardware event occurs (visible
in raw INTR register) — but the handler is never called. This is the #1 cause of "interrupt doesn't
work" on projects that link FreeRTOS.

Any ISR that calls `xSemaphoreGiveFromISR()` or similar must also stay within the project's
FreeRTOS syscall-safe priority range (at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`).
Do not use priority 0 with FreeRTOS APIs.

> **If BASEPRI is clear and the ISR still doesn't fire**, check whether FreeRTOS tickless idle
> is entering deep sleep — see `mtb-freertos/SKILL.md` (Tickless Idle section) for deep sleep
> interactions with interrupt-driven peripherals.

### LED polarity is board-specific
On KIT_PSE84_AI and KIT_PSE84_EVAL_EPC2, the user LED is active-high: writing `1` turns it on. Many other Infineon kits are active-low. If your button ISR toggles an LED and the logic looks inverted, check the board polarity before debugging the interrupt code.

### HSIOM mismatch
Drive mode alone does not route a pin. If Device Configurator moved a pin to a peripheral HSIOM function, your GPIO assumptions can be wrong even though the code compiles. Verify the pin is still a GPIO input for buttons, and verify output LEDs are routed where you expect.

### `__DSB()` before `NVIC_ClearPendingIRQ()` *(pending PDL doc fix)*
On Cortex-M33/M55, clearing the GPIO interrupt flag can sit in the write buffer for a few cycles. If you clear NVIC pending first, the ISR can immediately re-enter with no new edge. Always place `__DSB()` between the GPIO clear and the NVIC clear.

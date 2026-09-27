# Pre-Coding Verification (MANDATORY)

**Before writing ANY application code that uses a Device Configurator-configured peripheral,** complete this verification checklist. Do not skip — generating code without verification produces non-functional firmware.

### Locating `GeneratedSource/` in your project:

1. First check for the BSP-level config in `bsps/TARGET_<bsp>/config/GeneratedSource/` (shared across cores)
2. If not there, search the workspace, ignoring `build` directories, for a `TARGET_<bsp>` directory that contains a `config/GeneratedSource` tree with a `cycfg_peripherals.h` file 

## 1. Verify Pin Routing

- Inspect `cycfg_pins.h` and `cycfg_routing.h` in `GeneratedSource/` to confirm the peripheral's pins are routed via HSIOM
- For TCPWM (PWM/Timer): verify the output pin's HSIOM is set to the TCPWM line function, not GPIO
- For SCB (UART/I2C/SPI): verify RX/TX or SCL/SDA pins match the expected BSP macros

### GPIO HSIOM Conflict Check

For **every GPIO pin your application controls directly** (LEDs, buttons, sensor enables, chip selects, external interrupt lines):

1. Read `cycfg_routing.h` in `GeneratedSource/`
2. Search for `ioss_0_port_<N>_pin_<M>_HSIOM` defines matching your pin
3. If the HSIOM is set to anything other than `HSIOM_SEL_GPIO` (e.g., `TCPWM0_LINE_*`, `SCB*_*`, `CANFD*_*`), the pin is **hardware-muxed to a peripheral** — GPIO writes will be silently ignored

**Common trap:** BSP default `design.modus` may route LED or button pins to TCPWM for PWM dimming, or to other peripherals for alternate functions. If your application uses simple GPIO control (digital write, read, or interrupt), you must disconnect the conflicting peripheral routing in Device Configurator first.

**Symptom:** GPIO pin does not respond to `Cy_GPIO_Write()` or `Cy_GPIO_Set()` / `Cy_GPIO_Clr()`, even though drive mode and pin config appear correct. Other pins on the same port work fine. This is NOT a TrustZone issue — it is an HSIOM mux conflict.

## 2. Verify Clock Configuration

- Check `cycfg_clocks.h` for the peripheral clock divider assignment
- For TCPWM: verify a peripheral clock divider is assigned and enabled — without it, the counter doesn't increment
- For SCB: verify the clock divider produces the expected baud rate / bus speed

### Clock Divider Values — GUI vs Register

Device Configurator GUI divider values are **1-based** (the actual division ratio). The personality XML converts to the 0-based register value automatically (`intDivider - 1`).

| Layer | Value format | Example (divide-by-2000) |
|-------|-------------|--------------------------|
| **DC GUI** | 1-based (enter the division ratio) | **2000** |
| **Generated code** (`cycfg_clocks.c`) | 0-based (register value) | **1999U** |

**Formula:** `peripheral_freq = source_clock / GUI_divider_value`

⚠️ **Common agent error:** Computing `(source / target) - 1` and presenting that to the user. The minus-one conversion is done by DC — always present the **division ratio** (1-based) in the settings table.

When verifying after DC closes, read `cycfg_clocks.c` — the value there will be `GUI_value - 1`. This is correct.

### ⛔ Do NOT Manually Calculate Peripheral Clock Frequencies

Clock chains include intermediate dividers not visible in `cycfg_clocks.h`. **Always use
the Device Configurator UI's reported frequency.** If your calculation disagrees with DC,
your calculation is wrong. See [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §2 for details on
hidden intermediate dividers.

## 3. Verify Init Sequence Completeness

1. Find the matching peripheral pattern file (e.g., [`timer-counter-patterns.md`](./timer-counter-patterns.md), [`uart-runtime-patterns.md`](./uart-runtime-patterns.md))
2. Confirm ALL steps in the Init → Enable → Start sequence are present
3. Pay special attention to commonly forgotten steps (the #1 cause of "peripheral appears dead"):
   - TCPWM: `TriggerStart` after Enable
   - SCB: context struct at file scope (not local variable)
   - DMA: channel enable after descriptor init
4. If target is PSE84/PSOC Edge: also read [`pse84-gotchas.md`](./pse84-gotchas.md)

## 4. FreeRTOS ISR Checklist (MANDATORY before writing ANY ISR code)

If the project links FreeRTOS libraries, complete this checklist **before** writing interrupt
handler code. Document the values explicitly — do not assume from architecture knowledge.

### Required values to extract:

1. **`__NVIC_PRIO_BITS`** — Search for `#define __NVIC_PRIO_BITS` in:
   - `bsps/TARGET_*/cy_device_headers_ns.h` (NS core)
   - `bsps/TARGET_*/cy_device_headers_s.h` (secure core)
   - `bsps/TARGET_*/cy_device_headers.h` (single-core devices)

2. **`configMAX_SYSCALL_INTERRUPT_PRIORITY`** — Search in `<project>/FreeRTOSConfig.h`
   - Note: may have `#ifdef` conditionals — find the active definition for your core

3. **ISR priority register value** — Calculate: `priority << (8 - __NVIC_PRIO_BITS)`
   - If ISR calls `*FromISR` APIs: register value must be **≥** `configMAX_SYSCALL_INTERRUPT_PRIORITY`
   - If ISR must NOT be masked by BASEPRI: register value must be **<** `configMAX_SYSCALL_INTERRUPT_PRIORITY`

4. **`configUSE_TICKLESS_IDLE`** — Search in `<project>/FreeRTOSConfig.h`
   - If 1 or 2: use finite timeouts for ISR-signaled semaphores (never `portMAX_DELAY`)
   - See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) tickless section

### Verification artifact (include as comment in source):

```c
/* FreeRTOS ISR Verification:
 * __NVIC_PRIO_BITS        = ___
 * configMAX_SYSCALL_...   = 0x___
 * This ISR priority       = ___ (register value 0x___)
 * BASEPRI masks this ISR? = YES/NO
 * Tickless idle enabled?  = YES/NO → using finite timeout
 */
```

If you cannot fill in ALL values above, you have not read the required reference files.
Stop and read [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md),
[`device-configurator-gotchas.md`](./device-configurator-gotchas.md), and
[`pse84-gotchas.md`](./pse84-gotchas.md) (if PSE84 target) before proceeding.

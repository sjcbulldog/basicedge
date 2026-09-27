# Device Configurator Gotchas — Common Pitfalls

> **Read this file BEFORE writing peripheral init code.** These gotchas apply to ALL
> Infineon ModusToolbox devices (PSOC 6, PSOC Edge, PSOC Control, XMC7). They are
> confirmed from hardware testing and are NOT obvious from documentation.
>
> For device-specific gotchas, see also: [`pse84-gotchas.md`](./pse84-gotchas.md) (PSOC Edge E84)

## 1. IP Block Versioning — APIs Vary by Personality Version

PDL API availability depends on the IP version defined in the personality file, NOT the
core architecture. The personality file version (e.g., `counter2.0.cypersonality`) determines
which APIs exist.

**How to determine the IP version:**
1. Check the personality name shown in Device Configurator for the configured peripheral
2. Or search: `../mtb_shared/mtb-dsl-<device>/device-info/personalities/` for the relevant `.cypersonality` file
3. The version number in the filename (e.g., `counter2.0`) indicates the IP version

**How to verify API availability for your version:**
1. Check `mtb_shared/mtb-dsl-<device>/` (device-specific DSL — only what your device supports)
2. Check `mtb_shared/mtb-pdl-cat1/` (generic CAT1 PDL — may include APIs for other versions)
3. If a function is defined in generic PDL but not in the DSL, it is NOT available on your device
4. Each peripheral's reference file (e.g., [`timer-counter-patterns.md`](./timer-counter-patterns.md))
   documents version differences where applicable

**General rule:** Code examples from other device families may use APIs not available on
your target — always verify against the device-specific DSL headers.

## 2. Multi-Stage Clock Trees — Hidden Intermediate Dividers

Peripheral clock domains are often separated from the high-frequency source by **intermediate
dividers** (peri group dividers, bus dividers) that are not visible in the peripheral clock
configuration alone.

**The issue pattern:**
- You configure a peripheral clock divider and compute expected frequency from the HF clock
- The actual frequency is 2x–4x lower than calculated
- The discrepancy comes from an intermediate divider between the HF clock and the
  peripheral clock domain

### How to determine the actual peripheral clock frequency:

1. **Device Configurator UI (authoritative)** — Open the Clocks tab. The reported frequency
   for each peripheral clock accounts for ALL intermediate dividers. If your manual
   calculation disagrees with DC, your calculation is wrong.

2. **Trace the clock tree in generated source** — Search `GeneratedSource/cycfg_system.c`
   for peri group divider configuration (look for `Cy_SysClk_PeriGroupSetDivider` or
   equivalent clock tree setup calls).

3. **Runtime verification** — Use `Cy_SysClk_ClkPeriGetFrequency()` or the peripheral-specific
   frequency getter at runtime to confirm the actual input frequency.

### General rule:

**Never assume a simple `CLKHF / peripheral_divider` relationship.** Always verify the
actual frequency via Device Configurator or generated clock tree configuration before using
the value in timing calculations. See [`pre-coding-verification.md`](./pre-coding-verification.md) §2.

## 3. Tickless Idle + BASEPRI — ISR Masking During Sleep

When FreeRTOS tickless idle is enabled, the port raises BASEPRI before `WFI`, masking
peripheral ISRs during sleep. This applies to ANY Cortex-M device with FreeRTOS tickless idle.

### How to determine if affected:

1. Check `FreeRTOSConfig.h` for `configUSE_TICKLESS_IDLE` (if 0 or undefined → not affected)
2. Compare ISR priority register value against `configMAX_SYSCALL_INTERRUPT_PRIORITY`
   (see [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) for the lookup procedure)
3. If ISR is at or below ceiling AND task blocks with `portMAX_DELAY` → affected

**Fix:** Use `xSemaphoreTake(sem, pdMS_TO_TICKS(<wake_interval_ms>))` instead of `portMAX_DELAY`.

See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) § Tickless Idle for
the full mechanism explanation.

## 4. Pin Mux Constraints — HSIOM Determines Available Peripheral Functions

Each physical pin supports only a subset of peripheral functions, determined by the HSIOM
(High-Speed I/O Matrix) mux options. The available functions are NOT obvious from the
peripheral configuration alone — you must check the pin's HSIOM capabilities.

**Common trap:** Assuming a TCPWM counter's LINE output is available on a chosen pin, then
discovering only LINE_COMPL (inverted) is offered. Similarly, an SCB's SPI MOSI may not be
available on the pin you selected.

### How to determine pin function availability:

1. **Device Configurator UI** — When assigning a pin to a peripheral, the dropdown shows
   only the functions physically available on that pin. If your expected function is missing,
   the pin does not support it.

2. **BSP pin map file** — Check `bsps/TARGET_*/cybsp.h` and the BSP's pin package file
   for the HSIOM options per pin.

3. **Generated routing file** — After Device Configurator saves, inspect
   `GeneratedSource/cycfg_routing.h` for `ioss_0_port_<N>_pin_<M>_HSIOM` defines to
   confirm the actual mux selection.

### Impact when the available function differs from expected:

| Scenario | Impact | Mitigation |
|----------|--------|------------|
| Only LINE_COMPL available (not LINE) | PWM output is inverted | Adjust compare value: `compare = period - desired_high_time` |
| Only a specific SCB instance available on a pin | Cannot use the SCB you planned | Choose a different pin or redesign around the available SCB |
| Pin supports GPIO OR peripheral, not both | Cannot use pin for GPIO if peripheral is routed | Disconnect peripheral in DC to use as GPIO |

### General rule:

**Always verify pin-to-peripheral routing in Device Configurator BEFORE writing code that
assumes a specific output polarity or peripheral instance.**

## 5. BSP Pre-Configured Peripherals — Repurposing Conflicts

BSPs pre-configure peripherals for common board functions (LED dimming, USB timers, debug
UART, etc.). When you configure a peripheral instance in Device Configurator, you may be
repurposing one that the BSP already uses.

### How to discover BSP peripheral assignments:

1. **Device Configurator UI** — Open `design.modus`. Pre-configured peripherals appear with
   BSP-assigned names (e.g., `CYBSP_GENERAL_PURPOSE_TIMER`, `CYBSP_PWM_LED_CTRL`).

2. **BSP header** — Search `bsps/TARGET_*/cybsp.h` for `CYBSP_*` defines that map to
   peripheral instances (TCPWM counters, SCB instances, GPIO pins).

3. **Generated peripherals header** — Read `GeneratedSource/cycfg_peripherals.h` for all
   `#define` aliases and their mapped hardware instances.

### Risks when repurposing:

| Risk | Symptom | How to detect |
|------|---------|---------------|
| BSP code or library depends on the original config | Undefined behavior, missing functionality | Search for the BSP alias in BSP source and linked libraries |
| Mode change invalidates assumptions | Code expecting TC interrupts breaks after mode change | Verify no other source files reference the original instance/mode |

### Safe repurposing procedure:

1. Search the workspace for any references to the BSP alias
2. If no library or application code uses it → safe to repurpose
3. Change the mode/configuration in Device Configurator
4. The config struct regenerates automatically — no manual struct editing needed
5. Your custom aliases will coexist with the original BSP aliases

## Related

- [`pse84-gotchas.md`](./pse84-gotchas.md) — PSOC Edge E84 device-specific gotchas
- [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) — BASEPRI masking details
- [`pre-coding-verification.md`](./pre-coding-verification.md) — mandatory pre-coding checklist
- [`timer-counter-patterns.md`](./timer-counter-patterns.md) — TCPWM version differences

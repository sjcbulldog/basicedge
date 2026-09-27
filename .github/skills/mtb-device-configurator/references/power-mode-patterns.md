# Power Mode Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read `cy_syspm.h` directly (see `pdl-apis.md`).
>
> **Reference CEs:**
> - `mtb-example-psoc-edge-switching-power-modes` (CE238718) — canonical HP/LP/ULP/DeepSleep/Hibernate
> - `mtb-example-psoc-edge-power-measurements` (CE238719) — SID-based current measurement (all modes)
> - `mtb-example-psoc-edge-rtc-periodic-wakeup` (CE239556) — DeepSleep + Hibernate with RTC alarm
> - `mtb-example-psoc-edge-lpcomp-hibernate-wakeup` (CE239357) — Hibernate wakeup via LPComp
> - `mtb-example-psoc-edge-wlan-lowpower` (CE240066) — MCU DeepSleep + WLAN power save (LPA)
>
> **App Note:** AN241681 — Low-Power System Design with PSOC Edge + AIROC Wi-Fi/BT
>
> **Memory power control** (PD1, SRAM macros, SoCMEM): see [memory-power-patterns.md](./memory-power-patterns.md)

## Device Configurator Setup

PSE84 uses the **`power_v4`** personality (`power_v4-1.0.cypersonality`
in `personalities_8.0/platform/`). Resource: `srss[0].power[0]`.

### Key DC Fields

| DC Field | Options | What It Controls |
|----------|---------|-----------------|
| System Active Power Profile | **HP** (0.9V) · **LP** (0.8V) · **ULP** (0.7V) | Boot power mode; auto-derives CoreBuck + SRAMLDO voltages |
| System Idle Power Mode | Active · CPU Sleep · **System Deep Sleep** · DS-RAM · DS-OFF | Lowest mode entered when idle (RTOS tickless) |
| Deep Sleep Latency (ms) | 0–1000 | Max entry+exit latency; RTOS uses this to decide DS worthiness |
| VDDA / VDDD / VDDIO (mV) | 1700–3600 | Operating voltages (default 1800) |
| Hibernate Wakeup (0–3) | GPIO pin assignment | Wakeup sources; drive auto-set to `CY_GPIO_DM_PULLUP` |

> CoreBuck voltage/mode and SRAMLDO voltage are **read-only** — auto-derived from profile.

### Profile → Voltage Mapping (auto-derived)

| Profile | CoreBuck | CoreBuck Mode | SRAMLDO |
|---------|----------|---------------|---------|
| HP | 0.90V | HP | 0.90V |
| LP | 0.80V | HP | 0.80V |
| ULP | 0.70V | HP | 0.80V |

The generated `init_cycfg_power()` calls `Cy_SysPm_SystemEnterHp/Lp/Ulp()` +
`Cy_SysPm_SetDeepSleepMode()` based on these selections.

## Patterns

### Power mode hierarchy (PSE84 / CAT1D)

| Mode | Voltage | CM33 Max | CM55 Max | Flash Writes |
|------|---------|----------|----------|-------------|
| HP (High Performance) | 0.9V | 200 MHz | 400 MHz | ✅ |
| LP (Low Power) | 0.8V | 140 MHz | 140 MHz | ✅ |
| ULP (Ultra Low Power) | 0.7V | 50 MHz | 50 MHz | ❌ |
| DeepSleep | — | — | — | HF clocks off, full SRAM retain |
| DeepSleep-RAM | — | — | — | Configurable SRAM; CPU resets on wakeup |
| DeepSleep-OFF | — | — | — | No SRAM; CPU resets on wakeup |
| Hibernate | — | — | — | Full device reset; I/O frozen |

### Active mode transitions (HP ↔ LP ↔ ULP)

**Entering a lower mode** — reduce clock FIRST, then enter mode:

```c
/* HP → LP: clock FIRST, then mode, then RRAM + UART divider */
dpll_lp_set_freq(LP_INITIAL_FREQ);                // 1. reduce DPLL
Cy_SysPm_SystemEnterLp();                          // 2. enter LP
Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);  // 3. track RRAM voltage
recalc_uart_divider(LP_FREQ);                       // 4. fix peripheral clocks
dpll_lp_set_freq(LP_FINAL_FREQ);                    // 5. optionally raise to LP max
```

**Returning to HP** — enter mode FIRST, then raise clock:

```c
/* LP/ULP → HP: mode FIRST, then clock */
Cy_SysPm_SystemEnterHp();                           // 1. enter HP
Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);   // 2. track RRAM voltage
dpll_lp_set_freq(HP_FINAL_FREQ);                     // 3. raise DPLL
recalc_uart_divider(HP_FREQ);                         // 4. fix peripheral clocks
```

> See CE238718 `main.c` for the full DPLL reconfiguration helper (`dpll_lp_set_freq`)
> and UART divider calculation.

### DeepSleep entry (CAT1D critical rule)

> **MUST** call `Cy_SysPm_SetDeepSleepMode()` BEFORE `Cy_SysPm_CpuEnterDeepSleep()`.

```c
/* Standard DeepSleep (full SRAM retention) */
Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
/* Execution resumes here after wakeup */
```

For DeepSleep-RAM or DeepSleep-OFF, substitute the matching enum value.
See `cy_en_syspm_deep_sleep_mode_t` in `cy_syspm.h`.

> **Multi-CPU**: PSE84 enters system DeepSleep only when ALL CPUs have called
> `CpuEnterDeepSleep()`. If CM55 is active, CM33 alone won't trigger system DeepSleep.

For DeepSleep-RAM warm boot complexity, see [memory-power-patterns.md](./memory-power-patterns.md).

### Hibernate entry and wakeup detection

```c
/* Enter Hibernate — does NOT return */
while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));  // drain TX
Cy_SCB_UART_DeInit(CYBSP_DEBUG_UART_HW);                  // deinit peripherals
Cy_SysPm_SetHibernateWakeupSource(CY_SYSPM_HIBERNATE_PIN1_LOW);
Cy_SysPm_SystemEnterHibernate();
```

```c
/* Detect Hibernate wakeup (runs after device reset) */
if (CY_SYSLIB_RESET_HIB_WAKEUP ==
    (Cy_SysLib_GetResetReason() & CY_SYSLIB_RESET_HIB_WAKEUP))
{
    Cy_SysLib_ClearResetReason();
}
```

**Wakeup sources:** PIN0–PIN6 (programmable polarity), LPComp0/1, RTC alarm, WDT.
See `cy_en_syspm_hibernate_wakeup_source_t` in `cy_syspm.h`.

### Power callback registration

Callbacks notify peripherals of power transitions. They execute in registration
order entering low power, reverse order exiting. See `cy_syspm.h` for
`cy_en_syspm_callback_mode_t` and `cy_en_syspm_callback_type_t`.

```c
cy_en_syspm_status_t my_deepsleep_cb(
    cy_stc_syspm_callback_params_t *params, cy_en_syspm_callback_mode_t mode)
{
    switch (mode) {
        case CY_SYSPM_CHECK_READY:  return CY_SYSPM_SUCCESS; /* or FAIL to abort */
        case CY_SYSPM_BEFORE_TRANSITION: /* disable peripherals */ break;
        case CY_SYSPM_AFTER_TRANSITION:  /* re-enable after wakeup */ break;
        default: break;
    }
    return CY_SYSPM_SUCCESS;
}

cy_stc_syspm_callback_params_t cbParams = { .base = SCB0, .context = NULL };
cy_stc_syspm_callback_t cbConfig = {
    .callback = &my_deepsleep_cb, .type = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &cbParams, .order = 0U
};
Cy_SysPm_RegisterCallback(&cbConfig);
```

> **BSP auto-registers** `Cy_SysClk_DeepSleepCallback` for clock save/restore —
> do NOT register it manually.

### Power domains (PSE84)

| Domain | Contents | Control |
|--------|----------|---------|
| PD0 (System) | CM33, system peripherals, SRAM0 | Always active |
| PD1 (App) | CM55 + NPU, SRAM1 | See [memory-power-patterns.md](./memory-power-patterns.md) |
| SoCMEM | ~5 MB shared memory | See [memory-power-patterns.md](./memory-power-patterns.md) |

PPU and PDCM are internal to the PDL — application code does NOT call PPU/PDCM
APIs directly. Generated BSP config sets PPU modes in `cycfg_system.h`.

## Wakeup Source Quick Reference

| Mode | Wakeup Sources |
|------|---------------|
| Sleep | Any interrupt |
| DeepSleep | GPIO interrupt, RTC alarm, WDT, LPCOMP, WLAN (via LPA) |
| DeepSleep-RAM/OFF | Same as DeepSleep (CPU resets on wakeup) |
| Hibernate | Wakeup pins (PIN0–PIN6), LPComp0/1, RTC alarm, WDT |

## Gotchas

1. **Clock BEFORE mode transition** — ALWAYS reduce DPLL frequency before entering
   LP/ULP. Entering ULP at 200 MHz causes undefined behavior or lockup.

2. **RRAM voltage must match** — Call `Cy_RRAM_SetVoltageMode()` after every
   HP↔LP↔ULP transition. This is NOT automatic.

3. **DeepSleep variant selection** — On CAT1D, forgetting
   `Cy_SysPm_SetDeepSleepMode()` before `CpuEnterDeepSleep()` defaults to
   standard DeepSleep — which may not be what you want.

4. **Multi-CPU DeepSleep** — System enters DeepSleep only when ALL CPUs are in
   DeepSleep. If CM55 is still active, CM33 alone won't enter system DeepSleep.

5. **Hibernate — deinitialize peripherals** — Leaving SCB (UART/SPI/I2C)
   initialized before Hibernate causes wakeup recovery failures.

6. **No AFTER_TRANSITION for Hibernate** — Hibernate resets the device.
   Registered `CY_SYSPM_HIBERNATE` callbacks only get CHECK_READY and
   BEFORE_TRANSITION.

7. **Clear pending interrupts before DeepSleep** — A stale pending interrupt
   prevents DeepSleep entry. Clear with `NVIC_ClearPendingIRQ()`.

8. **ULP flash restriction** — Flash writes are prohibited in ULP mode.

9. **BSP auto-registers SysClk callback** — Do not manually register
   `Cy_SysClk_DeepSleepCallback`; it's already done in `cybsp.c`.

10. **UART baud rate changes with clock** — Recalculate UART divider after mode
    transitions or baud rate will be wrong.

11. **TCPWM / HF-clock peripherals stop in System DeepSleep** — System DeepSleep
    stops ALL HF clock trees. Any peripheral clocked by HF (TCPWM, SAR ADC, SCB)
    stops counting/converting. If your application uses TCPWM timers or PWM,
    register a `CY_SYSPM_DEEPSLEEP` callback that returns `CY_SYSPM_FAIL` on
    `CHECK_READY` to veto DeepSleep while measurements are active.

12. **"Works under debug, fails on physical reset" = DeepSleep bug** — The debug
    probe inhibits System DeepSleep (needs clocks for SWD access). If your
    application works with the probe attached but tasks hang after a standalone
    power cycle, the root cause is almost certainly an unintended DeepSleep entry
    stopping peripheral clocks. Check: (a) is CM55 idle entering tickless sleep?
    (b) is CM33 also entering DeepSleep? Both cores in DeepSleep → system
    DeepSleep → all HF clocks stop. Fix: veto callback or disable unused core.
    See also [memory-power-patterns.md](./memory-power-patterns.md) for PD1 disable.
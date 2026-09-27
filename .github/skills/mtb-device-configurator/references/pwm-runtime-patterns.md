# PWM Runtime Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### Frequency and duty-cycle math
| Mode | Period clocks | Frequency | Duty |
|---|---|---|---|
| Left-aligned | `period + 1` | `f_pwm = f_peri / (period + 1)` | `compare / (period + 1)` |
| Center-aligned | `2 * (period + 1)` | `f_pwm = f_peri / (2 * (period + 1))` | `compare / (period + 1)` |

Examples:
- 10 kHz at 1 MHz, left-aligned: `period = 99`, `compare = 50` for 50% duty.
- 20 kHz at 10 MHz, center-aligned: `period = 249`, `compare = 125` for 50% duty.

### Center-aligned dead-time math
Dead time is counted at the peripheral clock rate, not doubled by center alignment.

```text
deadTimeClocks = dead_time_seconds * peripheral_clock_hz
```

Examples:
- 200 ns at 10 MHz -> 2 clocks
- 200 ns at 20 MHz -> 4 clocks

### Glitch-free compare update (buffered swap)
For smooth dimming or motor ramps, use the buffered compare path so the new duty value swaps at terminal count.

**⛔ Prerequisite — compare swap must be enabled BEFORE writing the buffer:**

| Method | When to use |
|--------|-------------|
| Device Configurator → PWM personality → enable "Compare Swap" | Preferred — generates `.enableCompareSwap = true` in config struct |
| `Cy_TCPWM_PWM_EnableCompare0Swap(HW, NUM, true)` (call once after init) | Use when DC config wasn't set or you need dynamic control |

If `.enableCompareSwap = false` (the Device Configurator default), writing to the buffer register has **no effect** — the value never transfers to the active compare register. This is a silent failure with no error code.

```c
/* Enable compare swap (if not already set in Device Configurator config): */
Cy_TCPWM_PWM_EnableCompare0Swap(HW, NUM, true);

/* Then update duty — hardware swaps buffer→active at next terminal count: */
Cy_TCPWM_PWM_SetCompare0BufVal(HW, NUM, next_compare);
```

Direct compare updates (`Cy_TCPWM_PWM_SetCompare0Val`) are acceptable for coarse changes, but can generate a one-cycle glitch if the counter is already between the old and new thresholds.

> ⚠️ **When swap is enabled, `SetCompare0Val` alone is not enough.** At every terminal count the hardware overwrites the active `CC0` register from the buffer `CC0_BUFF`. If you write only the live register, the value survives for exactly one PWM period before being reset. See the **Compare-swap overwrite** gotcha below.

### Stop/restart pattern
To restart cleanly from the beginning of a PWM cycle:

```c
Cy_TCPWM_TriggerStopOrKill_Single(HW, NUM);
Cy_TCPWM_TriggerReloadOrIndex_Single(HW, NUM);
Cy_TCPWM_TriggerStart_Single(HW, NUM);
```

If you skip the reload, the counter resumes from its old value and the first pulse width can be wrong.

### Period change pattern
When frequency changes at runtime, stop first, update period and compare together, then reload and restart. If buffered period support is enabled, use the buffered path for seamless retiming.

### One-shot PWM
Configure one-shot mode when you need exactly one pulse per trigger. Typical uses: servo trigger pulses, strobes, and precise timing edges.

### TC interrupt pattern
Use terminal-count interrupts when application work must stay phase-locked to PWM. TC is a good place for synchronized updates; compare-match is better when you need a specific point inside the PWM cycle.

## Gotchas

### FreeRTOS BASEPRI may mask TCPWM interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent TCPWM overflow/compare ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### Pin-routing feasibility
Not every GPIO can emit the TCPWM line you want. Verify that the chosen pin actually exposes the required TCPWM route before designing around it.

### HSIOM correctness
A valid TCPWM config still produces a flat pin if the pin remains routed as GPIO. Always verify the final HSIOM route, not just the counter settings.

### BSP clock-divider trap
On KIT_PSE84_EVAL_EPC2, the default LED PWM divider is often much too slow for smooth dimming. The common trap is a divider of `50000`, which turns a nominal LED PWM into visible blinking. Recalculate from the actual peri-group clock, then set the divider intentionally.

### Compare-swap overwrite — `SetCompare0Val` alone silently resets every period

⚠️ When `enableCompareSwap = true` (Device Configurator) or `Cy_TCPWM_PWM_EnableCompare0Swap()` has been called, the hardware sets `AUTO_RELOAD_CC0 = 1`. At every terminal count (TC) the active compare register `CC0` is **automatically overwritten from the buffer register `CC0_BUFF`**.

If application code calls only `Cy_TCPWM_PWM_SetCompare0Val()`, the written value is visible for at most one PWM period, then the hardware silently resets it to whatever `CC0_BUFF` holds — typically the value from init. There is no error code.

**Symptom:** The PWM output steps to the correct duty for one cycle, then snaps back to a fixed value. A smooth ramp appears stuck or oscillates between the init value and a single target step.

**Fix — write both registers on every update:**
```c
Cy_TCPWM_PWM_SetCompare0Val(HW, NUM, compare);     /* immediate — applies this period */
Cy_TCPWM_PWM_SetCompare0BufVal(HW, NUM, compare);  /* buffer — persists through TC auto-reload */
```

**Diagnosis:** Read `CC0` and `CC0_BUFF` in the debugger immediately after your `SetCompare0Val` call. If `CC0_BUFF` still holds the init value while `CC0` shows the new value, the auto-reload will reset `CC0` at the next TC.

Note: this is the **opposite** of the `enableCompareSwap = false` failure (where buffer writes are ignored). Both are silent failures with no error code; the difference is which register is authoritative.

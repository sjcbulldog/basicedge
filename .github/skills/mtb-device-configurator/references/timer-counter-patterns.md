# Timer/Counter Runtime Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read `cy_tcpwm_counter.h` and `cy_tcpwm.h` directly (see `pdl-apis.md`).
>
> **Reference CEs:**
> - `mtb-example-psoc-edge-pwm-timer` — PSE84 periodic timer (1s LED toggle)
> - `mtb-example-psoc4-tcpwm-event-counter` — External event counting (button presses)
> - `mtb-example-ce240776-tcpwm-counter` — PWM-triggered counter with CC0 match
> - `mtb-example-pdl-xmc7000-tcpwm-counter` — XMC7000 counter
>
> **App Note:** AN220224 — How to use Timer, Counter, and PWM (TCPWM) in TRAVEO™ T2G family

## Device Configurator Setup

| Personality | Use Case |
|---|---|
| `counter` (v1) | PSOC 6, PSOC 4 — basic counter (no Capture1, no trigger routing) |
| `counter` (v2) | PSOC Edge, PSOC Control, XMC7 — full counter with Capture0/1, trigger I/O |
| `quaddec` / `quaddec_v2` | Quadrature encoder decoding |
| `pwm` | Use PWM personality if generating an output waveform |

### Counter personality — key DC parameters

The generated config struct (`cy_stc_tcpwm_counter_config_t`) mirrors the DC personality fields.
To discover available fields and valid values:
1. Open `cycfg_peripherals.h` → find your counter's config struct
2. Read `cy_tcpwm_counter.h` for the struct definition and valid enum values

**Critical DC parameters to verify:**
- **Period** — 0-based terminal count (period+1 clocks per cycle)
- **Compare or Capture** — determines whether CC0 triggers on match or latches count
- **Input modes** (Capture/Count/Start/Reload/Stop) — external signal routing from DC triggers
- **Trigger Events** — output trigger routing to other peripherals or DMA

### Generated defines (pattern)

Device Configurator generates these macros for each configured counter (names derive from
your DC personality instance name):

```c
#define <NAME>_HW       TCPWMx          // Base HW pointer
#define <NAME>_NUM      <n>UL           // Counter instance number
#define <NAME>_MASK     (1UL << <n>)    // Bitmask (v2+ — for Enable_Multiple)
#define <NAME>_IRQ      tcpwm_x_interrupts_<n>_IRQn  // If interrupts enabled
```

Find these in `GeneratedSource/cycfg_peripherals.h` after DC saves.

## Patterns

### Periodic timer with TC interrupt (canonical PSE84 pattern)

The most common use case — generate a fixed-rate tick and toggle/signal in the ISR.

```c
// DC setup: period = (interval_clocks - 1), Continuous, Count Up, TC interrupt enabled
// Example: 1s at 10kHz input clock → period = 9999

cy_stc_sysint_t timer_irq_cfg = {
    .intrSrc = MY_TIMER_IRQ,
    .intrPriority = <priority>  // See freertos-interrupt-priority.md — must not be masked by BASEPRI
};

// Init
Cy_TCPWM_Counter_Init(MY_TIMER_HW, MY_TIMER_NUM, &MY_TIMER_config);
Cy_SysInt_Init(&timer_irq_cfg, &timer_isr);
NVIC_EnableIRQ(timer_irq_cfg.intrSrc);

// Enable and start
Cy_TCPWM_Counter_Enable(MY_TIMER_HW, MY_TIMER_NUM);
Cy_TCPWM_TriggerStart_Single(MY_TIMER_HW, MY_TIMER_NUM);

// ISR
void timer_isr(void) {
    uint32_t interrupts = Cy_TCPWM_GetInterruptStatusMasked(MY_TIMER_HW, MY_TIMER_NUM);
    Cy_TCPWM_ClearInterrupt(MY_TIMER_HW, MY_TIMER_NUM, interrupts);
    if (CY_TCPWM_INT_ON_TC & interrupts) {
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
    }
}
```

**Math:** `period = (f_counter_clock / f_desired) - 1`
- 1 ms tick at 1 MHz: period = 999
- 1 s tick at 10 kHz: period = 9999
- 100 µs at 10 MHz: period = 999

### CC0 match interrupt (polled approach)

When ISR overhead is unwanted, poll the interrupt status in the main loop. Used in CE240776.

```c
// DC setup: Compare mode, CC0 match value set, CC0 interrupt enabled
Cy_TCPWM_Counter_Init(COUNTER_HW, COUNTER_NUM, &COUNTER_config);
Cy_TCPWM_Counter_Enable(COUNTER_HW, COUNTER_NUM);
// Counter starts from external trigger (Start Input configured in DC)

// Main loop polling
for (;;) {
    uint32_t intrMask = Cy_TCPWM_GetInterruptStatusMasked(COUNTER_HW, COUNTER_NUM);
    if (intrMask == CY_TCPWM_INT_ON_CC0) {
        Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
        Cy_TCPWM_ClearInterrupt(COUNTER_HW, COUNTER_NUM, CY_TCPWM_INT_ON_CC0);
    }
}
```

### One-shot delay (software-triggered)

Fire once, generate TC interrupt, then stop. Useful for debounce, protocol timeouts.

```c
// DC setup: One-Shot mode, period = delay_clocks - 1, TC interrupt enabled
Cy_TCPWM_Counter_Init(COUNTER_HW, COUNTER_NUM, &COUNTER_config);
Cy_TCPWM_Counter_Enable(COUNTER_HW, COUNTER_NUM);
Cy_TCPWM_TriggerStart_Single(COUNTER_HW, COUNTER_NUM);
// Counter runs to TC, stops, fires interrupt

// To re-arm after completion:
Cy_TCPWM_TriggerReloadOrIndex_Single(COUNTER_HW, COUNTER_NUM);
Cy_TCPWM_TriggerStart_Single(COUNTER_HW, COUNTER_NUM);
```

### Event counting (external pulses)

Count button presses, encoder ticks, sensor pulses, or flow meter events. The Count Input is
an external GPIO pin configured in Device Configurator.

```c
// DC setup: Count Input = pin trigger (rising edge), period = 0xFFFFFFFF (max),
//           Count Up, Continuous, Compare mode
Cy_TCPWM_Counter_Init(EVENT_COUNTER_HW, EVENT_COUNTER_NUM, &EVENT_COUNTER_config);
Cy_TCPWM_Enable_Multiple(EVENT_COUNTER_HW, EVENT_COUNTER_MASK);
Cy_TCPWM_TriggerStart(EVENT_COUNTER_HW, EVENT_COUNTER_MASK);

// Read count at any time
uint32_t count = Cy_TCPWM_Counter_GetCounter(EVENT_COUNTER_HW, EVENT_COUNTER_NUM);

// Reset count (for periodic rate measurement)
Cy_TCPWM_Counter_SetCounter(EVENT_COUNTER_HW, EVENT_COUNTER_NUM, 0);
```

### PWM-triggered counter (hardware start)

Counter auto-starts from another TCPWM's output trigger. Configure in Device Configurator:
- PWM: Trigger 0 Event = "PWM (line out)", Trigger 0 Signal = target counter's start input
- Counter: Start Input = connected trigger, Start Input Mode = Rising Edge

```c
// Initialize PWM first (it produces the start trigger)
Cy_TCPWM_PWM_Init(PWM_HW, PWM_NUM, &PWM_config);
Cy_TCPWM_PWM_Enable(PWM_HW, PWM_NUM);
Cy_TCPWM_TriggerReloadOrIndex_Single(PWM_HW, PWM_NUM);

// Initialize counter (will auto-start from PWM trigger)
Cy_TCPWM_Counter_Init(COUNTER_HW, COUNTER_NUM, &COUNTER_config);
Cy_TCPWM_Counter_Enable(COUNTER_HW, COUNTER_NUM);
// No software TriggerStart needed — PWM signal starts it
```

### Input capture (pulse width / period measurement)

Measure elapsed time between edges. Counter free-runs; capture latches current count.

```c
// DC setup: Capture mode, Continuous, Capture 0 Input = pin (rising edge)
// For pulse width: configure Capture0 = Rising, Capture1 = Falling (v2+)
Cy_TCPWM_Counter_Init(COUNTER_HW, COUNTER_NUM, &COUNTER_config);
Cy_TCPWM_Counter_Enable(COUNTER_HW, COUNTER_NUM);
Cy_TCPWM_TriggerStart_Single(COUNTER_HW, COUNTER_NUM);

// In CC0 interrupt — read captured values:
uint32_t cap0 = Cy_TCPWM_Counter_GetCapture0Val(COUNTER_HW, COUNTER_NUM);
// v2+: uint32_t cap1 = Cy_TCPWM_Counter_GetCapture1Val(COUNTER_HW, COUNTER_NUM);
```

For period measurement (same-edge to same-edge):
```c
static uint32_t prev_capture = 0;
uint32_t curr = Cy_TCPWM_Counter_GetCapture0Val(COUNTER_HW, COUNTER_NUM);
uint32_t period_clocks = curr - prev_capture;  // handles 32-bit wrap
prev_capture = curr;
float freq_hz = (float)counter_clock_hz / period_clocks;
```

### Capture clock conversion — use DC-reported frequency

⚠️ **Do NOT manually calculate the capture counter clock.** The clock chain includes peri group
dividers that are not visible from `cycfg_clocks.h`. Always use the frequency displayed in
the Device Configurator UI for the counter's assigned clock divider.

```c
// CORRECT: Use DC-reported frequency as a #define
#define CAPTURE_CLOCK_HZ    250000UL  // From Device Configurator: 100 MHz / divider 400 = 250 kHz

uint32_t high_counts = capture_rising - capture_falling;
uint32_t high_time_us = (high_counts * 1000000UL) / CAPTURE_CLOCK_HZ;
```

See [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §2 for details on hidden intermediate dividers.

### Synchronized multi-counter start

#### TCPWM v1 (PSOC 6, PSOC 4)

Start multiple counters simultaneously via bitmask trigger:

```c
Cy_TCPWM_Counter_Enable(HW, COUNTER1_NUM);
Cy_TCPWM_Counter_Enable(HW, COUNTER2_NUM);

uint32_t mask = COUNTER1_MASK | COUNTER2_MASK;
Cy_TCPWM_TriggerStart(HW, mask);
```

> ⚠️ **TCPWM v1 only.** On v2 devices (PSE84, PSOC Control, XMC7), each counter has its own
> `TR_CMD` register. The bitmask API exists but does not guarantee atomic simultaneous start.

#### TCPWM v2 (PSE84, PSOC Control, XMC7)

Use back-to-back `TriggerStart_Single` calls with interrupts disabled to minimize skew:

```c
Cy_TCPWM_Counter_Enable(HW, COUNTER1_NUM);
Cy_TCPWM_Counter_Enable(HW, COUNTER2_NUM);

__disable_irq();
Cy_TCPWM_TriggerStart_Single(HW, COUNTER1_NUM);
Cy_TCPWM_TriggerStart_Single(HW, COUNTER2_NUM);
__enable_irq();
// Skew: < 1 µs (a few clock cycles between calls)
```

### Glitch-free compare update (runtime threshold change)

Use buffered compare to swap at next TC (avoids mid-count glitch):

```c
Cy_TCPWM_Counter_SetCompare0BufVal(HW, NUM, new_threshold);
Cy_TCPWM_Counter_EnableCompare0Swap(HW, NUM, true);
// Hardware swaps active/buffer at TC
```

### Cascading counters for extended range

When 16-bit width is insufficient:
1. Counter A: period = 0xFFFF, Trigger 0 Event = TC
2. Counter B: Count Input = Counter A's trigger output (rising edge)
3. Total range: 65536 × (Counter B period + 1) clocks

## Quadrature Decoder Patterns

### Basic encoder with X4 resolution

```c
// DC setup: quaddec personality, Resolution = X4, phiA/phiB pins assigned
Cy_TCPWM_QuadDec_Init(ENCODER_HW, ENCODER_NUM, &ENCODER_config);
Cy_TCPWM_QuadDec_Enable(ENCODER_HW, ENCODER_NUM);
Cy_TCPWM_TriggerStart_Single(ENCODER_HW, ENCODER_NUM);

// Read position (signed via GetCounter — counter counts up/down)
int32_t position = (int32_t)Cy_TCPWM_QuadDec_GetCounter(ENCODER_HW, ENCODER_NUM);

// Reset on index pulse (configure Index Input in DC)
// Or reset in software:
Cy_TCPWM_QuadDec_SetCounter(ENCODER_HW, ENCODER_NUM, 0);
```

**Resolution modes:**
- X1: one count per full quadrature cycle (low resolution)
- X2: two counts per cycle (medium)
- X4: four counts per cycle (maximum resolution, most common)
- UP_DOWN_ROTARY_COUNT: special rotary knob mode (v2+ only)

## Gotchas

### Initial start does NOT require TriggerReloadOrIndex

After `Counter_Init` + `Counter_Enable`, the counter is at 0 with period loaded from config.
`TriggerStart_Single` is sufficient for first start:

```c
// CORRECT — initial start:
Cy_TCPWM_Counter_Enable(HW, NUM);
Cy_TCPWM_TriggerStart_Single(HW, NUM);  // starts from 0, period already loaded

// TriggerReloadOrIndex is needed ONLY for RE-starting:
// - After one-shot completes (counter is stopped at TC)
// - After TriggerStopOrKill (counter holds last value)
// - To reset mid-run to a known state
```

### FreeRTOS BASEPRI may mask TCPWM interrupts

⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent TC/CC ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### 16-bit vs 32-bit counter groups — verify before assuming range

TCPWM blocks may organize counters into **groups with different bit widths** (e.g., one
group of 32-bit counters and another of 16-bit counters). A 16-bit counter at 1 MHz
overflows every 65.5 ms — using one for a long timeout will silently wrap.

**How to determine counter bit width:**
1. **Device Configurator** — select the counter instance; the personality shows the counter
   width (16 or 32 bit) in the instance properties
2. **Device datasheet** — the TCPWM chapter lists groups and their widths
3. **Generated define** — check if `MY_COUNTER_NUM` maps to a group with reduced width

**How to find BSP pre-assigned TCPWM instances:**
See [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §5 for the general discovery procedure — search
`cybsp.h` for `CYBSP_*` defines that map to TCPWM counters. Repurposing a BSP-assigned
counter requires verifying no library depends on the original configuration.

### Capture buffer depth — one value without ISR

Capture0 + Capture0Buffer = 2 values maximum. If captures arrive faster than ISR drains them, earlier values are lost. For high-frequency signals, use DMA to drain captures into a memory buffer.

### One-shot does not auto-reload

After a one-shot reaches TC, it **stops and holds**. You must explicitly:
1. `TriggerReloadOrIndex_Single` — resets counter to 0, reloads period
2. `TriggerStart_Single` — starts counting again

### Interrupt sources (bitmask)

Multiple interrupt sources can fire simultaneously. Always mask-check:

```c
uint32_t intr = Cy_TCPWM_GetInterruptStatusMasked(HW, NUM);
if (CY_TCPWM_INT_ON_TC & intr)  { /* terminal count */ }
if (CY_TCPWM_INT_ON_CC0 & intr) { /* compare/capture 0 match */ }
if (CY_TCPWM_INT_ON_CC1 & intr) { /* compare/capture 1 match (v2+) */ }
Cy_TCPWM_ClearInterrupt(HW, NUM, intr);  // clear ALL that fired
```

### Pin routing — Counter mode usually has no output pin

Unlike PWM, a counter in timer/capture mode typically does not drive a pin. If you need the TC signal routed externally (e.g., to trigger DMA or another peripheral), configure Trigger Output in Device Configurator. This creates an internal trigger connection, not a GPIO output.

### Clock divider — same G6 rule applies

Device Configurator GUI accepts the actual division ratio (1-based). DC subtracts 1 internally for the register. Present `source_clock / target_freq` — do NOT subtract 1.

### TCPWM version differences (verify via personality file — see [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §1)

| Feature | v1 | v2+ |
|---|---|---|
| Capture 1 / Compare 1 | ❌ | ✅ |
| `_MASK` define generated | ❌ | ✅ |
| Glitch filter on inputs | ❌ | ✅ |
| Synchronized bitmask start | ✅ atomic | ⚠️ per-counter TR_CMD |

Determine your version from the personality filename (e.g., `counter2.0.cypersonality` = v2).

### `Enable_Multiple` vs `Counter_Enable`

- `Cy_TCPWM_Counter_Enable(HW, NUM)` — enables a single counter by instance number
- `Cy_TCPWM_Enable_Multiple(HW, MASK)` — enables multiple counters atomically via bitmask (v2+ generates `_MASK`)

Use `Enable_Multiple` + `TriggerStart` (bitmask version) when synchronizing multiple counters on v1 devices.

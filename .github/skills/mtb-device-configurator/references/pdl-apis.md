# PDL Peripheral API Documentation

Peripheral documentation is provided directly by the source API definitions in the header files of the PDL library.

## Principle

**The PDL headers ARE the documentation.** They contain:
- Exact function signatures (correct for this device family)
- Doxygen `\param` descriptions explaining each argument
- `\note` blocks with gotchas and constraints
- `\funcusage` + `\snippet` references showing usage patterns
- Inline implementations revealing register-level behavior

## How to Find PDL Headers

PDL headers are in the project's shared library path:

```
<workspace>/mtb_shared/mtb-pdl-<cat>/release-v*/drivers/include/
```

Where `<cat>` matches the device:
- `cat2` → PSOC 4 (Cortex-M0+)
- `cat1a` → PSOC 6 (Cortex-M4/M0+)
- `cat1d` → PSOC Edge (Cortex-M55/M33)
- `cat1c` → PSOC Control C3 (Cortex-M33) and XMC7xxx (Cortex M4/M7)

## Peripheral-to-Header Mapping

Naming between peripheral and the matching PDL driver header generally matches the peripheral name plus a prefix of `cy_` (or `mtb_` for more recently written PDLs). Examples:

| Peripheral | Primary Header | Supporting Headers |
|-----------|---------------|-------------------|
| PWM | `cy_tcpwm_pwm.h` | `cy_tcpwm.h` (TriggerStart, Enable_Multiple) |
| Timer | `cy_tcpwm_counter.h` | `cy_tcpwm.h` |
| GPIO | `cy_gpio.h` | — |
| GPIO Interrupt | `cy_gpio.h` + `cy_sysint.h` | — |
| UART | `cy_scb_uart.h` | `cy_scb_common.h` |
| I2C | `cy_scb_i2c.h` | `cy_scb_common.h` |
| SPI | `cy_scb_spi.h` | `cy_scb_common.h` |
| ADC (SAR) | `cy_sar.h` | — |
| DMA | `cy_dmac.h` or `cy_dma.h` | — |
| Clocks | `cy_sysclk.h` | — |

## Implementation Workflow

### Step 1: Identify Generated Macros

After Device Configurator closes, read `GeneratedSource/cycfg_peripherals.h`:
- Find `*_HW` macros (base pointer)
- Find `*_NUM` macros (counter number)
- Find `*_MASK` macros (bitmask — used by some APIs like `TriggerStart`)
- Find `*_config` structs (pre-filled configuration)

Keep these in working context going forward (do not forget these during compaction).

### Step 2: Read the Relevant PDL Header

Open the header file and look for:
1. **Init function** — usually `Cy_<PERIPH>_<MODE>_Init(base, num, &config)`
2. **Enable function** — e.g., `Cy_TCPWM_PWM_Enable(base, num)`
3. **Start/trigger function** — check parameter types carefully:
   - Does it take a `num` (counter number) or `counters` (bitmask)?
   - Read the `\param` description — it will say "counter instance number" vs "bit field"
4. **Runtime control functions** — SetCompare, SetPeriod, etc.

### Step 3: Check for Device-Family Differences

Pay attention to:
- Function name suffixes: `_Single` variants exist only on some device families
- Parameter semantics: same-named function may take `num` on one family, `mask` on another
- Available APIs: not all functions exist on all families (e.g., `Cy_GPIO_SetInterruptMask` exists on cat1d but not cat2)

**If a function from a skill or example doesn't exist in the header, DO NOT USE IT.**
Search the header for the closest match.

### Step 4: Check Notes and Gotchas

Search the header for `\note` blocks near the functions you plan to use:
```
grep -A3 "\\\\note" cy_tcpwm_pwm.h
```

These contain critical warnings about:
- Pin drive mode requirements
- Clock dependencies
- Sequencing requirements (enable before start, etc.)
- Buffered vs immediate updates

## What This Approach Does NOT Cover

- **Device Configurator GUI navigation** — that's in the Device Configurator skill's `user-instruction-template.md`
- **Pin routing verification** — that's in `pin-assignment.md`
- **Post-DC verification** — that's in `post-close-verification.md`
- **Clock calculation formulas** — derive from the PDL header comments + Device Configurator-generated clock values

## Anti-Patterns

- ❌ Do NOT copy code from examples targeting a different device family
- ❌ Do NOT assume function names based on pattern-matching from other families
- ❌ Do NOT use functions not present in the actual PDL header for this project
- ✅ DO read the header file directly when uncertain
- ✅ DO check `\param` descriptions for bitmask vs number semantics
- ✅ DO verify function existence with grep before using it

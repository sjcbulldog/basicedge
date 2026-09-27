---
name: mtb-device-configurator
description: "Configure device peripherals, clocks and pins in a ModusToolbox project through the Device Configurator GUI. Use it when asked to set up or enable a peripheral such as UART, I2C, SPI, PWM or ADC before writing driver code, or when a library-tier skill needs a hardware peripheral enabled first. Inspects GeneratedSource/ before prompting to avoid unnecessary Device Configurator sessions."
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Device Configurator

> ⚠️ **Device Configurator is a GUI-only tool.** The agent CANNOT programmatically edit `design.modus` files — they are binary/proprietary format. The agent's role is to **instruct the user** with specific settings, then wait for the user to make changes manually in the GUI.

> ⛔ **CRITICAL — `vscode/askQuestions` tool invocation is MANDATORY.** When presenting DC instructions, you MUST invoke `vscode/askQuestions` (the actual tool, not plain text). If your response contains DC instructions as numbered options or markdown text instead of a tool invocation, you have violated this skill. See Gate G5 and Workflow Step 4.

## Quick Reference

| Item | Value |
|------|-------|
| **Purpose** | Configure MCU peripherals, clocks, pins via GUI → generates `GeneratedSource/` |
| **Config file** | `bsps/TARGET_<bsp>/config/design.modus` |
| **Output** | `GeneratedSource/cycfg_*.c/h` — config structs, NOT init calls |
| **Key rule** | DC settings are truth — never override in code |
| **Workflow** | Pre-check → Present instructions → User configures → Verify → Code |

## ⛔ Gates (Read First)

| # | Gate | Violation |
|---|------|-----------|
| G0 | **GUI-only tool** — agent CANNOT edit design.modus; must instruct user with settings tables | Attempting to create/edit .modus files |
| G1 | **Device Configurator before code** — configure peripherals in Device Configurator BEFORE writing init code | Writing `Cy_*_Init()` before Device Configurator setup |
| G2 | **Present full instructions** — prepare navigation path + settings table BEFORE the user begins configuring. Use the two-message pattern: brief summary + ask → detailed instructions after DC opens | "Open Device Configurator and configure PWM" with no details |
| G3 | **Verify after Device Configurator** — read `GeneratedSource/` and confirm values match BEFORE writing app code | Assuming Device Configurator was done correctly |
| G4 | **Never override in code** — if Device Configurator values are wrong, reopen Device Configurator; don't code around them | Using `Cy_SysClk_PeriPclkSetDivider()` to fix clock |
| G5 | **⛔ MANDATORY TOOL INVOCATION** — invoke the `vscode/askQuestions` tool (not plain text) to offer opening Device Configurator, then STOP until user responds | Outputting the question as text instead of invoking the tool |
| G6 | **Clock divider = division ratio (1-based)** — Device Configurator GUI accepts the actual division ratio. DC subtracts 1 internally for the register. Present `source_clock / target_freq` — do NOT subtract 1 | Telling user to enter 99 instead of 100, or 1999 instead of 2000 |
| G7 | **⛔ Read references BEFORE writing code** — list the `references/` folder and read ALL applicable files for your peripheral type BEFORE writing init or ISR code | Writing ISR code without reading `freertos-interrupt-priority.md` or `pre-coding-verification.md` |

### Gate G7 — Mandatory Reference Exploration

Before writing ANY application code that uses a configured peripheral:

1. **List** the `references/` subfolder of this skill
2. **Identify** which files apply to your peripheral type (timer → `timer-counter-patterns.md`, PWM → `pwm-runtime-patterns.md`, etc.)
3. **Always read** these files regardless of peripheral type:
   - `device-configurator-gotchas.md` — universal gotchas (all devices)
   - `pre-coding-verification.md` — mandatory checklist
   - `freertos-interrupt-priority.md` — if project uses FreeRTOS
   - `pse84-gotchas.md` — if target is PSE84/PSOC Edge E84
4. **Extract and document** key values (clock frequency, priority bits, BASEPRI threshold) before proceeding

**Failure to complete G7 is a gate violation** — equivalent to G1 (writing code before Device Configurator).

### Forbidden Functions (G4 Detail)

Never use these — they override Device Configurator settings:
- `Cy_SysClk_PeriPclkSetDivider()` / `DisableDivider()` / `EnableDivider()` / `AssignDivider()`
- `Cy_GPIO_SetHSIOM()` — fix pin routing in DC instead
- `Cy_GPIO_Pin_Init()` with manual config struct — use `cybsp_init()`
- Manual `cy_stc_*_config_t` struct initialization — use Device Configurator-generated config

## Workflow (Step-by-Step)

1. **Pre-check** — Read `GeneratedSource/cycfg_peripherals.h`; if already configured correctly, skip Device Configurator
2. **Resolve pins** — Check `cybsp.h` and `gpio_*.h` HSIOM enum ([pin-assignment.md](./references/pin-assignment.md))
3. **Discover settings** — Read the personality XML to determine what GUI fields exist and their valid values ([personality-lookup.md](./references/personality-lookup.md)). Only suggest settings that appear as `<Member>` elements in the personality — do NOT invent field names.
4. **Present brief summary + ASK** — Output a one-line summary of what needs configuring, then immediately invoke the tool:

**⛔ MANDATORY TOOL INVOCATION — do NOT output the question as plain text:**

```
vscode/askQuestions:
  question: "Device Configurator settings are ready. Would you like me to open Device Configurator now?"
  options:
    - "Yes — open Device Configurator"
    - "I'll open it myself"
    - "Skip — already configured"
```

If user selects "Yes": call `run_tool("device-configurator")` to launch Device Configurator.
If user selects "I'll open it myself": wait for user to confirm when ready.
If user selects "Skip": proceed to verification step.

5. **Present detailed instructions** — AFTER Device Configurator opens (or user is ready), present the full navigation path + settings table ([user-instruction-template.md](./references/user-instruction-template.md)). This ensures the detailed table is visible while the user works in Device Configurator.

6. **Verify** — Read `cycfg_*.h` files, confirm settings match ([post-close-verification.md](./references/post-close-verification.md))
7. **Pre-coding check** — Complete checklist before writing code ([pre-coding-verification.md](./references/pre-coding-verification.md))

**Full procedures:** [Standalone workflow](./references/workflow-standalone.md) | [Sub-agent invocation](./references/workflow-subagent.md)

## How to Run Device Configurator

> ⚠️ **MUST run from the project directory** — the script resolves `design.modus`
> relative to CWD. It will fail silently or error if run from any other directory.

**Launch procedure:**
1. Ensure terminal CWD is the project root (where `Makefile` lives)
2. Run: `python .github/skills/mtb-tools/scripts/run_tool.py config`

**Fallback:** VS Code Command Palette → **ModusToolbox: Launch Device Configurator**

## Peripheral Init — PDL-Driven Approach

Device Configurator generates config structs only — app must call Init → Enable → Start.

**For ALL peripheral initialization and runtime code, read the actual PDL header files.**
See [pdl-apis.md](./references/pdl-apis.md) for the complete workflow.

| PDL Header | Peripheral | Patterns & Gotchas |
|-----------|-----------|-------------------|
| `cy_gpio.h` | GPIO pin control | [gpio-interrupt-patterns.md](./references/gpio-interrupt-patterns.md) |
| `cy_scb_uart.h` | UART communication | [uart-runtime-patterns.md](./references/uart-runtime-patterns.md) |
| `cy_scb_uart.h` | Multi-core debug output | [multi-core-debug-output.md](./references/multi-core-debug-output.md) |
| `cy_scb_i2c.h` | I2C master/slave | [i2c-bus-patterns.md](./references/i2c-bus-patterns.md) |
| `cy_scb_spi.h` | SPI communication | [spi-transfer-patterns.md](./references/spi-transfer-patterns.md) |
| `cy_tcpwm_pwm.h` | PWM generation | [pwm-runtime-patterns.md](./references/pwm-runtime-patterns.md) |
| `cy_tcpwm_counter.h` | Timer/Counter | [timer-counter-patterns.md](./references/timer-counter-patterns.md) |
| `cy_tcpwm_quaddec.h` | Quadrature decoder | [timer-counter-patterns.md](./references/timer-counter-patterns.md) |
| `cy_dma.h` | DMA (DW) transfers | [dma-patterns.md](./references/dma-patterns.md) |
| `cy_axidmac.h` | DMA (HPDMA) transfers | [dma-patterns.md](./references/dma-patterns.md) |
| `cy_trigmux.h` | Trigger routing | [dma-patterns.md](./references/dma-patterns.md) |
| `cy_syspm.h` | Power mode management | [power-mode-patterns.md](./references/power-mode-patterns.md) |
| `cy_syspm_pdcm.h` | Power dependency control | [power-mode-patterns.md](./references/power-mode-patterns.md) |
| `cy_syspm.h` | Memory power optimization | [memory-power-patterns.md](./references/memory-power-patterns.md) |
| `cy_sysint.h` | System interrupts | [gpio-interrupt-patterns.md](./references/gpio-interrupt-patterns.md) |
| `cy_i2s.h` / `cy_tdm.h` | Audio I2S/TDM | [audio-pdm-i2s-patterns.md](./references/audio-pdm-i2s-patterns.md) |
| `cy_pdm_pcm.h` | PDM microphone | [audio-pdm-i2s-patterns.md](./references/audio-pdm-i2s-patterns.md) |
| `cy_sd_host.h` | SD/SDIO host | [sdhc-card-lifecycle-patterns.md](./references/sdhc-card-lifecycle-patterns.md) |
| `cy_sar.h` / `cy_autanalog.h` | ADC / Autonomous Analog | [autanalog-sampling-patterns.md](./references/autanalog-sampling-patterns.md) |

> ⚠️ **Do NOT guess API names.** Verify every function exists in the actual PDL headers before using it.
> Pattern files contain use-case recipes and gotchas — NOT API signatures.

## Key Files

| File | Purpose |
|------|---------|
| `bsps/TARGET_<bsp>/config/design.modus` | Hardware config — DC GUI only |
| `GeneratedSource/cycfg_peripherals.h` | Peripheral aliases and config structs |
| `GeneratedSource/cycfg_pins.h` | Pin macros and HSIOM |
| `GeneratedSource/cycfg_routing.h` | Signal-to-pin connections |
| `GeneratedSource/cycfg_notices.h` | Compile-time config errors |

## Skill Reference Files

Peripheral pattern files are linked in the [PDL-Driven Approach](#peripheral-init--pdl-driven-approach) table above. These additional references support the Device Configurator workflow:

| Reference | Purpose |
|-----------|---------|
| [device-configurator-gotchas.md](./references/device-configurator-gotchas.md) | Universal gotchas: clock trees, pin mux, BSP repurposing, IP versioning |
| [pse84-gotchas.md](./references/pse84-gotchas.md) | PSOC Edge E84 device-specific gotchas (priority bits, D-Cache) |
| [pdl-apis.md](./references/pdl-apis.md) | PDL header-driven implementation workflow and peripheral-to-header mapping |
| [bsp-peripheral-map.md](./references/bsp-peripheral-map.md) | BSP peripheral availability and pin mapping per board |
| [freertos-interrupt-priority.md](./references/freertos-interrupt-priority.md) | BASEPRI masking, priority bit-width, and FreeRTOS interrupt interaction |
| [troubleshooting.md](./references/troubleshooting.md) | Common Device Configurator issues and resolutions |
| [workflow-standalone.md](./references/workflow-standalone.md) | DC workflow for standalone (non-subagent) usage |
| [workflow-subagent.md](./references/workflow-subagent.md) | DC workflow when invoked by another agent |
| [personality-lookup.md](./references/personality-lookup.md) | Personality types and parameter reference |
| [pin-assignment.md](./references/pin-assignment.md) | Pin assignment and HSIOM routing guidance |
| [user-instruction-template.md](./references/user-instruction-template.md) | Template for user-facing DC instructions |
| [pre-coding-verification.md](./references/pre-coding-verification.md) | Checklist before writing code against DC config |
| [post-close-verification.md](./references/post-close-verification.md) | Verification after closing Device Configurator |

## Self-Check (Before Responding)

Before sending ANY response that involves Device Configurator, verify:

- [ ] Did I invoke `vscode/askQuestions` (the tool, not plain text) to offer opening DC?
- [ ] Did I present the settings table AFTER the user responded to the tool prompt (two-message pattern)?
- [ ] For clock divider values: did I present the **1-based GUI value** (division ratio), not the 0-based register value?
- [ ] Did I check `cycfg_routing.h` for HSIOM conflicts on every GPIO pin my code controls?
- [ ] Did I use DC UI's reported frequency instead of manually computing from HF clock defines?

If any answer is "no" — fix it before responding.

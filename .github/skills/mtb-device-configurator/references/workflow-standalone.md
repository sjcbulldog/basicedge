# Workflow: Standalone Peripheral Configuration

Detailed step-by-step procedure for the standalone Device Configurator workflow.

---

## Step 1 — Identify the Peripheral

From the user's request, determine:

- **Peripheral type**: SCB UART, SCB I2C, SCB SPI, TCPWM (PWM/Counter), SAR ADC, GPIO, DMA, etc.
- **Specific requirements**: baud rate, clock frequency, resolution, interrupt-driven vs. polling, etc.
- **Target project/core**: In multi-core MCU projects, ask the user which core sub-project to target if it is not clear from context. Default to the NS application core for communication peripherals.

---

## Step 2 — Pre-check GeneratedSource

1. Navigate to `<project-root>/bsps/<bsp-name>/config/GeneratedSource/`
2. Open `cycfg_peripherals.h`
3. Search for the peripheral type (e.g., `TCPWM`, `SCB`, `SAR`)
4. **If the peripheral is already configured with the EXACT settings needed** (correct mode, clock, parameters):
   - Extract the alias macro (e.g., `#define DEBUG_UART_HW SCB2`)
   - Note the peripheral instance number and config struct name (e.g., `DEBUG_UART_config`)
   - Skip to Step 7 — no Device Configurator interaction is needed
5. **Otherwise:** proceed to Step 3 — even if an alias exists, the peripheral may need reconfiguration

> ⚠️ **Note:** Finding an alias in `cycfg_peripherals.h` does NOT mean the peripheral is correctly configured for your use case. BSP defaults often have peripherals aliased but with generic settings. Always verify the actual configuration matches your requirements before skipping Device Configurator.

---

## Step 3 — Resolve Pin Assignments

When settings require specific pin assignments (e.g., UART RX/TX, I2C SCL/SDA):

1. Open the BSP's `cybsp.h` file
2. Search for macros matching the peripheral type:
   - UART: `CYBSP_DEBUG_UART_RX`, `CYBSP_DEBUG_UART_TX`
   - I2C: `CYBSP_I2C_SCL`, `CYBSP_I2C_SDA`
   - SPI: `CYBSP_SPI_MOSI`, `CYBSP_SPI_MISO`, `CYBSP_SPI_CLK`, `CYBSP_SPI_CS`
3. **If macros found:** use them as the pin values in the settings table (e.g., `P6[5]` derived from the macro definition)
4. **If no macros:** check the EVK schematic or board documentation
5. **If still unknown:** note in the user instruction that pin assignment is board-specific and prompt the user to confirm the correct pins before proceeding

---

## Step 4 — Consult Personality (if settings are unclear)

If the correct personality name, version, or configuration parameters are not known, follow the
[Personality Lookup Algorithm](./personality-lookup.md) to determine:

- Personality filename and version (e.g., `uart3.0`)
- GUI navigation path (tab → category → peripheral instance)
- Available configuration parameters and their default values
- Generated symbol names (`ALIAS_HW`, `ALIAS_IRQ`, `ALIAS_config`, etc.)

---

## Step 5 — Brief Summary + Ask to Open Device Configurator

**Output ordering matters for UX.** Present a brief one-line summary of what needs configuring,
then immediately ask to open Device Configurator. The detailed settings table comes AFTER
the tool is open (Step 6) so the user can reference it while working.

1. Output a brief summary: *"Configure <PERIPHERAL> as <ALIAS> — <key settings in ≤15 words>."*
2. **⛔ MANDATORY: Invoke the `vscode/askQuestions` tool directly.** Do NOT output this question as plain text:

```yaml
vscode/askQuestions:
  question: "Device Configurator settings are ready. Would you like me to open Device Configurator now?"
  options:
    - "Yes — open Device Configurator"
    - "I'll open it myself"
    - "Skip — already configured"
```

Based on user response:
- **"Yes — open Device Configurator":** run from workspace root: `cd <project-path> && python .github/skills/mtb-tools/scripts/run_tool.py config`
  - If script fails: VS Code Command Palette → **ModusToolbox: Launch Device Configurator**
  - Monitor stderr for launch errors; stop and report if any occur
- **"I'll open it myself":** wait for user to confirm they are ready
- **"Skip — already configured":** proceed directly to Step 7

---

## Step 6 — Present Detailed Instructions

AFTER Device Configurator opens (or user confirms they are ready), present the full instruction
using the format in [user-instruction-template.md](./user-instruction-template.md):

- Peripheral name and instance number
- Alias to assign (use descriptive uppercase names: `DEBUG_UART`, `USER_I2C`, `MAIN_SPI`)
- Personality name and version
- Navigation path in the GUI
- Settings table (Section / Setting / Value) — include only settings that differ from defaults;
  state "leave all others as default"

**Multiple peripherals:** If more than one peripheral needs configuration, present all of them
together in a single instruction for one Device Configurator session. Group settings by peripheral.

Remind the user: *"Make sure you SAVE CHANGES before or upon closing Device Configurator, then let me know when you are done and I will proceed."*
Wait for the user to confirm they have completed the changes and closed Device Configurator

---

## Step 7 — Post-close Verification

After the user confirms they have completed the changes and closed Device Configurator:

1. Re-inspect `GeneratedSource/cycfg_peripherals.h` for the expected alias macro
   (e.g., `#define DEBUG_UART_HW SCB2`)
2. Run a build: scan output for errors originating from `cycfg_notices.h` — these indicate
   the user left configuration tasks unresolved
3. **If verification fails:**
   - Inform the user the configuration was not completed correctly
   - Identify specifically what is missing (alias absent, notices errors, etc.)
   - Offer to re-open Device Configurator
   - Do not proceed with code integration until verification passes
4. **If verification passes:** proceed to Step 8

---

## Step 8 — Application Integration Checks

1. Open `main.c` (or the application's primary startup file)
2. Verify `#include "cycfg.h"` is present near the top — add if absent
3. Verify `init_cycfg_all()` is called before any peripheral use in the startup sequence — add
   if absent (typically near the top of `main()`, before RTOS scheduler start if applicable)
4. Note the generated alias symbols available for use (e.g., `DEBUG_UART_HW`,
   `DEBUG_UART_config`, `DEBUG_UART_IRQ`) — provide these to the library or application code
   integration step
5. **Look up the peripheral runtime pattern** — consult the appropriate `*-patterns.md`
   reference from `../SKILL.md` (Peripheral Runtime Patterns table) to determine:
   - Which PDL init function to call with the Device Configurator-generated config struct
   - Which Enable / Start calls are required (and commonly forgotten)
   - Known gotchas for that peripheral type
6. **Generate the init code** using the pattern from the reference, substituting the actual
   alias names from `GeneratedSource/`. Verify:
   - Context structs (where required) are declared at file scope or as `static` — never local
   - The complete Init → Enable → Start sequence is present (no missing steps)
   - ISR registration is included if using interrupt-driven APIs (see [gpio-interrupt-patterns.md](gpio-interrupt-patterns.md))

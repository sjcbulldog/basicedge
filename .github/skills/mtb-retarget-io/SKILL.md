---
name: mtb-retarget-io
description: Integrate the retarget-io library into a ModusToolbox project to enable printf(), scanf(), and STDIO output over UART. Use when asked to "enable printf", "add serial debug output", "set up stdio over UART", "add retarget-io", or "get printf working". Configures the debug UART via Device Configurator, installs the library, and generates all initialization code for bare-metal and RTOS environments.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# retarget-io

Integrate the Infineon `retarget-io` library to redirect `printf()`, `scanf()`, and all standard I/O to a UART serial interface. Supports three HAL configurations (MTB-HAL, CY-HAL, PDL-only) and two environments (bare-metal and RTOS).

This is a **library-tier skill**. It orchestrates:
1. Library installation via the `mtb-library-installer` skill (if not already present)
2. UART peripheral configuration via the `mtb-device-configurator` skill (MTB-HAL and PDL-only paths)
3. All code changes needed to initialize and use `retarget-io`

## When to Use This Skill

- User says: "enable printf", "add serial debug output", "set up stdio over UART", "add retarget-io", "get printf working over UART", "redirect stdout to UART"
- A library-installer sub-agent has installed `retarget-io` and dispatched this skill for code integration
- User requests `scanf()` or STDIN input from a UART terminal

### When Not to Use This Skill

- `retarget-io` is already integrated and working in the project
- RTT mode (`COMPONENT_RETARGET_IO_RTT`) is already enabled — UART is not needed for stdio
- The project's only available UART is already in use for another purpose and cannot be shared
- The user wants to configure retarget-io in **more than one sub-project** simultaneously — sharing a physical UART across cores without IPC causes output corruption; configure one sub-project at a time

## Prerequisites

- A ModusToolbox project with a valid `Makefile` at the project root
- `make` available in the project
- This skill is pre-installed at `.github/skills/retarget-io/` by the Infineon ModusToolbox AI Assistant VS Code extension
- Determination of the project's active configurations.  If any of this information is not present in the copilot-instructions.md file, ask the user for this information and offer to update:
    - HAL configuration: MTB_HAL, CY_HAL, or PDL-only
    - Bare-metal or RTOS
* Within a multi-project MTB workspace (i.e., multi-core or multi-security-profile workspace), the project and specific core+security profile that the library is to be added to must be provided.  If not provided by the user in the prompt, query the user.
* Like all peripherals in a multi-project MTB workspace, the UART (SCB) peripheral is bound to a specific project (core+security profile) by configuring that peripheral in the design.modus file of a particular project.  The UART selected must be initialized for retarget-io by only the project indicated by the user.  They must be used only within the core that it belongs to.  To use them across cores requires IPC mechanisms.

## Key Files

| File / Directory                                               | Purpose                                                              |
| -------------------------------------------------------------- | -------------------------------------------------------------------- |
| `deps/retarget-io.mtb`                                         | Library reference file — presence confirms installation              |
| `../mtb_shared/retarget-io/<version>/`                         | Library source fetched by `make getlibs`                             |
| `bsps/TARGET_<bsp>/config/design.modus`                        | Hardware configuration source — edited by Device Configurator only   |
| `bsps/TARGET_<bsp>/config/GeneratedSource/cycfg_peripherals.h` | Peripheral aliases; inspect before opening Device Configurator       |
| `bsps/TARGET_<bsp>/config/GeneratedSource/cycfg_notices.h`     | Compile-time errors for unresolved Device Configurator tasks         |
| `cybsp.h`                                                      | BSP pin macros (e.g., `CYBSP_DEBUG_UART_TX`, `CYBSP_DEBUG_UART_RX`)  |
| `Makefile`                                                     | `COMPONENTS`, `DEFINES`, and `LDFLAGS` entries managed by this skill |

## Environment Detection

Determine the HAL mode and RTOS environment before generating any code.

### HAL Mode

| Condition                        | HAL Mode     | Detection                                                           |
| -------------------------------- | ------------ | ------------------------------------------------------------------- |
| `COMPONENTS` contains `MTB_HAL`  | **MTB_HAL**  | Check `Makefile`; confirm `mtb_hal_` types in `cycfg_peripherals.h` |
| `COMPONENTS` contains `PSOC6HAL` | **CY_HAL**   | Check `Makefile` for `PSOC6HAL` component                           |
| CY_USING_HAL is defined          | **CY_HAL**   | Check `Makefile` `DEFINES` variable                                 |
| Neither component present        | **PDL-Only** | No HAL component in `COMPONENTS`                                    |

If detection is ambiguous, ask the user which HAL configuration their project uses before proceeding.

### RTOS Environment

| Condition                                                  | Environment    | Detection                               |
| ---------------------------------------------------------- | -------------- | --------------------------------------- |
| `COMPONENTS` contains `FREERTOS`, `THREADX`, or equivalent | **RTOS**       | Check `Makefile` or `deps/freertos.mtb` |
| No RTOS component present                                  | **Bare-metal** | No RTOS component in `COMPONENTS`       |

## Step-by-Step Workflow

Execute these steps in order. See [workflow.md](./references/workflow.md) for initialization code for each path.

> **Multi-core projects:** Confirm the single target sub-project before starting. The `retarget-io` library and debug UART must be configured in exactly **one** sub-project — configuring retarget-io in multiple sub-projects sharing the same physical UART causes output corruption. Default to the NS application core (CM33_NS, CM55, etc.) unless the user specifies otherwise. For patterns on how the non-owner core can still produce debug output, see [multi-core-debug-output.md](../mtb-device-configurator/references/multi-core-debug-output.md).

### Step 0 — Upfront User Queries

Ask the user these two questions **before making any changes**. Apply the answers automatically during Step 4.

**Query 1 — Line ending conversion:**
> "Should I automatically convert `\n` to `\r\n` in `printf()` output? This is recommended for most UART terminal emulators (PuTTY, TeraTerm, etc.). [Yes / No]"

- **Yes** → add `DEFINES += CY_RETARGET_IO_CONVERT_LF_TO_CRLF` to the Makefile in Step 4
- **No** → the user must use `\r\n` in all `printf()` format strings

**Query 2 — Floating-point printf support:**
> "Do you need `printf()` to format floating-point values (`%f`, `%e`, `%g`)? [Yes / No]"

- **Yes** → float is enabled by default; no `DEFINES` change needed. If `TOOLCHAIN` in the Makefile is `GCC_ARM`, also add `LDFLAGS += -u _printf_float` in Step 4 — this ensures float works correctly with GCC_ARM's newlib-nano and is harmless on standard newlib.
- **No** → add `DEFINES += CY_RETARGET_IO_NO_FLOAT` in Step 4 to save flash; `%f`/`%e`/`%g` will produce no output.

### Step 1 — Verify Library Installation

Check whether `deps/retarget-io.mtb` exists in the target sub-project. If it does not exist, invoke the `mtb-library-installer` skill with library identifier `retarget-io`. Return here after installation completes.

### Step 2 — Detect Environment

Run these checks to detect HAL mode, RTOS environment, and TOOLCHAIN without manually scanning build files:

1. **HAL mode** — Search for `COMPONENTS` assignments in the project `Makefile` (look for lines starting with `COMPONENTS`). If `MTB_HAL` is not found there, check the Device Support Library: look for `mtb-dsl-*.mtb` in `libs/`, then search for `COMPONENTS+=.*MTB_HAL` in `../mtb_shared/mtb-dsl-*/*/library.mk`.
2. **RTOS** — Search the project `Makefile` and `deps/*.mtb` for `FREERTOS`, `THREADX`, or `RTOS`.
3. **TOOLCHAIN** — Search for a line starting with `TOOLCHAIN` in the project `Makefile`.

Apply [Environment Detection](#environment-detection) rules to the output. If `MTB_HAL` is found in either the project Makefile or the DSL's `library.mk`, the project uses **MTB-HAL** mode. If the result is still ambiguous, ask the user.

### Step 3 — Configure UART (MTB-HAL and PDL-Only only; skip entirely for CY-HAL)

Check **one place only** for an existing debug UART alias — search for `DEBUG_UART_HW` or `_UART_HW` in `bsps/*/config/GeneratedSource/cycfg_peripherals.h`.

- **Found** → extract the alias (e.g., `DEBUG_UART_HW`) and proceed to Step 4
- **Not found** → the UART is not yet configured. Do **not** search elsewhere in the project or in any other project in the workspace. Proceed immediately to the steps below.

Look for existing UART/SCB info in the project's `design.modus` only — this is the single permitted source for pin hints. Search for `uart` or `SCB` in `bsps/*/config/design.modus`, filtering results for pin/rx/tx/port references.

- If the grep returns useful SCB and pin information, use it to populate the Device Configurator instructions table in [UART Configuration](#uart-configuration-device-configurator).
- If it returns nothing useful, proceed with placeholder values and tell the user to identify the correct SCB and pins themselves (see the `<ask user>` fallback in the UART Configuration section).

**Do not search** `cybsp.h`, `.modustoolbox/docs/`, other sub-projects, or any path outside the current project directory for pin assignments.

**Delegation (required):** Invoke the **`mtb-device-configurator` skill as a sub-agent** — do not generate Device Configurator instructions inline. Pass it the peripheral type, required alias, personality name (`uart-3.0`), and settings from [UART Configuration (Device Configurator)](#uart-configuration-device-configurator). When the sub-agent returns, read its `DEVICE_CONFIGURATOR_RESULT` block to obtain the confirmed `hw_macro` and `config_struct` values, then return here to continue.

### Step 4 — Update Makefile

Add all `DEFINES`/`LDFLAGS` determined in Step 0 plus any environment-required entries, per [Makefile Integration](#makefile-integration). Edit only the Makefile of the **single target sub-project** identified at the start.

### Step 5 — Generate Initialization Code

Insert includes and `cy_retarget_io_init()` per [workflow.md](./references/workflow.md) for the detected HAL path and environment.

### Step 6 — Add Verification printf

Insert immediately after `cy_retarget_io_init()`:

```c
printf("retarget-io initialized\r\n");
```

### Step 7 — Build and Verify

Build the **target sub-project only** — do not trigger a full workspace build:

```
python .github/skills/mtb-tools/scripts/run_make.py --args build
```

After a successful build:

1. Offer to program the device:
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args program
   ```
2. **User action:** Open a serial terminal at **115200 baud** on the board's debug port (e.g., PuTTY or Tera Term on Windows, `minicom` or `screen` on Linux/macOS).
3. After reset/power-up, the verification `printf` should appear within one second. Confirm success per [Post-Install Verification](#post-install-verification).

## UART Configuration (Device Configurator)

> **Applies to:** MTB-HAL and PDL-Only paths only. CY-HAL manages UART resource acquisition internally — skip this section entirely for CY-HAL.

**Delegation (required):** Invoke the **`mtb-device-configurator` skill as a sub-agent** — pass it the peripheral type, required alias, personality name, and settings table below. Do not generate Device Configurator instructions inline; the `mtb-device-configurator` skill owns that workflow.

**Pin resolution (one source only):** Before invoking the sub-agent, search `bsps/*/config/design.modus` for `uart` or `SCB`, filtering for pin/rx/tx/port references.

Use any pin values found to populate the RX/TX rows. If the grep yields nothing useful, leave RX/TX as `<ask user>` and tell the user to identify the correct pins (KitProg3 UART, expansion header, etc.) when Device Configurator opens. **Do not search** anywhere else — not `cybsp.h`, not `.modustoolbox/docs/`, not other projects.

**Multi-project workspaces:** Open the `design.modus` of the **single target sub-project** only. Do not configure the debug UART in multiple sub-project design files.

Pass the following structured invocation block to the `mtb-device-configurator` sub-agent:

```
<!-- mtb-device-configurator sub-agent invocation -->
peripheral_type: SCB UART
personality: uart-3.0
alias: DEBUG_UART
settings:
  - section: General
    setting: Baud Rate (bps)
    value: 115200
  - section: General
    setting: Data Width
    value: 8 bits
  - section: General
    setting: Parity
    value: None
  - section: General
    setting: Stop Bits
    value: 1 bit
  - section: Flow Control
    setting: Enable Flow Control
    value: "False / unchecked"
  - section: Connections
    setting: Clock
    value: "16-bit divider 0 (or any available 16-bit divider; Device Configurator sets the correct divider value for 115200 baud automatically — accept any DRC autocorrection)"
  - section: Connections
    setting: RX
    value: <port/pin resolved from design.modus, or ask user>
  - section: Connections
    setting: TX
    value: <port/pin resolved from design.modus, or ask user>
  - section: API Mode
    setting: API Mode
    value: High Level
```

**Pin aliases (Pins tab — required):** Also instruct the user to set pin names in the Pins tab:
- RX pin Name: `DEBUG_UART_RX`
- TX pin Name: `DEBUG_UART_TX`

After the `mtb-device-configurator` sub-agent returns a `DEVICE_CONFIGURATOR_RESULT` with `status: success` or `status: already_configured`, confirm `DEBUG_UART_HW` is present by searching for it in `bsps/*/config/GeneratedSource/cycfg_peripherals.h`.

If found, proceed to Step 4. If not found, do not proceed — inform the user and offer to re-open Device Configurator.

## Makefile Integration

Apply these entries to the Makefile of the **single target sub-project** as determined by the Step 0 user queries and environment detection. Do not edit Makefiles of other sub-projects.

### Required (RTOS + GCC_ARM only)

```makefile
# Enable RTOS-aware thread safety in _write() for GCC_ARM/Newlib.
# ARM and IAR compilers handle this automatically — do not add for those compilers.
DEFINES += CY_RTOS_AWARE
```

Add `CY_RTOS_AWARE` when the project uses FreeRTOS, ThreadX, or another RTOS **and** the compiler is GCC_ARM.

### From Step 0 — Applied Automatically Based on User Answers

```makefile
# Add if user chose "Yes" to line ending conversion (Query 1):
DEFINES += CY_RETARGET_IO_CONVERT_LF_TO_CRLF

# Add if user chose "No" for float support (Query 2) — disables %f/%e/%g to save flash:
DEFINES += CY_RETARGET_IO_NO_FLOAT

# Add if user chose "Yes" for float AND TOOLCHAIN is GCC_ARM (Query 2):
LDFLAGS += -u _printf_float
```

Apply exactly the entries matching the user's Step 0 answers and detected TOOLCHAIN — no others.

## Configuration Options

These options are decided in **Step 0** and applied automatically during integration. For reference:

| Option | Makefile Entry | Effect |
|--------|---------------|--------|
| LF to CRLF conversion | `DEFINES += CY_RETARGET_IO_CONVERT_LF_TO_CRLF` | `\n` auto-becomes `\r\n`; recommended for PuTTY/TeraTerm |
| RTOS thread safety (GCC_ARM) | `DEFINES += CY_RTOS_AWARE` | Enables mutex in `_write()`; required for RTOS + GCC_ARM |
| Disable float in printf | `DEFINES += CY_RETARGET_IO_NO_FLOAT` | Reduces flash; `%f/%e/%g` produce no output |
| Enable float (newlib-nano) | `LDFLAGS += -u _printf_float` | Enables `%f/%e/%g` on GCC newlib-nano |

## Post-Install Verification

Build the **target sub-project only** — do not trigger a full workspace build:

```
python .github/skills/mtb-tools/scripts/run_make.py --args build
```

Then verify:

1. No errors or warnings related to retarget-io.
2. `DEBUG_UART_HW` present in `GeneratedSource/cycfg_peripherals.h` (MTB-HAL and PDL paths only).
3. `#include "cy_retarget_io.h"` present in all files that use `printf()`.
4. `cy_retarget_io_init()` called before any `printf()` (bare-metal: after `init_cycfg_all()`; RTOS: no `printf()` before the kernel starts).
5. Makefile `DEFINES` match the Step 0 user answers and the detected environment.

If the build fails, diagnose using the [Troubleshooting](#troubleshooting) table below.

> **Runtime verification:** Run `make program` to flash the device. Connect a serial terminal to the board's debug port at **115200 baud** (e.g., `minicom -D /dev/ttyACM0 -b 115200`). The verification `printf` should appear within one second of reset. If it does not, check pin assignments, baud rate, and buffer flushing.

## Troubleshooting

| Condition                                                                       | Action                                                                                                                                                                                            |
| ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `deps/retarget-io.mtb` not found, `../mtb_shared/retarget-io/` absent           | Invoke `mtb-library-installer` skill with identifier `retarget-io` before proceeding                                                                                                                  |
| `DEBUG_UART_HW` absent from `GeneratedSource/` (MTB-HAL / PDL paths)            | Do not proceed; instruct user to complete Device Configurator configuration                                                                                                                       |
| `CYBSP_DEBUG_UART_TX` / `CYBSP_DEBUG_UART_RX` not defined in `cybsp.h` (CY-HAL) | Check EVK schematic or ask user for KitProg3 debug UART pin assignments                                                                                                                           |
| `cy_retarget_io_init()` returns non-`CY_RSLT_SUCCESS`                           | Check: `Cy_SCB_UART_Enable()` called before init; clock divider configured; correct HW pointer                                                                                                    |
| **⛔ HardFault in `Cy_SCB_WriteTxFifo` from a FreeRTOS task (RTOS projects)**   | **The UART HAL object and context passed to `cy_retarget_io_init()` are stack-local in `main()`. After `vTaskStartScheduler()`, `main()`'s stack is reclaimed — making these dangling pointers. FIX: Declare as `static` or file-scope global. See [RTOS Initialization](./references/workflow.md#rtos-initialization-freertos-example).** |
| Device Configurator DRC error on clock divider                                  | Accept the DRC autocorrection — Device Configurator will set the correct divider value for 115200 baud                                                                                            |
| No terminal output after successful init                                        | Check: correct COM port/baud rate in terminal; `CY_RETARGET_IO_CONVERT_LF_TO_CRLF` missing; buffer not flushed (add `setvbuf`); RTOS: `printf()` must not be called before the RTOS kernel starts |
| `printf()` causes fault or deadlock in RTOS                                     | Check: `CY_RTOS_AWARE` added for GCC_ARM; `printf()` not called from ISR; kernel running before init call                                                                                         |
| Build error: `cy_retarget_io.h` not found                                       | Confirm `make getlibs` completed; verify `../mtb_shared/retarget-io/` exists                                                                                                                      |
| Float format not printing (`%f` outputs empty)                                  | Add `-u _printf_float` linker flag (newlib-nano); or verify `CY_RETARGET_IO_NO_FLOAT` is not set                                                                                                  |
| HAL mode detection ambiguous                                                    | Ask user which HAL configuration their project uses before proceeding                                                                                                                             |
| Garbled / corrupted UART output in multi-core project                           | retarget-io is configured in more than one sub-project sharing the same physical UART — remove it from all but the intended sub-project                                                           |
| Garbled output after FreeRTOS task starts (`BþBBBB...` pattern)                 | **⚠️ CRITICAL:** FreeRTOS tickless idle (`configUSE_TICKLESS_IDLE != 0`) enters Deep Sleep, which corrupts UART mid-transmission. **Register SysPm callback** — see [FreeRTOS Tickless + UART](#freertos-tickless--uart-critical) section below. |
| Build fails in a different sub-project after adding retarget-io                 | retarget-io.mtb should only be in the target sub-project's `deps/` — do not copy it to other sub-projects                                                                                         |

---

## FreeRTOS Tickless + UART (CRITICAL)

When FreeRTOS is used with `configUSE_TICKLESS_IDLE != 0`, Deep Sleep powers down the SCB clock before the UART TX FIFO drains. This causes garbled output (`BþBBBB...` pattern) after the first idle cycle.

**Detection:** During Step 2 (Detect Environment), if FreeRTOS is detected, search for `configUSE_TICKLESS_IDLE` in `FreeRTOSConfig.h`. If the value is 1 or 2, **WARN the user** and add the SysPm callback.

**Required Code — add the callback function and registration after retarget-io init:**

Define the callback function and variables (at file scope, outside `main()`), then register the callback inside `main()` right after the `cy_retarget_io_init()` call using `Cy_SysPm_RegisterCallback()`.

```c
#include "cy_syspm.h"

static cy_en_syspm_status_t uart_deep_sleep_callback(
    cy_stc_syspm_callback_params_t *params,
    cy_en_syspm_callback_mode_t mode)
{
    CySCB_Type *base = (CySCB_Type *)params->base;
    if (mode == CY_SYSPM_CHECK_READY) {
        if (!Cy_SCB_IsTxComplete(base)) {
            return CY_SYSPM_FAIL;  /* Block deep sleep */
        }
    }
    return CY_SYSPM_SUCCESS;
}

static cy_stc_syspm_callback_params_t uart_pm_params = {
    .base = DEBUG_UART_HW,  /* Or CYBSP_DEBUG_UART_HW */
    .context = NULL
};
static cy_stc_syspm_callback_t uart_pm_callback = {
    .callback = uart_deep_sleep_callback,
    .type = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &uart_pm_params,
    .order = 0U,
    .nextItm = NULL, .prevItm = NULL, .skipMode = 0U
};

/* Register after cy_retarget_io_init() */
Cy_SysPm_RegisterCallback(&uart_pm_callback);
```

**Development Workflow:** If FreeRTOS + tickless idle detected, always add this callback when integrating retarget-io.

See also: [mtb-device-configurator/uart-runtime-patterns.md](../mtb-device-configurator/references/uart-runtime-patterns.md) — UART deep sleep protection

## References

- [Integration Workflow — All HAL Paths](./references/workflow.md)
- [retarget-io GitHub Repository](https://github.com/Infineon/retarget-io)
- [retarget-io API Reference](https://infineon.github.io/retarget-io/html/index.html)
- [Library Installer Skill](../mtb-library-installer/SKILL.md)
- [Device Configurator Skill](../mtb-device-configurator/SKILL.md)

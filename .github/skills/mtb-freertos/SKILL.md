---
name: mtb-freertos
description: Install and configure the Infineon FreeRTOS library in a ModusToolbox project, generate a tuned FreeRTOSConfig.h based on the user's application, and integrate a running example task. Use when asked to "add FreeRTOS", "add an RTOS", "set up tasks", "configure FreeRTOS", "fix my FreeRTOSConfig.h", "adjust FreeRTOS heap size", "set up low-power RTOS", "FreeRTOS stack overflow", "FreeRTOS malloc failed", or when another skill (`mtb-wifi-stack`, `mtb-ble-setup`, `mtb-mqtt-client`) requires FreeRTOS as a prerequisite.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.2.0"
---

# mtb-freertos

Install, configure, and integrate the [Infineon FreeRTOS library](https://github.com/Infineon/freertos) into a ModusToolbox project. Generates a `FreeRTOSConfig.h` tuned to the user's application profile and produces a working example task so the scheduler runs immediately.

This is a **library-tier skill**. It orchestrates:
1. Pre-check to detect install state and route to the correct workflow
2. Scope questions to understand the application before generating any configuration
3. Library installation via the `mtb-library-installer` skill (if not already present)
4. `FreeRTOSConfig.h` generation tailored to the user's answers
5. Makefile updates (`COMPONENTS`, `DEFINES`)
6. Integration code — example task and `main()` modifications
7. Build verification

## When to Use This Skill

- User says: "add FreeRTOS", "add an RTOS", "I need tasks", "set up FreeRTOS", "configure FreeRTOS", "reconfigure FreeRTOS", "fix my FreeRTOSConfig.h"
- User reports: "stack overflow", "malloc failed", "FreeRTOS assert", "tasks not scheduling", "heap exhausted", "ISR hard fault with FreeRTOS"
- Another skill (`mtb-wifi-stack`, `mtb-ble-setup`, `mtb-mqtt-client`) has directed the user here because FreeRTOS is a prerequisite
- FreeRTOS is installed but `FreeRTOSConfig.h` is missing from the project
- **⚠️ ALSO read this skill when:** Adding ANY interrupt-driven peripheral (UART, SPI, I2C, **GPIO buttons**) to a project that already has FreeRTOS installed — see [Tickless Idle + SCB Peripherals](#tickless-idle--scb-peripherals-critical) for SCB clock issues and [config-guide.md — Interrupt Priority](./references/config-guide.md#interrupt-priority-and-basepri-masking) for ISRs that never fire

### When Not to Use This Skill

- The project already has FreeRTOS integrated and working and the user is asking about application-level task design (answer directly)
- The user needs ThreadX or another RTOS — explain that this skill is specific to FreeRTOS

## Prerequisites

- A ModusToolbox project with a valid `Makefile` at the project root
- `make` available in the environment (`$CY_TOOLS_PATHS` set)
- This skill is pre-installed at `.github/skills/mtb-freertos/` by the Infineon ModusToolbox AI Assistant VS Code extension
- Determine the core topology before proceeding — see [Detecting the Target Core](#detecting-the-target-core). Only ask the user when detection is genuinely ambiguous.

## Detecting the Target Core

The core count and core name are discoverable; do not ask the user first. Try in order:

1. **MCP workspace data** — the ModusToolbox MCP workspace tool returns a `projects` array with one entry per core sub-project, each carrying `core_type` (`CM0+`, `CM4`, `CM33`, `CM7`, `CM55`) and `folder_name`. More than one entry means a multi-core application.
2. **Make variables** — `MTB_CORE_TYPE` / `MTB_CORE_NAME` for the target core, `MTB_PROJECTS` for the sub-project list.
3. **Filesystem** — sub-directories each holding their own `Makefile` (`proj_*` by convention) indicate multi-core; a lone project root is single-core.

`core_type` is the value that goes into the `COMPONENT_<CORE>` template path, so no mapping table is needed.

On a multi-core application, default to the non-secure application core and confirm with the user before editing.

## Key Files

| File / Directory | Purpose |
|---|---|
| `deps/freertos.mtb` | Library reference — presence confirms installation |
| `deps/clib-support.mtb` | Required for GCC/newlib thread-safe malloc; installed alongside freertos |
| `../mtb_shared/freertos/<version>/` | Library source fetched by `make getlibs` |
| `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_<CORE>/FreeRTOSConfig.h` | Per-core template to copy into the project |
| `FreeRTOSConfig.h` | Project-level config — placed in the project root |
| `freertos_integration.c` / `freertos_integration.h` | Generated integration file with example task |
| `Makefile` | `COMPONENTS += FREERTOS` and optional `DEFINES` managed by this skill |

---

## Pre-Check: Detect Install State

Before taking any action, determine the installation state:

1. Check for `deps/freertos.mtb` (or `<core-subproject>/deps/freertos.mtb`)
2. Check for `../mtb_shared/freertos/` directory
3. Check `Makefile` for `FREERTOS` in `COMPONENTS`
4. Check whether `FreeRTOSConfig.h` exists in the project root

| State | Route |
|-------|-------|
| Not installed | → [Fresh Install Workflow](#fresh-install-workflow) |
| Installed, no `FreeRTOSConfig.h` | Inform user; → [Configure Step](#3-generate-freertosconfigh) |
| Installed and configured | → [Reconfiguration Workflow](#reconfiguration-workflow) |
| `.mtb` exists but `make getlibs` not run | Run `make getlibs`; → [Configure Step](#3-generate-freertosconfigh) |

---

## Fresh Install Workflow

See [config-guide.md](./references/config-guide.md) for scope questions and parameter details.
See [integration-workflow.md](./references/integration-workflow.md) for code and Makefile changes.

### 1. Scope Questions

Ask these questions **one at a time** before generating anything. Present each as a clear choice with a brief reason.

**Q1 — Application Profile** (drives all other defaults):

| Choice | Key implications |
|--------|-----------------|
| IoT / Networking (WiFi, BLE, MQTT) | Large heap, 1000 Hz tick, timers on, clib-support required |
| Low-Power / Battery-operated | Tickless idle, 200 Hz tick, minimal heap |
| General Embedded / Control | Balanced defaults, 1000 Hz tick, 20 KB heap |
| Real-Time Control (motor, hard deadlines) | 1000 Hz tick, preemptive, overflow detection |
| Static / Safety-Critical (no dynamic alloc) | `NO_HEAP_ALLOCATION`, all static APIs |

**Q2 — Memory Allocation Strategy:**

> heap_3 is the **Infineon default and is strongly recommended for all profiles**. It wraps the toolchain's `malloc()`/`free()` for thread safety via `clib-support` and is compatible with all Infineon middleware. Only deviate if you have a specific, well-understood reason.

| Choice | Scheme | Use when |
|--------|--------|---------|
| ⭐ System malloc/free — `heap_3` **(default)** | `HEAP_ALLOCATION_TYPE3` | All profiles unless user specifically requests otherwise |
| FreeRTOS-managed — `heap_4` | `HEAP_ALLOCATION_TYPE4` | No system heap in linker; advanced use only; incompatible with `malloc()`/`free()` in app code |
| Simple, no free — `heap_1` | `HEAP_ALLOCATION_TYPE1` | All RTOS objects created at startup, never freed; rare |
| Static only — no heap | `NO_HEAP_ALLOCATION` | Safety-critical fully-static designs only |

**Q3 — Total heap size** (skip for heap_3 and NO_HEAP — for heap_3 the size is set in the linker script, not here):
See profile-derived suggestions in [config-guide.md](./references/config-guide.md#q3-total-heap-size).

> ⚠️ **heap_3 debugging note:** When heap_3 is active (the ModusToolbox default), `configTOTAL_HEAP_SIZE` has **NO effect** — it is never used. All `pvPortMalloc()`/`vPortFree()` calls map directly to the C runtime's `malloc()`/`free()`, which allocate from the linker-managed heap. If memory exhaustion is suspected, increase the heap size in the **linker script** (not `FreeRTOSConfig.h`). Adjusting `configTOTAL_HEAP_SIZE` when heap_3 is active is a common debugging red herring — it changes nothing. This applies to all Infineon middleware that allocates dynamically (btstack, lwIP, mbedTLS).

**Q4 — Tick rate:** 1000 Hz (default), 200 Hz (low-power), 100 Hz, or custom.

**Q5 — Max task priorities:** 3, 5, 7 (default), or custom.

**Q6 — Tickless idle:** Auto-detect from Device Configurator (recommended), always-on, or off.

**Q7 — Software timers:** Yes (default) or No.

**Q8 — Stack overflow detection:** Level 2 (default, recommended during development), Level 1, or off.

### 2. Install Libraries

Invoke the `mtb-library-installer` skill for:
- `freertos` — primary library
- `clib-support` — required when heap_3 is selected or `configUSE_NEWLIB_REENTRANT` is 1; check `deps/clib-support.mtb` before installing

Verify after `make getlibs`:
- `deps/freertos.mtb` exists
- `../mtb_shared/freertos/<version>/` directory is present
- `deps/clib-support.mtb` exists (if required)

### 3. Generate FreeRTOSConfig.h

See [config-guide.md](./references/config-guide.md) for the full parameter table, profile defaults, template selection, and required hook stubs.

Steps:
1. Resolve the target core — see [Detecting the Target Core](#detecting-the-target-core)
2. Copy `../mtb_shared/freertos/<version>/Source/portable/COMPONENT_<CORE>/FreeRTOSConfig.h` to the project root
3. Remove the `#warning` template directive from the copied file
4. Apply all values from the user's scope question answers
5. Generate required hook function stubs (see [config-guide.md — Hook Stubs](./references/config-guide.md#required-hook-function-stubs))

### 4. Makefile and Code Integration

See [integration-workflow.md](./references/integration-workflow.md) for exact Makefile lines and generated source files.

Steps:
1. Add `COMPONENTS += FREERTOS` to the project `Makefile`
2. Add `DEFINES += CY_RTOS_AWARE` when using GCC toolchain with heap_3/newlib-reentrant
3. Generate `freertos_integration.c` and `freertos_integration.h` with an example task
4. Insert `freertos_integration_start()` and `vTaskStartScheduler()` into `main()`

### 5. Verify

Run `make build`. A passing build confirms integration is complete.

If it fails, check:
- Missing hook stubs (see [config-guide.md — Hook Stubs](./references/config-guide.md#required-hook-function-stubs))
- `FREERTOS` not in `COMPONENTS`
- `FreeRTOSConfig.h` not on the compiler include path (must be in project root)
- `SystemCoreClock` not defined at the point `FreeRTOSConfig.h` is included

---

## Prerequisite-Mode Workflow

When the user arrives from a connectivity skill (`mtb-wifi-stack`, `mtb-ble-setup`, `mtb-mqtt-client`) that requires FreeRTOS:

1. Run Pre-Check — if already installed and configured, report success and return to the task that brought the user here
2. Skip Q1 — infer the profile from that context (IoT / Networking)
3. Apply heap_3 as the default without asking; skip Q2 unless user previously expressed a different preference
4. Ask Q4 (tick rate) only if the user has expressed a low-power concern
5. Inform the user of all defaults being applied in a concise summary before proceeding
6. Skip the example task generation (`freertos_integration.c`) — the connectivity skill supplies its own task
7. Complete library install → config → Makefile steps, then return the user to the original skill

---

## Reconfiguration Workflow

When FreeRTOS is already installed and configured. See [reconfig-guide.md](./references/reconfig-guide.md) for the full problem-to-fix mapping.

1. Read and parse the existing `FreeRTOSConfig.h`
2. Present a concise summary of current settings to the user
3. Ask what the user wants to change or fix — accept free-form description of a problem or a specific parameter
4. Map common complaints to targeted fixes (see [reconfig-guide.md](./references/reconfig-guide.md))
5. Show a diff-style summary of all proposed changes before modifying any file
6. Apply confirmed changes and run `make build` to verify

---

## Error Handling

| Condition | Action |
|-----------|--------|
| `FreeRTOSConfig.h` template missing after `make getlibs` | Re-run `make getlibs` once; if still absent, report and ask user to check library installation |
| CPU core cannot be determined | Exhaust all three sources in [Detecting the Target Core](#detecting-the-target-core) first; only then ask the user to name the core |
| `COMPONENTS += FREERTOS` already present | Skip Makefile edit; proceed to config |
| `clib-support` not installed when heap_3 selected | Install via `mtb-library-installer` before continuing |
| `vTaskStartScheduler()` already in `main.c` | Do not add a second call; skip `main.c` edits; inform user |
| `configTOTAL_HEAP_SIZE` exceeds ~75% of BSP SRAM | Warn user with SRAM estimate; ask for confirmation |
| Tickless idle enabled but `abstraction-rtos` absent | Offer to install `abstraction-rtos`; if declined, set `configUSE_TICKLESS_IDLE 0` and explain requirements |
| Tickless idle enabled but SCB peripherals in use (UART, SPI, I2C) | **⚠️ CRITICAL:** Deep sleep gates the SCB peripheral clocks, corrupting the state of any SCB that is mid-transaction. UART output becomes garbled (`BþBBBB...` pattern) after first idle cycle. **Register SysPm callbacks** for each active SCB peripheral to block deep sleep while busy — see [Tickless Idle + SCB Peripherals](#tickless-idle--scb-peripherals-critical) section below. |
| ISR never fires after adding FreeRTOS (UART, SPI, I2C, GPIO) | The port raises `BASEPRI` at init, masking ISRs even before the scheduler starts — see [config-guide.md — Interrupt Priority](./references/config-guide.md#interrupt-priority-and-basepri-masking) |
| Build fails after changes | Show error output; consult [integration-workflow.md — Build Failures](./references/integration-workflow.md#build-failure-checklist) |
| Multi-core project; target core ambiguous | Propose the non-secure application core from the detected `projects` list and ask the user to confirm |

---

## Tickless Idle + SCB Peripherals (CRITICAL)

**Problem:** When `configUSE_TICKLESS_IDLE` is enabled (value 1 or 2), the port-optimized `vApplicationSleep()` enters Deep Sleep via `Cy_SysPm_CpuEnterDeepSleep()`. Deep Sleep powers down SCB peripheral clocks (UART, SPI, I2C) before pending transactions complete.

**Symptom:** Initial printf output works correctly, then subsequent output becomes garbled (`BþBBBB...` pattern) after the first FreeRTOS idle cycle.

### Solution: Register SysPm Callbacks

Each active SCB peripheral must register a callback that blocks Deep Sleep while the peripheral is busy:

| SCB Type | Busy Check API | When to Block |
|----------|---------------|---------------|
| UART | `Cy_SCB_IsTxComplete(CYBSP_<ALIAS>_HW)` | TX FIFO not empty |
| SPI | `Cy_SCB_SPI_IsBusBusy(CYBSP_<ALIAS>_HW)` | Transfer in progress |
| I2C | `Cy_SCB_I2C_IsBusBusy(CYBSP_<ALIAS>_HW)` | Transaction in progress |

### Required Code (per SCB peripheral)

```c
#include "cy_syspm.h"

static cy_en_syspm_status_t scb_deep_sleep_callback(
    cy_stc_syspm_callback_params_t *params,
    cy_en_syspm_callback_mode_t mode)
{
    CySCB_Type *base = (CySCB_Type *)params->base;
    
    if (mode == CY_SYSPM_CHECK_READY) {
        /* For UART: */ if (!Cy_SCB_IsTxComplete(base)) return CY_SYSPM_FAIL;
        /* For SPI:  */ if (Cy_SCB_SPI_IsBusBusy(base)) return CY_SYSPM_FAIL;
        /* For I2C:  */ if (Cy_SCB_I2C_IsBusBusy(base)) return CY_SYSPM_FAIL;
    }
    return CY_SYSPM_SUCCESS;
}

/* Callback struct (one per SCB instance) */
static cy_stc_syspm_callback_params_t pm_params = { .base = CYBSP_DEBUG_UART_HW, .context = NULL };
static cy_stc_syspm_callback_t pm_callback = {
    .callback = scb_deep_sleep_callback,
    .type = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &pm_params,
    .order = 0U,
    .nextItm = NULL, .prevItm = NULL, .skipMode = 0U
};

/* Register after peripheral init */
Cy_SysPm_RegisterCallback(&pm_callback);
```

### Development Workflow

When FreeRTOS is detected with `configUSE_TICKLESS_IDLE != 0` AND any SCB peripheral is configured:

1. **WARN** the user about the tickless + SCB interaction
2. **ADD** SysPm callback code for each active SCB peripheral
3. **VERIFY** callbacks are registered before `vTaskStartScheduler()`

See also:
- **TCPWM / HF-clock peripherals:** Tickless DeepSleep stops ALL HF clocks, halting timers, PWM, and ADC. Register a DeepSleep veto callback for any HF-clocked peripheral that must remain active.
- **"Works under the debugger, fails on a power cycle"** is the classic signature of an unintended DeepSleep entry: the debug probe keeps the clocks alive for SWD, a standalone run does not. Suspect a missing veto callback before suspecting the peripheral.
- **BLE:** the HCI transport uses a UART SCB, but the BT stack holds a sleep lock during active HCI communication, so tickless idle should not normally interfere. If unexplained HCI timeouts occur, temporarily disabling tickless idle is a useful diagnostic step to rule out deep sleep interference during firmware download.
- If the `mtb-device-configurator` skill is deployed for this device, its `references/uart-runtime-patterns.md`, `spi-transfer-patterns.md`, `i2c-bus-patterns.md` and `power-mode-patterns.md` carry the per-peripheral deep-sleep detail and the device-specific power-mode rules.

---

## References

- [Scope Questions & FreeRTOSConfig.h Guide](./references/config-guide.md)
- [Integration Workflow & Code Templates](./references/integration-workflow.md)
- [Reconfiguration & Troubleshooting Guide](./references/reconfig-guide.md)
- [Infineon FreeRTOS GitHub](https://github.com/Infineon/freertos)
- [FreeRTOS Configuration Docs](https://www.freertos.org/a00110.html)
- [FreeRTOS Heap Management](https://www.freertos.org/a00111.html)
- [CLib Support Library](https://github.com/Infineon/clib-support)
- [RTOS Abstraction Library](https://github.com/Infineon/abstraction-rtos)

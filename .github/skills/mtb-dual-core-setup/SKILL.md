---
name: mtb-dual-core-setup
description: Use when bringing up or reconfiguring a previously unused core so that two cores are actively running application logic at the same time. Use when activating a previously inactive/sleeping core, configuring boot synchronization, setting up FreeRTOS on previously inactive core, or adding a secondary core to a ModusToolbox project. This skill applies only to PSOC Edge E84 projects with a valid 3-project layout (`proj_cm33_s`, `proj_cm33_ns`, `proj_cm55`).
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Dual-Core Setup — PSOC Edge E84

Set up a PSOC Edge project with an **active CM55 core** running application logic alongside CM33.

> **Prerequisites:** Read `CONTEXT.md` first. This skill applies only to PSOC Edge E84 with a valid 3-project layout (`proj_cm33_s`, `proj_cm33_ns`, `proj_cm55`).

---

## When to use this skill

- Your CM55 needs to run DSP, radar, audio, graphics, ML inference, or other compute-intensive workloads
- You need IPC (inter-processor communication) between CM33 and CM55
- Default `proj_cm55/main.c` deep-sleep loop needs to be replaced with active code
- The CM33 core was launching the CM55 and then going to sleep, but now the CM33 will be performing application logic.

---

## Step 1: Project Structure

The 3-project layout is mandatory. CM55 gets its own `source/` and `include/`:

```
proj_cm55/
├── Makefile
├── main.c
├── source/
│   └── cm55_app_task.c      ← Your CM55 application logic
├── include/
│   └── cm55_app_task.h
├── FreeRTOSConfig.h          ← CM55 needs its own FreeRTOS config
└── deps/
    └── freertos.mtb          ← If using FreeRTOS on CM55
```

## Step 2: CM55 Makefile Configuration

Add to `proj_cm55/Makefile`:
```makefile
# Enable FreeRTOS on CM55
COMPONENTS+=FREERTOS

# If using Arm Helium/MVE SIMD instructions
DEFINES+=configENABLE_MVE=1
```

> **Critical:** Without `configENABLE_MVE=1`, FreeRTOS will not save MVE/Helium registers on context switch. Any task using SIMD intrinsics will silently corrupt data when preempted.

> **Stack sizing for graphics:** If CM55 runs LVGL, set `configMINIMAL_STACK_SIZE = 512` (32 KB) in CM55's `FreeRTOSConfig.h`. Non-graphics projects typically default to 128 (1 KB), which causes immediate stack overflow when LVGL allocates framebuffers. Stack overflow manifests as HardFault with no useful backtrace.

## Step 3: Boot Synchronization (CM33 side)

In `proj_cm33_ns/main.c`, CM33 must initialize IPC and enable CM55 **before** CM55 attempts any IPC:

```c
#include "cy_syspm.h"

int main(void)
{
    cybsp_init();
    init_retarget_io();

    /* Enable CM55 core — must happen before CM55 tries IPC */
    Cy_SysPm_SetSRAMPwrMode(CY_SYSPM_SRAM_PWR_MODE_ON, CY_SYSPM_SRAM_ALL);
    Cy_SysEnableCM55();

    /* Start FreeRTOS scheduler */
    vTaskStartScheduler();
}
```

### Debug Output Strategy

In dual-core projects, only **one core** may own `cy_retarget_io_init()` and `printf()`. The other core must use an alternative (lightweight `PutString`, IPC channel, or hand-off). Violating this causes the most common dual-core debugging issue — intermittent or missing serial output.

→ See [multi-core-debug-output.md](../mtb-device-configurator/references/multi-core-debug-output.md) for patterns and decision guide.

## Step 4: CM55 Boot Wait

CM55 must wait for CM33 to signal readiness before using shared resources:

```c
/* proj_cm55/main.c */
int main(void)
{
    cybsp_init();

    /* Wait for CM33 to initialize shared resources */
    while (!cm33_ready_flag)
    {
        __WFE();  /* Low-power wait */
    }

    /* Start FreeRTOS scheduler on CM55 */
    vTaskStartScheduler();
}
```

### D-Cache Invalidation After cybsp_init() (CRITICAL)

On CM55, the Reset_Handler enables D-Cache BEFORE `cybsp_init()` configures MPU non-cacheable regions. This means shared memory locations are cached during the boot window. After `cybsp_init()` returns, those stale cache lines persist even though MPU now marks the regions as non-cacheable.

**Fix:** Immediately after `cybsp_init()` in CM55 `main.c`:

```c
cybsp_init();
/* Invalidate stale D-Cache lines from boot window */
SCB_InvalidateDCache_by_Addr((void *)IPC_SHARED_BASE, IPC_SHARED_SIZE);
```

Without this, IPC reads return stale/zero data intermittently. This is the #1 cause of "IPC works sometimes" bugs on PSOC Edge dual-core projects.

## Step 5: Shared Memory Configuration

For IPC data exchange, use the `/ipc-patterns` skill. The shared memory section must be defined in both cores' linker scripts at the same physical address.

### IPC Command Routing: Pre-Framework vs. Post-Framework

IPC poll tasks on the receiver core start processing commands as soon as boot completes. But application frameworks (Matter, WiFi manager, MQTT client) may not be initialized yet.

**Pattern:** Route framework-dependent IPC commands through the application's event queue, not directly in the IPC poll task:
- **Immediate commands** (local state queries, LED control): Execute directly in IPC handler
- **Deferred commands** (Matter attribute writes, WiFi state changes): Post to app event queue; app task processes when framework is ready

This prevents crashes or silent data corruption when IPC commands arrive during framework initialization.

---

## Common Mistakes

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Missing `configENABLE_MVE=1` | Silent data corruption in CM55 tasks using SIMD | Add `DEFINES+=configENABLE_MVE=1` to CM55 Makefile |
| CM55 starts before CM33 is ready | HardFault or IPC deadlock on boot | Add boot synchronization handshake |
| No `FreeRTOSConfig.h` in `proj_cm55/` | CM55 uses CM33's config (wrong heap, stack sizes) | Create separate `FreeRTOSConfig.h` in `proj_cm55/` |
| Both cores call `cy_retarget_io_init()` | Garbled or missing serial output (timing-dependent) | Only one core owns retarget-io — see [multi-core-debug-output.md](../mtb-device-configurator/references/multi-core-debug-output.md) |

---

## Peripheral-to-Core Assignment

Not all peripherals are accessible from both cores. The PPU (Protection Policy Unit) restricts CM55 from accessing certain CM33-reserved peripherals — a PPU violation produces a BusFault with no runtime error message.

### Assignment Rules

| Capability | Typical Core | Reason |
|---|---|---|
| WiFi, BLE, MQTT, cloud connectivity | CM33 | Radio hardware access, TLS stack |
| LVGL display, touch input | CM55 | Compute-intensive rendering, GPU access |
| Sensors on display I2C bus | CM55 | Shared SCB — same core as display touch |
| Sensors on independent I2C/SPI | Either | Assign to whichever core needs the data |
| Audio/DSP processing | CM55 | Helium/MVE SIMD acceleration |
| ML inference | CM55 | Ethos-U55 NPU access |

### Shared Bus Rule

**If two peripherals share the same I2C/SPI bus (SCB instance), they must be managed by the same core.** There is no hardware arbitration between cores on a shared SCB — simultaneous access corrupts transactions.

Example: An EVK may expose I2C pins on Arduino headers that share the same SCB instance used by a display touch controller. Any external sensor connected to those shared pins must be driven by the same core that owns the touch controller.

**How to check:** Device Configurator → Peripherals → examine which SCB instances are assigned.

---

## Memory Rebalancing for Dual-Core Projects

BSP defaults split System SRAM roughly evenly between CM33 code and data. In practice, CM33 code is small (runs from flash) while runtime data (WiFi/MQTT/TLS heaps, stacks, BSS) fills up fast.

### When to Rebalance

| Adding to CM33 | Memory Pressure | Action |
|---|---|---|
| WiFi + TLS | High (~530 KB data) | Shrink `m33_code`, grow `m33_data` |
| MQTT | Moderate (+30-50 KB) | Check `m33_data` headroom |
| BLE stack | Moderate (+60-100 KB) | Check `m33_data` headroom |

### How to Rebalance

1. Open `design.modus` → **Memory Configuration** (not Peripherals)
2. Regions are contiguous — shrinking one grows its neighbor
3. Totals must match hardware: System SRAM = `0x100000` (1 MB), SOCMEM = `0x500000` (5 MB)
4. Save → clean rebuild required: `make clean && make build`

> For display-specific memory (gfx_mem sizing, framebuffer formula), see /lvgl-setup §9.

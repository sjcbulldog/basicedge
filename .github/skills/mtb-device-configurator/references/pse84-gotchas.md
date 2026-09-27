# PSOC Edge E84 (PSE84) — Device-Specific Gotchas

> **Read this file BEFORE writing any peripheral init code for PSE84 targets.**
> Also read [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) for
> universal gotchas that apply to all Infineon devices.

## 1. `__NVIC_PRIO_BITS = 3` (NOT 4)

Despite being a Cortex-M33 core (which architecturally supports up to 8 bits),
the PSE84 implements only **3 priority bits** — giving 8 priority levels (0–7).

### How to verify:

1. Search `bsps/TARGET_*/cy_device_headers_ns.h` for `#define __NVIC_PRIO_BITS`
2. Calculate shift: `8 - __NVIC_PRIO_BITS` (PSE84: `8 - 3 = 5`, so `<< 5`)
3. Read `configMAX_SYSCALL_INTERRUPT_PRIORITY` from `<project>/FreeRTOSConfig.h`

**Common error:** Assuming 4 bits because "most M33 devices use 4." The PSE84 does not.

**Impact on FreeRTOS:** See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md)
for the full lookup procedure and BASEPRI masking implications.

**Tickless idle note:** With only 3 priority bits, most ISRs fall at or below the RTOS
ceiling. This makes the `portMAX_DELAY` + tickless idle gotcha more likely to manifest on
PSE84 than on devices with more priority granularity. See
[`device-configurator-gotchas.md`](./device-configurator-gotchas.md) §3.

## 2. D-Cache Boot Window (CM55 Only)

On dual-core PSE84 projects using CM55, the Reset_Handler enables D-Cache **before**
`cybsp_init()` configures MPU non-cacheable regions. Shared memory is cached during this window.

### How to determine if affected:

1. Confirm project has a CM55 sub-project (`proj_cm55/`)
2. Check if shared memory is used for IPC (look for shared linker sections or `IPC_SHARED_*` defines)
3. If both true → this gotcha applies

### Fix:

Immediately after `cybsp_init()` in CM55 `main.c`:
```c
cybsp_init();
SCB_InvalidateDCache_by_Addr((void *)IPC_SHARED_BASE, IPC_SHARED_SIZE);
```

**Symptom without fix:** IPC reads return stale/zero data intermittently — the #1 cause of
"IPC works sometimes" bugs on dual-core projects.

## PSE84-Specific Notes on Universal Gotchas

These gotchas are documented in [`device-configurator-gotchas.md`](./device-configurator-gotchas.md)
but have PSE84-relevant context:

| Universal Gotcha | PSE84 Note |
|------------------|------------|
| §1 IP Block Versioning | PSE84 DSL path: `mtb_shared/mtb-dsl-pse8xxgp/`. Uses TCPWM v2, SCB v3. |
| §2 Hidden Clock Dividers | PSE84 default: CLKHF0 400 MHz with peri group /4 → 100 MHz peripheral base. Verify in DC. |
| §4 Pin Mux / HSIOM | Arduino headers on KIT_PSE84_EVAL_EPC2 have limited TCPWM routing — check DC dropdown. |

## Related

- [`device-configurator-gotchas.md`](./device-configurator-gotchas.md) — universal gotchas (all devices)
- [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md) — BASEPRI lookup procedure
- [`pre-coding-verification.md`](./pre-coding-verification.md) — mandatory pre-coding checklist

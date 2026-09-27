# Memory Power Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read `cy_syspm.h` directly (see `pdl-apis.md`).
>
> **Cross-reference:** Power mode transitions, callbacks, and DC power profile
> settings are in [power-mode-patterns.md](./power-mode-patterns.md).

## Device Configurator Setup

The `power_v4` personality exposes a **PD1 Enable** checkbox (`pd1Enable`) that
controls whether the CM55 + SRAM1 power domain is powered at boot.

| DC Field | Generated Define | Impact |
|----------|-----------------|--------|
| PD1 Enable | `CY_CFG_PWR_PD1_DOMAIN` | When 1: `init_cycfg_power()` calls `Cy_System_EnablePD1()` + enables CM55 TCM. When 0: PD1 stays off — CM55 + SRAM1 unpowered. |

> Inspect `init_cycfg_power()` in `GeneratedSource/cycfg_system.c` for the exact
> boot sequence (PD1 enable → TCM access → PDCM dependency clearing).

## Patterns

### Disabling PD1 for single-core operation

Disable PD1 when running CM33-only (no CM55/NPU workload). This is the
single largest power reduction — removes CM55, SRAM1, and associated HF clocks.

```c
/* Disable HF clocks feeding CM55 domain */
Cy_SysClk_ClkHfDisable(CY_CFG_SYSCLK_CLKHF1);
Cy_SysClk_ClkHfDisable(CY_CFG_SYSCLK_CLKHF2);
/* With PD1 disabled in DC (pd1Enable=false), CM55 is never started */
```

### SRAM macro power control

SRAM0 has up to 8 macros. Each can be independently ON or OFF at runtime.
CAT1D does NOT support `CY_SYSPM_SRAM_PWR_MODE_RET` — only ON/OFF.

```c
/* Power off unused SRAM0 macro (highest-numbered first) */
Cy_SysPm_SetSRAMMacroPwrMode(
    CY_SYSPM_SRAM0_MEMORY, CY_SYSPM_SRAM0_MACRO_7,
    CY_SYSPM_SRAM_PWR_MODE_OFF);

/* Power off entire SRAM1 (only if CM55 is not running) */
Cy_SysPm_SetSRAMPwrMode(CY_SYSPM_SRAM1_MEMORY, CY_SYSPM_SRAM_PWR_MODE_OFF);
```

> Power off from highest-numbered macro down — linker fills from low addresses.
> Verify the linker map to confirm which macros are in use.

### SoCMEM power control (CAT1D only)

SoCMEM (~5 MB shared memory) has its own PPU. Supports full disable and
per-partition active/DeepSleep power control.

```c
/* Disable SoCMEM entirely */
Cy_SysEnableSOCMEM(false);

/* Set SoCMEM behavior during DeepSleep */
Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

/* Per-partition power control */
Cy_SysPm_SetSOCMemPartActivePwrMode(partition_idx, power_mode);
Cy_SysPm_SetSOCMemPartDsPwrMode(partition_idx, power_mode);
```

> SoCMEM is enabled by `init_cycfg_mpc()` (not `init_cycfg_power()`) when MPC
> regions are configured. If unused after boot, disable at runtime for savings.

### SRAMLDO voltage adjustment

SRAMLDO is auto-derived from the DC power profile but can be adjusted at runtime:

```c
Cy_SysPm_SramLdoSetVoltage(CY_SYSPM_SRAMLDO_VOLTAGE_0_80V);
Cy_SysPm_SramLdoEnable(true);
```

> Profile → SRAMLDO mapping: HP=0.90V, LP=0.80V, ULP=0.80V.

### DeepSleep-RAM warm boot (advanced)

DeepSleep-RAM retains selected SRAM but resets the CPU. On wakeup, boot ROM
jumps to a warm-boot entry point in retained RAM.

```c
Cy_Syslib_SetWarmBootEntryPoint(warmboot_entry_function);
Cy_SysPm_SetupDeepSleepRAM(/* config */);
Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_RAM);
Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_RAM);
Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
```

The warm-boot entry function must: set VTOR, re-enable peripheral IPs, initialize
SMIF for flash access, then jump to main application.

> **Agent guidance**: DeepSleep-RAM is advanced. Only recommend when the user
> explicitly needs partial retention with minimal wakeup current. Standard
> DeepSleep (full retention, no reset) is the right default.

### DeepSleep variant → memory retention

| DeepSleep Variant | SRAM Retained | SoCMEM | CPU on Wakeup |
|-------------------|--------------|--------|---------------|
| DeepSleep | All | Configurable | Resumes from WFI |
| DeepSleep-RAM | Configurable | Configurable | CPU + peripherals RESET |
| DeepSleep-OFF | None | Off | CPU + peripherals RESET |

## Gotchas

1. **PD1 and CM55 coupling** — Disabling PD1 powers off CM55 and SRAM1. Never
   disable PD1 if CM55 code is running.

2. **SRAM macro OFF is destructive** — Data is lost. Only power off macros
   confirmed unused via the linker map.

3. **No RET mode on CAT1D** — Unlike PSOC 6 (CAT1A), PSE84 SRAM macros only
   support ON/OFF. There is no low-power retention mode for individual macros.

4. **SoCMEM enable is in MPC init** — `Cy_SysEnableSOCMEM(true)` runs during
   `init_cycfg_mpc()`, not `init_cycfg_power()`. SoCMEM is re-enabled on reset.

5. **RRAM voltage is manual** — Unlike SRAMLDO (auto-derived), RRAM voltage must
   be set explicitly after every power profile transition. See
   [power-mode-patterns.md](./power-mode-patterns.md) gotcha #2.

6. **DeepSleep-RAM requires RAM-resident code** — The warm-boot entry MUST be in
   retained RAM. Flash (SMIF/XIP) is unavailable immediately after wakeup.

7. **PDCM dependency clearing** — Boot code sets APPCPU→SYSCPU dependency.
   Generated `init_cycfg_power()` clears it so CM55 can sleep independently.
   Not needed when PD1 is disabled.
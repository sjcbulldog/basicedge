# Adding LVGL to Existing Project — Overview

> Use this guide when your project already has WiFi, BLE, Matter, or other configurations to preserve.

## Quick Reference

| Topic | Key Point |
|-------|-----------|
| **Critical path** | Memory → GFXSS → MPU → Drivers → Init → IPC |
| **Core assignment** | LVGL runs on **CM55** (graphics subsystem owner) |
| **Memory requirement** | ~3 MB SOCMEM for framebuffers + GPU heap |

## Prerequisites

- ModusToolbox 3.7+ with PSOC Edge E84 BSP
- An existing dual-core project (CM33 + CM55) that builds and runs
- A working graphics reference project on the same board (e.g., Smart Home Panel)
- Waveshare 4.3" MIPI-DSI LCD with FT5406 touch (or compatible display)
- Device Configurator and Memory Configurator familiarity

---

## Step-by-Step Reference Files

| Step | File | What It Covers |
|------|------|----------------|
| 1 | [graphics-memory-layout.md](./graphics-memory-layout.md) | Memory Configurator — allocate `gfx_mem` region |
| 2-3 | [gfxss-display-config.md](./gfxss-display-config.md) | Device Configurator GFXSS + MPU non-cacheable config |
| 4-6 | [graphics-dependencies.md](./graphics-dependencies.md) | Dependencies, driver files, Makefile, FreeRTOSConfig |
| 7-8 | [graphics-task-init.md](./graphics-task-init.md) | Graphics task init sequence, UART sharing |
| 9 | Use the **mtb-ipc-patterns** skill | IPC between CM33 and CM55 — Pattern 4 (Bidirectional) recommended for display apps |

---

## ⛔ Common Pitfalls Checklist (Read First)

| # | Pitfall | Symptom | Fix |
|---|---------|---------|-----|
| 1 | FreeRTOS stack too small | Blank display, silent crash | `configMINIMAL_STACK_SIZE = 512` |
| 2 | Missing `init_retarget_io()` on CM55 | CM55 halts before display init | Always call init, both cores share UART |
| 3 | `COMPONENTS+=GFXSS` only in CM55 Makefile | CM33 build fails (cy_graphics.h not found) | Add to `common.mk` |
| 4 | I2C too fast for display | Error 0x00AA2001, garbled ID reads | Clock divider=31, ~184 kHz SCL |
| 5 | Dual-core I2C bus conflict | Display init fails intermittently | Only one core owns each SCB instance |
| 6 | Framebuffers cacheable | Garbled/corrupted display | MPU non-cacheable region for gfx_mem |
| 7 | MPU regions not synced after memory move | Garbled display despite MPU "configured" | Manually verify MPU addresses match Memory Configurator |
| 8 | Device Configurator silent save failure | Settings revert on close/reopen | Check generated code, re-enter if needed |
| 9 | SDA stuck LOW on I2C bus | Display I2C init fails with NAK | I2C bus recovery (SCL toggle) before init |
| 10 | IPC init runs after IPC writes | Shared data zeroed | `ipc_manager_init()` must precede any IPC writes |

---

## Architecture Diagram

```
CM33 (200 MHz)                    CM55 (400 MHz)
┌────────────────┐               ┌────────────────┐
│ Matter Stack   │               │ LVGL v9 + GPU  │
│ WiFi/BLE       │──── IPC ────→│ GFXSS→MIPI-DSI │
│ App Logic      │←── IPC ──────│ Touch (FT5406) │
│ (Authority)    │               │                │
└────────────────┘               └────────────────┘
     │                                   │
     └───── Shared Memory (0x261C0000) ──┘
              Non-Cacheable via MPU
```

---

## Memory Budget Reference

For typical LVGL + connectivity project on PSOC Edge E84:

| Resource | Typical Usage | Budget |
|----------|---------------|--------|
| CM33 SRAM | 400-530 KB | 532 KB |
| CM55 Flash | 1.5-2.0 MB | 2.8 MB |
| SOCMEM gfx_mem | 1.6-3.0 MB | 3.0 MB |
| Framebuffers (832×480 RGB565 ×2) | 1.6 MB | — |

*Validated on KIT_PSE84_EVAL_EPC2 with ModusToolbox 3.7*

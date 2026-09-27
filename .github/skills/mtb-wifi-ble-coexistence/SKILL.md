---
name: mtb-wifi-ble-coexistence
description: Set up simultaneous WiFi and BLE operation on combo-radio BSPs. Use when the application needs both WiFi connectivity and Bluetooth Low Energy on a shared radio chip (e.g., CYW43xxx, CYW55500). Handles the critical initialization ordering, FreeRTOS task coordination, memory budgeting, and Makefile configuration required for reliable dual-radio operation.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# wifi-ble-coexistence

Set up simultaneous WiFi and BLE operation on a combo-radio ModusToolbox project. This skill handles the coordination layer that makes WiFi and BLE work together on a shared radio — initialization ordering, FreeRTOS task synchronization, combined Makefile configuration, and memory budgeting.

This is an **orchestration-tier skill**. It combines knowledge from the `mtb-wifi-stack` and `mtb-ble-setup` skills with coexistence-specific coordination:
1. Scope clarification (WiFi mode, BLE role, operational model)
2. BSP detection (combo radio type, platform requirements)
3. Combined library installation via `mtb-library-installer`
4. Unified Makefile configuration (WiFi + BLE COMPONENTS and DEFINES)
5. Memory budget verification (FreeRTOS heap, SRAM partition)
6. Coordinated initialization code with FreeRTOS task synchronization
7. WiFi initialization (WCM) that loads shared radio firmware
8. BLE initialization (BTSTACK) gated on radio firmware readiness

## When to Use This Skill

- User says: "add WiFi and BLE", "BLE provisioning for WiFi", "WiFi onboarding via Bluetooth", "I need both WiFi and BLE", "combo radio setup"
- The `mtb-ble-setup` skill detected WiFi already present on a combo-radio BSP
- A higher-level skill requires both connectivity stacks (e.g., cloud-connected sensor with BLE provisioning)

### When Not to Use This Skill

- BSP has a standalone BLE chip (no shared radio) — use `mtb-wifi-stack` and `mtb-ble-setup` independently
- Only WiFi is needed — use `mtb-wifi-stack`
- Only BLE is needed — use `mtb-ble-setup`
- WiFi and BLE are on separate hardware (e.g., external BLE module) — no coexistence coordination needed

## Prerequisites

- A ModusToolbox project with a valid `Makefile` at the project root
- **Combo-radio BSP:** The BSP must include a shared WiFi+BLE radio chip. Verify by checking BSP documentation or BSP component directories for both WiFi and BLE capabilities.
- **FreeRTOS must be installed.** If absent, direct to `mtb-freertos` skill first.

## Key Constraint: Shared Radio Firmware

On combo-radio chips, WiFi and BLE share a single radio with one firmware image. The WiFi Host Driver (WHD) loads this firmware during `cy_wcm_init()`. Until that firmware is loaded, the BLE HCI transport is not available.

**This means:**
- `wiced_bt_stack_init()` MUST NOT be called until `cy_wcm_init()` has returned successfully
- The WiFi task and BLE task must coordinate via FreeRTOS synchronization
- Both stacks share radio time via internal firmware arbitration (time-division multiplexing)
- No special application-level radio arbitration is needed — the firmware handles it

## Step-by-Step Workflow

> ⛔ **MANDATORY: Ask ALL scope questions BEFORE doing anything else.**

See [workflow.md](./references/workflow.md) for the complete execution sequence. High-level steps:

1. **Check prerequisites** — verify FreeRTOS; confirm BSP has combo radio capability
2. **Ask scope questions (REQUIRED)** — coexistence-specific + WiFi + BLE questions together
3. **Present configuration summary** — confirm with user
4. **Detect BSP configuration** — BLE component name, SDIO requirements, platform DEFINES
5. **Install all libraries** — WiFi stack + BLE stack together via `mtb-library-installer`
6. **Update Makefile** — combined COMPONENTS, DEFINES, config file paths
7. **Copy/generate configuration files** — WiFi configs (lwipopts.h, mbedtls, etc.) per `mtb-wifi-stack` patterns
8. **Verify memory budget** — check FreeRTOSConfig.h heap size, SRAM allocation
9. **Generate coordinated initialization code** — WiFi task, BLE task with synchronization
10. **Generate GATT database** — per `mtb-ble-setup` patterns
11. **Verify integration** — build the project

## Scope Questions

Ask **all applicable questions together** in a single message. See [workflow.md §Scope Questions](./references/workflow.md#scope-questions).

| # | Question | Always? |
|---|---|---|
| Q1 | Operational model: BLE provisions WiFi, concurrent operation, or BLE stops after WiFi connects? | Yes |
| Q2 | WiFi mode: STA, SoftAP, or Concurrent? | Yes |
| Q3 | BLE role: Peripheral, Central, or Both? | Yes |
| Q4 | GATT database: describe services/characteristics | Yes |
| Q5 | WiFi security: WPA2, WPA3, Open? | Yes |
| Q6 | BLE security: Just Works, Passkey, None? | Yes |
| Q7 | Device name (BLE advertising) | Yes |
| Q8 | Enable debug logging? | Yes |
| Q9 | WiFi credentials (SSID, password) | Yes |

## Troubleshooting

| Condition | Action |
|---|---|
| BLE does nothing after init | Verify `cy_wcm_init()` completed before `wiced_bt_stack_init()` — the #1 combo-radio failure |
| `cy_wcm_init()` hangs | BSPs requiring explicit SDIO init: confirm `app_sdio_init()` called first |
| HardFault during TLS handshake | FreeRTOS heap too small — need ≥160 KB for WiFi+BLE+TLS+MQTT |
| Intermittent WiFi buffer failures | SRAM partition too small — increase CM33 data SRAM via Device Configurator |
| BLE throughput degrades with WiFi active | Expected — radio time-division. Reduce WiFi polling frequency or stop BLE advertising when not needed |
| WiFi connects but BLE never starts | Check that `xTaskNotifyGive()` is called after `cy_wcm_init()` returns success |
| Build errors: both WiFi and BLE symbols undefined | Verify combined COMPONENTS line includes both WiFi and BLE components |
| MQTT reconnect fails after WiFi drop | Disconnect MQTT client before reconnecting WiFi — stale socket error otherwise |

> **Heap sizing guidance:** WiFi+BLE coexistence with TLS (MQTT or HTTPS) requires at minimum 160 KB FreeRTOS heap. With `heap_3.c` (ModusToolbox default), heap is the C runtime heap controlled by the linker — `configTOTAL_HEAP_SIZE` has no effect. Verify the linker script allocates sufficient SRAM for the heap region.

## References

- [Detailed Workflow](./references/workflow.md) — scope questions, combined library installation, execution sequence
- [Code Patterns](./references/code-patterns.md) — init ordering, task synchronization, memory configuration, SDIO init
- [WiFi Stack Skill](../mtb-wifi-stack/SKILL.md) — WiFi-specific configuration and patterns
- [BLE Setup Skill](../mtb-ble-setup/SKILL.md) — BLE-specific configuration and GATT patterns
- [Library Installer Skill](../mtb-library-installer/SKILL.md)

# BLE Setup — Detailed Workflow

## Scope Questions

> ⛔ **STOP. Do NOT install libraries, modify files, or run any command until ALL questions below have been explicitly answered by the user.**
>
> **Do NOT infer, assume, or pre-fill any answer** from installed libraries, project state, or previous sessions. Every answer must come from the user's own words.

Ask **all questions together in a single message** as a numbered list. Wait for the user to answer all before proceeding.

### Q1: BLE Role

> *What BLE role does your application need?*
>
> - **Peripheral (GATT Server)** — The device advertises and accepts connections from phones/tablets/centrals. It hosts a GATT database with services and characteristics that remote devices read/write. Most common for IoT sensors, actuators, and beacons.
> - **Central (GATT Client)** — The device scans for and connects to peripherals. It discovers their services and reads/writes their characteristics. Used for gateways, data collectors, or devices that consume BLE sensor data.
> - **Both (Peripheral + Central)** — The device can advertise its own services while also connecting to other peripherals. Used for mesh-like topologies or bridge devices.

### Q2: GATT Database Definition

> *Describe the BLE services and characteristics your device should expose (Peripheral/Both) or consume (Central):*
>
> For **Peripheral/Both**, describe:
> - Service name and purpose (e.g., "Temperature Sensor Service")
> - Each characteristic: name, data type, size, properties (Read, Write, Notify, Indicate)
> - Whether standard Bluetooth SIG profiles apply (Heart Rate, Battery, etc.) or a custom service
>
> For **Central only**:
> - Target device/service UUID to connect to (if known)
> - Which characteristics to read/subscribe to
>
> *If you're unsure, I can generate a simple custom service with one read/write characteristic and one notify characteristic as a starting template.*

### Q3: Security Level

> *What BLE pairing/security level should the device use?*
>
> - **None (open)** — No pairing required. Any device can connect and access all characteristics. Simplest; appropriate for non-sensitive data or development.
> - **Just Works** — Pairing with no user interaction (no PIN display). Provides encryption but no MITM protection. Good for devices without displays or input.
> - **Passkey** — A 6-digit PIN is displayed or entered during pairing. Provides MITM protection. Requires a display or fixed passkey.

### Q4: Device Name

> *What device name should appear in BLE advertising? (max 29 bytes)*
>
> This is the name visible to phones and other scanners. Examples: "PSOC-Sensor", "IFX-Gateway", "MyDevice"

### Q5: Enable Debug Logging?

> *Should BLE stack trace/debug output be enabled?*
>
> - **Yes** — BLE HCI traces and stack events are output for debugging. Helpful during development.
> - **No** — Silent operation. Appropriate for production.
>
> If yes, also answer Q5a:

### Q5a: Log Output Routing (when Q5 = yes)

> - **retarget-io (printf)** — Output via UART. If `retarget-io` is not in the project, the `mtb-retarget-io` skill will be invoked automatically.
> - **Segger RTT** — Output via SWD debug connection. No UART needed.

### Q6: Custom Connection Parameters (Central or Both only)

> *Do you need custom BLE connection parameters, or are defaults acceptable?*
>
> - **Defaults** — 30ms interval, 0 latency, 720ms timeout. Suitable for most applications.
> - **Custom** — Specify: connection interval (min/max in ms), slave latency, supervision timeout. Use for power-sensitive applications needing longer intervals or for throughput-sensitive applications needing shorter intervals.

---

## Library Installation

Delegate all installation to the `mtb-library-installer` skill. Check `deps/<name>.mtb` first; skip already-present libraries. After all `.mtb` files are placed, run `make getlibs` once via `run_make.py`.

### Required Libraries

| Library | Purpose |
|---|---|
| `btstack-integration` | BTSTACK protocol stack + platform HCI transport layer |

> ⛔ **Version resolution (MANDATORY):** Query the `mtb-list-available-libraries` tool for **EACH library independently** to get the correct version. Do NOT assume libraries share the same version numbering scheme. Always use `VersionTags[0]` from the manifest result for each library. Never hardcode or guess a version tag.
>
> **Floating tags prohibited:** Do NOT use `latest-*` tags in `.mtb` files. Resolve to the actual fixed release tag from the manifest. Floating tags cause unintended library updates on future `make getlibs` calls.
>
> **Dependency resolution:** The manifest includes transitive dependencies for each library. Trust the manifest's dependency list rather than hardcoding companion libraries. If the manifest indicates additional libraries are required (e.g., a FreeRTOS integration layer), install those as dependencies using the same per-library version resolution process.

### Prerequisite Libraries (delegate if missing)

| Library | Check | Delegate to |
|---|---|---|
| FreeRTOS | `deps/freertos.mtb` | `mtb-freertos` skill |
| retarget-io | `deps/retarget-io.mtb` (if Q5a = retarget-io) | `mtb-retarget-io` skill |

### Post-Installation Verification

After `make getlibs`, confirm `deps/btstack-integration.mtb` exists and `make getlibs` exited with code 0. Do NOT browse `../mtb_shared/` for verification — it is a workspace-level cache and not authoritative for project state.

---

## Makefile Configuration

All changes go to the project-root `Makefile` (or the sub-project that owns HCI transport access — typically CM33_NS for multi-core PSE84). Check for existing entries before adding.

### Detecting the BLE Component Name

The correct BLE COMPONENT name is determined by the BSP. Check the BSP's component directories:

```
# Look for BLE-related COMPONENT directories in the BSP
../mtb_shared/<bsp-name>/<ver>/COMPONENT_*BLE*
../mtb_shared/<bsp-name>/<ver>/COMPONENT_*BLUETOOTH*
```

Common BLE component names by platform:

| Platform Family | BLE Component | Notes |
|---|---|---|
| PSOC Edge (PSE84) + CYW55500 | `WICED_BLE` | Combo radio (WiFi + BLE) |
| PSOC 6 + CYW43xxx | `WICED_BLE` | Combo radio (WiFi + BLE) |
| CYW20829 / CYW89829 | `WICED_BLE` | Standalone BLE (no WiFi) |
| Older BSPs | Varies — check BSP | May use legacy names |

> **If unsure:** After `make getlibs`, inspect `../mtb_shared/btstack-integration/<ver>/` for `COMPONENT_*` directories. The directory name (minus `COMPONENT_` prefix) is what goes in your Makefile.

### Required COMPONENTS

```makefile
COMPONENTS+=FREERTOS <BLE_COMPONENT>
```

Replace `<BLE_COMPONENT>` with the value detected from the BSP (typically `WICED_BLE`).

### Required DEFINES

```makefile
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF
```

### Platform-Specific DEFINES

Check whether the BSP requires HCI baud rate configuration. Look for `CYBSP_BT_PLATFORM_CFG_BAUD_*` references in the BSP's `cybsp_bt_config.h` or the btstack-integration README:

```makefile
# Required for most AIROC combo-radio and standalone BLE chips:
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000

# Additional for combo-radio BSPs doing firmware download:
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_DOWNLOAD=3000000
```

> **How to verify:** After `make getlibs`, check `../mtb_shared/btstack-integration/<ver>/README.md` for your platform's required DEFINES. Not all platforms need baud rate configuration (e.g., chips with internal HCI may skip this).

If debug logging (Q5 = yes):
```makefile
DEFINES+=ENABLE_BT_SPY_LOG
```

---

## User Confirmation Pattern

Before installing anything or modifying any file, present a summary:

```
I'll set up BLE with the following configuration:

• Role: Peripheral (GATT Server)
• GATT Service: Custom "Temperature Sensor" — 1 read characteristic (temp value), 1 notify characteristic (temp updates)
• Security: Just Works
• Device name: "PSOC-TempSensor"
• Logging: Enabled via retarget-io
• Libraries: btstack-integration (v6.X series, pinned to resolved version), bluetooth-freertos (v6.X series, pinned)
• Makefile: COMPONENTS+=WICED_BLE, DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000

Shall I proceed?
```

---

## Execution Sequence (after confirmation)

1. **Detect BSP BLE configuration** — identify the correct COMPONENT name and any required DEFINES from the BSP
2. **Install libraries** via `mtb-library-installer`
3. **Run `make getlibs`** via `run_make.py` and verify
4. **Update Makefile** — COMPONENTS (using detected name), DEFINES (platform-specific)
5. **Generate `app_bt_gatt_db.c/h`** — see [gatt-db-patterns.md](./gatt-db-patterns.md)
6. **Generate `app_bt_cfg.c`** — stack configuration settings
7. **Generate `app_bt_init.c`** — platform init + stack init + role-specific code
8. **Update `main.c`** — create BLE FreeRTOS task (see critical rules below)
9. **Build verification** — `run_make.py --args build`

### main.c Rules (MANDATORY)

When generating or updating `main.c`:

- **All HAL objects and contexts that persist past `vTaskStartScheduler()` MUST be `static` or file-scope global.** This includes `mtb_hal_uart_t` objects passed to `cy_retarget_io_init()`, UART contexts, timer objects, and any struct whose pointer is stored by a library.
- **Rationale:** `vTaskStartScheduler()` never returns — FreeRTOS reclaims `main()`'s stack for the idle task. Stack-local objects in `main()` become dangling pointers, overwritten by task stack allocations. The resulting HardFault appears to be in an unrelated peripheral (e.g., `Cy_SCB_WriteTxFifo`) making diagnosis extremely difficult.
- **Disable tickless idle during initial BLE development (diagnostic step only):** If HCI timeouts persist after verifying init order and baud configuration, try `configUSE_TICKLESS_IDLE=0` in `FreeRTOSConfig.h`. The BT stack is designed to hold a sleep lock during active communication, so this should not normally be needed — but it can help isolate whether deep sleep is interfering with HCI firmware download.

---

## Combo-Radio Gate

Before proceeding with BLE-only setup, check:

1. Does the BSP include WiFi capability? (Check for `CYBSP_WIFI_CAPABLE` in BSP headers, or `deps/wifi-connection-manager.mtb`, or WiFi-related COMPONENTS like `LWIP`/`WCM` in the Makefile)
2. Did the user mention WiFi in their request?

If either is true:
> *This BSP has a combo radio that shares the radio between WiFi and BLE. The initialization order is critical — BLE must start after WiFi loads the shared radio firmware. I'll redirect to the `mtb-wifi-ble-coexistence` skill which handles this coordination.*

If BLE-only on a combo-radio BSP — this skill handles it correctly. The HCI transport initializes standalone when WiFi is not active.

If the BSP has a standalone BLE chip (no WiFi capability) — no combo-radio concern exists. Proceed normally.

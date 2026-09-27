# WiFi+BLE Coexistence — Detailed Workflow

## Scope Questions

> ⛔ **STOP. Do NOT install libraries, modify files, or run any command until ALL questions below have been explicitly answered by the user.**

Ask **all questions together in a single message**. These combine coexistence-specific questions with the essential WiFi and BLE scope items.

### Q1: Operational Model

> *How will WiFi and BLE be used together in your application?*
>
> - **BLE provisions WiFi** — BLE is used to receive WiFi credentials from a phone/tablet, then WiFi connects. BLE may remain active or shut down after provisioning completes. Common for headless IoT devices.
> - **Concurrent dual operation** — Both WiFi and BLE remain active simultaneously throughout the application lifetime. WiFi for cloud connectivity, BLE for local device interaction. Radio time is shared.
> - **BLE stops after WiFi connects** — BLE is used only during setup or configuration. Once WiFi is established, BLE advertising stops to free radio time for WiFi throughput.
> - **WiFi stops after data transfer** — WiFi connects periodically to upload data, then disconnects. BLE remains active for local communication between transfers.

### Q2: WiFi Operating Mode

> *Which WiFi mode?*
> - **Station (STA)** — Connect to existing network (most common)
> - **Soft Access Point (SoftAP)** — Device hosts its own network
> - **Concurrent (STA + SoftAP)** — Both simultaneously

### Q3: BLE Role

> *Which BLE role?*
> - **Peripheral (GATT Server)** — Device advertises and accepts connections
> - **Central (GATT Client)** — Device scans and connects to peripherals
> - **Both** — Advertise own services while connecting to others

### Q4: GATT Database

> *Describe the BLE services and characteristics needed.*
>
> For provisioning flows, a typical service includes:
> - WiFi SSID characteristic (Write)
> - WiFi Password characteristic (Write)
> - WiFi Status characteristic (Notify — reports connection result)
>
> Describe any additional services for your application.

### Q5: WiFi Security Type

> *What security does the target WiFi network use?*
> - WPA3, WPA2 AES, WPA2 Mixed, Open

### Q6: BLE Security Level

> - **None (open)** — No pairing required
> - **Just Works** — Encrypted, no MITM protection
> - **Passkey** — 6-digit PIN for MITM protection

### Q7: Device Name (BLE advertising)

> *Device name visible during BLE scanning? (max 29 bytes)*

### Q8: Enable Debug Logging?

> - **Yes** — WiFi + BLE diagnostic output (recommended during development)
> - **No** — Silent operation

### Q9: WiFi Credentials

> *SSID and password for the WiFi network.*
>
> If Q1 = "BLE provisions WiFi", credentials will be received over BLE at runtime — ask whether hardcoded fallback credentials are also desired for development.

---

## Combined Library Installation

Install all WiFi and BLE libraries together. Delegate to `mtb-library-installer` for each. Check `deps/<name>.mtb` first; skip already-present libraries.

### Required Libraries (always)

| Library | Purpose |
|---|---|
| `wifi-connection-manager` | WiFi management — loads combo radio firmware |
| `btstack-integration` | BLE protocol stack integration |
| `bluetooth-freertos` | FreeRTOS BLE task management |

> ⛔ **Version resolution (MANDATORY):** Query `mtb-list-available-libraries` for **EACH library independently**. Do NOT assume libraries share the same version numbering — each library has its own release cadence and tagging scheme. Use `VersionTags[0]` from each manifest entry. Never use `latest-*` floating tags in `.mtb` files.

### Conditional Libraries (per WiFi scope)

Follow `mtb-wifi-stack` patterns for:
- `lwip`, `lwip-freertos-integration`, `lwip-network-interface-integration` (when TCP/IP needed)
- `whd`, `whd-bsp-integration` (always for WiFi)
- `ifx-mbedtls` (when TLS needed)
- `secure-sockets` (when socket API needed)
- `connectivity-utilities` (when logging enabled)

> **Bundle option:** `wifi-core-freertos-lwip-mbedtls` can be used for the WiFi portion if LwIP + mbedTLS are both needed. BLE libraries must still be added individually.

> ⛔ **Bundle version by BSP radio (CRITICAL):** The wifi-core bundle has breaking version splits. See the `mtb-wifi-stack` skill [BSP-to-Bundle Version Compatibility table](../mtb-wifi-stack/references/workflow.md). Quick reference:
> - **CYW43012 / CYW4343W / CYW43439** (PSOC 6 boards) → `wifi-core-freertos-lwip-mbedtls` **v1.x** (v1.1.1)
> - **CYW55500 / CYW55573** (PSE84 boards) → `wifi-core-freertos-lwip-mbedtls` **v3.x** (v3.1.1)
>
> Using v3.x on a PSOC 6 board causes `whd_types.h` not found / missing COMPONENT_43012 errors.

### Post-Installation Verification

After `make getlibs`, confirm all required `.mtb` files exist in `deps/` and `make getlibs` exited with code 0. Do NOT browse `../mtb_shared/` for verification — it is a workspace-level cache and not authoritative for project state.

---

## Makefile Configuration

### Combined COMPONENTS

```makefile
COMPONENTS+=FREERTOS LWIP MBEDTLS SECURE_SOCKETS <BLE_COMPONENT>
```

Replace `<BLE_COMPONENT>` with the value detected from the BSP (typically `WICED_BLE`). See `mtb-ble-setup` workflow for BSP detection method.

> **Do NOT add `WCM` as a component** — `wifi-connection-manager` is a library, not a Makefile COMPONENT.

### Combined DEFINES

```makefile
DEFINES+=CYBSP_WIFI_CAPABLE
DEFINES+=CY_RTOS_AWARE
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF
```

Platform-specific DEFINES (check BSP):
```makefile
# HCI baud rate — required for most combo-radio BSPs:
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_DOWNLOAD=3000000
```

If mbedTLS included:
```makefile
DEFINES+=MBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"'
DEFINES+=MBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
DEFINES+=MBEDTLS_PSA_CRYPTO_CONFIG_FILE='"ifx_psa_crypto_config.h"'
```

If logging enabled:
```makefile
DEFINES+=ENABLE_WCM_LOGS
DEFINES+=ENABLE_BT_SPY_LOG
```

---

## Memory Budget Verification

Combo WiFi+BLE operation requires significantly more memory than either stack alone.

### FreeRTOS Heap

Check `FreeRTOSConfig.h` for `configTOTAL_HEAP_SIZE`:

| Configuration | Minimum Heap |
|---|---|
| WiFi + BLE (no TLS) | 96 KB |
| WiFi + BLE + TLS (MQTT/HTTPS) | 128 KB |
| WiFi + BLE + TLS + application buffers | 160 KB+ |

If current value is below minimum:
```c
/* FreeRTOSConfig.h */
#define configTOTAL_HEAP_SIZE    ((size_t)(128 * 1024))
```

> **Failure mode:** `pvPortMalloc()` returns NULL during TLS handshake → mbedTLS dereferences NULL → HardFault. Non-obvious; appears as random crash during WiFi connect.

### SRAM Partition (Multi-Core PSE84)

For PSE84, verify CM33 data SRAM allocation via Device Configurator:

| Usage | Minimum SRAM |
|---|---|
| WiFi only | 256 KB (default, borderline) |
| WiFi + BLE | 384 KB |
| WiFi + BLE + TLS + large buffers | 512 KB |

If SRAM is insufficient, WiFi buffer allocation fails intermittently under load.

---

## Multi-Core Considerations (PSE84)

CM33_NS is the recommended and supported core for both WiFi and BLE. Both stacks run on the same core, coordinated by FreeRTOS tasks.

The CM55 core can run application logic (ML inference, signal processing) and communicate with the WiFi/BLE tasks via FreeRTOS queues or shared memory — but the connectivity stacks themselves must remain on CM33_NS.

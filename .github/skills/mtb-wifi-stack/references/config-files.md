# WiFi Stack — Configuration Files and Makefile

> ⛔ **NEVER look outside the current project directory for any config file.** Do not read, copy, or reference files from `../mtb_shared/`, other project directories, or any path above the project root. Other projects' config files may be corrupted, outdated, or tuned for incompatible hardware. **ALL config files MUST come exclusively from this skill's own `resources/` directory.** Use `resources/lwipopts.h`, `resources/mbedtls_user_config.h`, `resources/ifx_psa_crypto_config.h`, `resources/ifx_psa_mxcrypto_config.h`, and `resources/ifx_tfm_config.h`. No exceptions.

## Makefile Integration

All changes go to the project-root `Makefile` (or the NS sub-project `Makefile` for multi-core PSE84 projects). Check for existing entries before adding — do not create duplicates.

### Required COMPONENTS

```makefile
COMPONENTS+=FREERTOS
COMPONENTS+=LWIP            # if user selected LwIP (Q2)
COMPONENTS+=MBEDTLS         # if user selected mbedTLS (Q3)
COMPONENTS+=SECURE_SOCKETS  # if user selected secure-sockets (Q4)
```

`FREERTOS` is always required (it is a prerequisite, not installed by this skill, but must be present in COMPONENTS).

### Required DEFINES

```makefile
DEFINES+=CYBSP_WIFI_CAPABLE
DEFINES+=CY_RTOS_AWARE
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF
```

If mbedTLS was included (Q3 = yes):
```makefile
# Path prefix must match where config files are placed:
#   Project root (default):  '"mbedtls_user_config.h"'
#   configs/ subdirectory:   '"configs/mbedtls_user_config.h"'
DEFINES+=MBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"'
DEFINES+=MBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
DEFINES+=MBEDTLS_PSA_CRYPTO_CONFIG_FILE='"ifx_psa_crypto_config.h"'
```

- `CYBSP_WIFI_CAPABLE` — enables WiFi hardware support in the BSP
- `CY_RTOS_AWARE` — tells the HAL layer that an RTOS is running; enables RTOS-safe resource management
- `CY_RETARGET_IO_CONVERT_LF_TO_CRLF` — converts `\n` to `\r\n` in printf output; required for correct display in most serial terminals
- `MBEDTLS_USER_CONFIG_FILE` — directs mbedTLS to use the project-level config header
- `MBEDTLS_CONFIG_FILE` — specifies the base mbedTLS config; must point to `mbedtls/mbedtls_config.h` from the installed library
- `MBEDTLS_PSA_CRYPTO_CONFIG_FILE` — points PSA crypto to the Infineon hardware acceleration config (`ifx_psa_crypto_config.h`). Without this, the bundled config file is never picked up and hardware crypto acceleration is disabled.

### Logging: Compile-Time DEFINES (Layer 1)

Connectivity logging is controlled by **two independent layers**. These Makefile DEFINES are **Layer 1** — they gate whether the middleware emits log calls into the binary at all. Add only if the user selected logging (Q5):

| DEFINE | What it gates |
|---|---|
| `ENABLE_WCM_LOGS` | WiFi Connection Manager — scan, connect, event, and error messages |
| `ENABLE_CONNECTIVITY_MIDDLEWARE_LOGS` | Network interface and WHD driver-level messages (very verbose at DEBUG levels) |
| `ENABLE_SECURE_SOCKETS_LOGS` | Socket open/close and TLS handshake messages |

> **Important:** These DEFINES must be present in the Makefile or `cy_log_init()` produces no connectivity output — the middleware log call sites are compiled out entirely. Setting a runtime log level without these DEFINES has no effect on middleware output.

**Layer 2** is the runtime log level and output routing configured via `cy_log_init()` in the WiFi task. See [code-patterns.md §Logging Setup](./code-patterns.md#logging-setup).

### WPS

If user enabled WPS (Q6):
```makefile
COMPONENTS+=WPS
```
`MBEDTLS` must also be in `COMPONENTS`.

---

## Config File Placement

Config files may be placed either in the **project root** (default) or in a **`configs/` subdirectory**. Both work; the `configs/` layout is cleaner for projects with many source files and is consistent with Infineon example applications.

| Placement | Example path | DEFINES path prefix |
|---|---|---|
| Project root (default) | `mbedtls_user_config.h` | `'"mbedtls_user_config.h"'` |
| configs/ subdirectory | `configs/mbedtls_user_config.h` | `'"configs/mbedtls_user_config.h"'` |

**Choose placement upfront and apply it consistently to all five config files and all three DEFINES (`MBEDTLS_USER_CONFIG_FILE`, `MBEDTLS_CONFIG_FILE`, `MBEDTLS_PSA_CRYPTO_CONFIG_FILE`).** If the user has a preference, use it; otherwise default to project root for single-project layouts and `configs/` for multi-project (PSE84) layouts.

---

## lwipopts.h

LwIP stack configuration file. Use the bundled version from the skill's `resources/lwipopts.h` — do not search for a copy in the installed library sources. The bundled file is sourced from `wifi-core-freertos-lwip-mbedtls` and uses WHD-aware constants (`WHD_PHYSICAL_HEADER`, `WHD_PAYLOAD_MTU`) for `PBUF_LINK_HLEN` and `TCP_MSS`, which hardcoded versions get wrong.

**Required action:** Copy `resources/lwipopts.h` to the chosen placement directory (project root or `configs/`). Do not modify the bundled source.

**Key tunables to inform the user about after copying:**

| Setting | Default | Effect |
|---|---|---|
| `MEM_SIZE` | Varies | LwIP heap size; increase if multiple simultaneous connections are needed (DCache platforms only — software heap on others) |
| `LWIP_AUTOIP` | `0` (commented out) | Set to `1` to enable Auto-IP fallback when DHCP is unavailable (per Q9) |
| `LWIP_DHCP_AUTOIP_COOP` | `0` (commented out) | Set to `1` to enable DHCP + Auto-IP cooperation |

If user selected AUTOIP (Q9 = yes), uncomment `LWIP_AUTOIP` and `LWIP_DHCP_AUTOIP_COOP` in the copied file.

---

## mbedtls_user_config.h

mbedTLS user configuration override file. Use the bundled version from the skill's `resources/mbedtls_user_config.h` — do not search the installed library sources. The bundled file is sourced from `wifi-core-freertos-lwip-mbedtls` and includes the PSE84 re-enable patch (see [PSE84 mbedTLS Patch](#pse84-mbedtls-patch) below).

**Required action:** Copy `resources/mbedtls_user_config.h` to the chosen placement directory. Update `MBEDTLS_USER_CONFIG_FILE` in the Makefile if using `configs/` placement.

**Optional code-size reductions — present to user as choices, never apply automatically:**

| Setting in mbedtls_user_config.h | Code Saved | Trade-off |
|---|---|---|
| `#define MBEDTLS_X509_REMOVE_INFO` | ~8 KB | Removes certificate info and debug print functions |
| `#undef MBEDTLS_ARIA_C` | ~6 KB | Disables ARIA cipher (rarely used) |
| `#undef MBEDTLS_CAMELLIA_C` | ~7 KB | Disables Camellia cipher (rarely used) |
| `#undef MBEDTLS_SSL_SRV_C` | ~16 KB | Disables TLS server — **only** safe if device is always a TLS client |
| `#undef MBEDTLS_SSL_CLI_C` | ~14 KB | Disables TLS client — **only** safe if device is always a TLS server |

Only suggest the last two when the operating mode clearly implies a single direction. Never apply silently — they will break TLS if misapplied.

---

## ifx_psa_crypto_config.h

PSA crypto algorithm configuration file. Controls which algorithms the hardware-accelerated PSA crypto engine exposes. Required when mbedTLS (Q3 = yes) is used on a device with an MX crypto engine.

**Required action:** Copy `resources/ifx_psa_crypto_config.h` to the chosen placement directory. Update `MBEDTLS_PSA_CRYPTO_CONFIG_FILE` in the Makefile to include the path prefix if using `configs/` placement.

---

## ifx_psa_mxcrypto_config.h

PSA MXCrypto driver configuration. Maps PSA algorithm wants to specific Infineon MXCrypto driver operations. Only active when `CY_IP_MXCRYPTO` is defined in the BSP.

**Required action:** Copy `resources/ifx_psa_mxcrypto_config.h` to the chosen placement directory silently.

---

## ifx_tfm_config.h

TFM (Trusted Firmware-M) crypto engine buffer configuration. Sets `CRYPTO_IOVEC_BUFFER_SIZE=18000` and `CRYPTO_ENGINE_BUF_SIZE=0x5080` to ensure TFM crypto operations have sufficient working memory. Required for PSE84 (CY8C6) TFM-based targets.

**Required action:** Copy `resources/ifx_tfm_config.h` to the chosen placement directory silently.

---

## PSE84 mbedTLS Patch

The PSE84 BSP auto-selects `pse84_mbedtls_config.h`, which disables three macros needed by `secure-sockets`:

| Disabled macro | Effect when absent |
|---|---|
| `MBEDTLS_HAVE_TIME` | TLS handshake timestamps unavailable; `secure-sockets` compile errors |
| `MBEDTLS_SSL_MAX_FRAGMENT_LENGTH` | TLS maximum fragment length negotiation unavailable |
| `MBEDTLS_SSL_ALPN` | ALPN extension unavailable; required for HTTP/2 and some cloud connections |

These are **re-enabled in the bundled `mbedtls_user_config.h`** via a conditional block at the end of the file:

```c
#if defined(COMPONENT_PSE84) || defined(COMPONENT_CY8C6)
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_ALPN
#endif
```

If a user reports TLS compile errors on PSE84 despite having `mbedtls_user_config.h` in the project, verify this block is present. If it is absent (e.g., the user has an older version of the file), add it manually.

---

## wifi_credentials.h

Create this file at the project root (or NS sub-project root). It contains sensitive credentials and must **never be committed to source control**.

1. Add `wifi_credentials.h` to `.gitignore` if a `.gitignore` exists in the project.
2. Ask the user for their network credentials before creating the file.

### STA Mode Template

```c
/* wifi_credentials.h — DO NOT COMMIT TO SOURCE CONTROL */
#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#define WIFI_SSID       "your-network-name"
#define WIFI_PASSWORD   "your-password"
#define WIFI_SECURITY   CY_WCM_SECURITY_WPA2_AES_PSK

#endif /* WIFI_CREDENTIALS_H */
```

### SoftAP Mode Template

```c
/* wifi_credentials.h — DO NOT COMMIT TO SOURCE CONTROL */
#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#define SOFTAP_SSID      "MyDevice-AP"
#define SOFTAP_PASSWORD  "12345678"
#define SOFTAP_SECURITY  CY_WCM_SECURITY_WPA2_AES_PSK
#define SOFTAP_CHANNEL   6

#endif /* WIFI_CREDENTIALS_H */
```

For Concurrent mode, include both STA and SoftAP blocks.

### Security Type Options

Ask the user which security type to use and fill `WIFI_SECURITY` / `SOFTAP_SECURITY` accordingly:

| Option | `cy_wcm_security_t` value | When to use |
|---|---|---|
| WPA3 Personal | `CY_WCM_SECURITY_WPA3_SAE` | Most secure; requires WPA3-capable AP |
| WPA2 AES | `CY_WCM_SECURITY_WPA2_AES_PSK` | Recommended for most deployments |
| WPA2 Mixed (AES+TKIP) | `CY_WCM_SECURITY_WPA2_MIXED_PSK` | Compatibility with older access points |
| Open (no password) | `CY_WCM_SECURITY_OPEN` | Isolated test networks only |

For SoftAP channel, recommend channel 1, 6, or 11 (non-overlapping 2.4 GHz channels). Valid range is 1–13.

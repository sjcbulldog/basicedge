# WiFi Stack — Detailed Workflow

## Scope Questions

> ⛔ **STOP. Do NOT install libraries, modify files, or run any command until ALL questions below have been explicitly answered by the user in the current session.**
>
> **ALL 9 questions must be asked, every time, without exception:**
> - [ ] Q1 — WiFi operating mode
> - [ ] Q2 — Include LwIP?
> - [ ] Q3 — Include mbedTLS?
> - [ ] Q4 — Include secure-sockets?
> - [ ] Q5 — Enable debug logging?
> - [ ] Q5a — Log level *(mandatory when Q5 = yes)*
> - [ ] Q5b — Log output routing *(mandatory when Q5 = yes)*
> - [ ] Q6 — Enable WPS?
> - [ ] Q7 — WiFi credentials (SSID, password, security type)
> - [ ] Q8 — Application-layer reconnection task?
> - [ ] Q9 — AUTOIP fallback?
>
> **Do NOT infer, assume, or pre-fill any answer** from installed libraries, project file state, previous sessions, or any other signal. Every answer must come from the user's own words in this session. If the user provides partial information upfront, ask for the remaining answers before proceeding.

Ask **all questions together in a single message** as a numbered list. Include Q5a and Q5b inline under Q5 (noting they are required when Q5 = yes), and note the conditions for Q6 (STA/Concurrent only) and Q9 (STA + LwIP only) inline. Wait for the user to answer all questions before proceeding.

### Q1: WiFi Operating Mode

> *Which WiFi operating mode does your application need?*
>
> - **Station (STA)** — Connect to an existing WiFi network. The most common mode for IoT devices that communicate with infrastructure or the internet.
> - **Soft Access Point (SoftAP)** — The device hosts its own WiFi network; other devices connect to it. Useful for local control, headless provisioning, or offline operation.
> - **Concurrent (STA + SoftAP simultaneously)** — Both roles active at once. Useful for provisioning flows where the device serves a configuration interface while also connecting to the target network.

### Q2: Include LwIP (TCP/IP stack)?

> *LwIP is a lightweight TCP/IP stack for embedded systems. It provides IP addressing, TCP, UDP, DHCP, DNS, and socket APIs — everything needed for network communication above the WiFi radio layer. Without it, only raw WiFi association is possible; there is no ability to send or receive data using standard protocols.*
>
> - **Yes, include LwIP** — Required for all socket communication, HTTP, MQTT, or any standard network protocol. *(Strongly recommended.)*
> - **No** — Only if direct low-level access to the WiFi Host Driver (WHD) is all that's needed. Ask for explicit confirmation before proceeding without LwIP.

### Q3: Include mbedTLS (TLS/cryptography)?

> *mbedTLS provides encrypted (TLS/SSL) connections, certificate-based authentication, and hardware-accelerated cryptographic operations. Without it, all socket communication is unencrypted plaintext. Required for HTTPS, MQTTS, and any connection to cloud services or the internet.*
>
> - **Yes, include mbedTLS** — Required for secure communications. Recommended for any internet-facing application.
> - **No — unencrypted communications only** — Appropriate only for isolated local networks or early prototyping. Not suitable for production.

### Q4: Include secure-sockets?

> *The secure-sockets library provides a BSD socket-like API layered on top of LwIP and mbedTLS. It abstracts low-level stack details and is required by most higher-level middleware (MQTT, HTTP client, etc.).*
>
> - **Yes, include secure-sockets** — Recommended whenever any application-layer protocol will be used.
> - **No — use LwIP APIs directly** — Advanced only; the application manages sockets through LwIP's native API.

> **Note:** `secure-sockets` is often a transitive dependency of `wifi-connection-manager`. After `make getlibs`, check whether it arrived automatically before concluding it needs explicit installation.

### Q5: Enable debug logging?

> *Connectivity debug logging is off by default. Enabling it produces diagnostic output for WiFi association, IP address acquisition, TLS handshakes, and socket operations. Logging is controlled by two layers: compile-time DEFINES that gate whether middleware emits log calls at all, and a runtime log level set via `cy_log_init()` that controls how much output appears. Both layers must be configured for logging to work.*
>
> - **Yes, enable connectivity logs** — Recommended during development and bringup.
> - **No — keep logs off** — Appropriate for production builds or when output is not needed.

If yes, **you MUST ask Q5a and Q5b** before proceeding to code generation — log level and output routing are mandatory inputs for generating correct code.

### Q5a: Log level (MANDATORY when Q5 = yes)

> *Which log level should the connectivity stack use at startup? This sets the verbosity for all WiFi middleware subsystems.*
>
> - **`CY_LOG_ERR`** — Errors only. Minimal output; appropriate for near-production or stable code.
> - **`CY_LOG_WARNING`** — Errors and warnings.
> - **`CY_LOG_INFO`** *(Recommended for development)* — Key lifecycle events: connection attempts, IP address assignments, TLS handshakes, disconnects.
> - **`CY_LOG_DEBUG`** — Detailed per-step output. Use when diagnosing a specific connection or protocol problem.
> - **`CY_LOG_DEBUG1` – `CY_LOG_DEBUG4`** — Progressively deeper trace. May produce hundreds of lines per second; use only for low-level radio or WHD firmware investigation.

Per-facility overrides (e.g., silence WHD driver output while keeping WCM at INFO) are generated automatically in the code. See [code-patterns.md §Logging Setup](./code-patterns.md#logging-setup).

### Q5b: Log output routing

> *Where should log output be sent?*
>
> - **`retarget-io` (stdio / printf)** *(Recommended)* — Log output goes through `printf()`. Requires `retarget-io` to route `printf` to a UART or USB-CDC terminal. If `retarget-io` is not yet in the project, the `mtb-retarget-io` skill will be invoked before code generation.
> - **Segger RTT** — Log output goes to the J-Link RTT Viewer. No UART required; uses the SWD debug connection. Suitable when no physical UART pin is available. Requires the Segger RTT library.
> - **Custom callback** — You supply a `log_output` callback. The library calls it with the pre-formatted message string; you decide where it goes. Useful for ring buffers, remote syslog, or any non-UART destination.

After Q5b, if the user selected `retarget-io`: check `deps/retarget-io.mtb`. If absent, **always invoke the `mtb-retarget-io` skill automatically** — do not ask the user whether they want it set up. Proceed with retarget-io installation before continuing to code generation.

### Q6: Enable WPS (WiFi Protected Setup)?

*Ask only if operating mode is STA or Concurrent.*

> *WPS allows the device to join an access point by push-button or PIN — without hardcoding credentials in firmware. Requires mbedTLS.*
>
> - **No — use credential-based connection** — SSID and password defined at build time or supplied at runtime. Simpler and more common.
> - **Yes — enable WPS enrollee** — The device can join a network via WPS push-button or PIN.

### Q7: WiFi credentials

Ask for credentials based on the selected operating mode. These will be used to generate `wifi_credentials.h` in a later step.

**STA mode (or Concurrent STA side):**
> *What SSID and password should the device connect to? What security type does that network use? (WPA3 / WPA2 AES / WPA2 Mixed / Open)*

**SoftAP mode (or Concurrent AP side):**
> *What SSID and password should the device's access point broadcast? Which channel? (Recommended: 1, 6, or 11 for 2.4 GHz; valid range 1–13)*

See [config-files.md §wifi_credentials.h](./config-files.md#wifi_credentialsh) for the security type options table.

### Q8: Application-layer reconnection task?

> *WCM automatically restores the WiFi radio link when it drops. However, application-layer sessions (MQTT, HTTP connections) must be re-established manually after a reconnect. Would you like a reconnection monitoring task that waits for link recovery and signals your application to restart those sessions?*
>
> - **Yes — generate a reconnection task** — A `network_monitor_task` is generated and wired to the WCM disconnect event.
> - **No — I will handle session restart in my own code** — Only the WCM event callback is generated; no monitor task.

### Q9: Enable AUTOIP fallback?

*Ask only if operating mode includes STA and LwIP is included.*

> *AUTOIP (RFC 3927) provides link-local addressing (169.254.x.x) when DHCP is unavailable. Useful for devices that must maintain local connectivity without an infrastructure router.*
>
> - **No — DHCP only** *(Recommended)* — DHCP client active; device waits for an address from a DHCP server. Standard for infrastructure networks.
> - **Yes — AUTOIP fallback** — If DHCP times out, device assigns itself a 169.254.x.x address. Enable `LWIP_AUTOIP=1` and optionally `LWIP_DHCP_AUTOIP_COOP=1` in `lwipopts.h`.

---

## Library Installation

> **⚠️ IMPORTANT — Complete ALL Makefile changes before the first build attempt.** Do not run `make build` until `COMPONENTS`, `DEFINES`, `MBEDTLS_USER_CONFIG_FILE`, and all other Makefile entries are in place. Incremental builds after partial Makefile state cause confusing errors (missing headers, undefined symbols) that waste time.

Delegate all installation to the `mtb-library-installer` skill. For each library, check `deps/<library-name>.mtb` first; skip any already present. After all `.mtb` files are in place, run `make getlibs` once and verify exit code 0.

### Required

| Library | Purpose |
|---|---|
| `wifi-connection-manager` | Core WiFi library. Provides the WCM API (scan, connect, host AP, event callbacks, auto-reconnect). Pulls in the WiFi Host Driver (WHD), RTOS abstraction layer, and platform-specific connectivity components transitively. |

### Conditional on Scope

| Library                            | Install When       | Why                                                                                                       |
| ---------------------------------- | ------------------ | --------------------------------------------------------------------------------------------------------- |
| `lwip`                             | Q2 = yes           | TCP/IP stack; enables IP-layer networking above the WiFi radio                                            |
| `lwip-freertos-integration`        | Q2 = yes           | FreeRTOS port layer for LwIP; provides `arch/cc.h` and related headers required at compile time           |
| `lwip-network-interface-integration` | Q2 = yes         | Integration layer linking LwIP to the WHD; required for actual packet I/O between LwIP and the WiFi radio |
| `whd`                              | Always (explicit)  | WiFi Host Driver; provides `whd_types.h`. May arrive transitively, but always install explicitly to guarantee availability |
| `whd-bsp-integration`              | Always (mandatory) | BSP-level integration of WHD; **mandatory for all WiFi use in ModusToolbox** regardless of device family  |
| `ifx-mbedtls`                      | Q3 = yes           | Infineon's mbedTLS variant; provides TLS/cryptography. **Always use `ifx-mbedtls`** — do not install the upstream ARM `mbedtls` directly |
| `secure-sockets`                   | Q4 = yes           | Socket abstraction API; required by mqtt, http-client, and similar middleware                             |
| `connectivity-utilities`           | Q5 = yes (logging) | Provides `cy_log_init()` needed to activate middleware debug output                                       |

### Alternative: `wifi-core-freertos-lwip-mbedtls` Bundle

When Q2 = yes **and** Q3 = yes, the `wifi-core-freertos-lwip-mbedtls` meta-package installs all TCP/IP and TLS dependencies in a single step and guarantees version compatibility between the libraries:

```
wifi-core-freertos-lwip-mbedtls
```

This bundle pulls in: `wifi-connection-manager`, `lwip`, `lwip-freertos-integration`, `lwip-network-interface-integration`, `whd`, `whd-bsp-integration`, `ifx-mbedtls`, and their transitive dependencies. It is the approach used in all official Infineon WiFi examples (including PSE84).

**When to use the bundle vs individual libraries:**
- Use the bundle when starting fresh with LwIP + mbedTLS — it is the Infineon-recommended path
- Use individual libraries when LwIP or mbedTLS is not needed (WiFi-only, no TLS), or when version pinning of individual components is required

If the bundle is chosen, `secure-sockets` and `connectivity-utilities` still need to be installed individually if Q4/Q5 are yes.

#### ⛔ BSP-to-Bundle Version Compatibility

The `wifi-core-freertos-lwip-mbedtls` bundle has **breaking version splits by device family**. Using the wrong major version causes missing COMPONENT errors (`whd_types.h` not found, no `COMPONENT_43012`, etc.).

| BSP Radio Chip | wifi-core-freertos-lwip-mbedtls | wifi-host-driver (transitive) | Notes |
|---|---|---|---|
| CYW43012 (CY8CKIT-062S2-43012) | **v1.x** (use v1.1.1) | v4.x (WIFI5) | PSOC 6 combo boards |
| CYW4343W (CY8CKIT-062-WIFI-BT) | **v1.x** (use v1.1.1) | v4.x (WIFI5) | PSOC 6 combo boards |
| CYW43439 (CY8CPROTO-062S3-4343W) | **v1.x** (use v1.1.1) | v4.x (WIFI5) | PSOC 6 combo boards |
| CYW55500 (KIT_PSE84_AI) | **v3.x** (use v3.1.1) | v5.x (WIFI6) | PSOC Edge E84 only |
| CYW55573 (KIT_PSE84_EVAL) | **v3.x** (use v3.1.1) | v5.x (WIFI6) | PSOC Edge E84 only |

**How to determine the radio chip:** Check the BSP's README or `cybsp_types.h` for `COMPONENT_WIFI5` (→ v1.x) or `COMPONENT_WIFI6` (→ v3.x). Alternatively, the board part number suffix indicates the radio (e.g., `-43012`, `-4343W`).

> ⚠️ **v2.x does not exist as a release.** The jump from v1.x to v3.x is intentional — v3.x dropped all PSOC 6/WIFI5 support to add PSE84/WIFI6 support.

**If the wrong version is installed:** Delete the `.mtb` file, remove `../mtb_shared/wifi-core-freertos-lwip-mbedtls/<wrong-version>/`, create a new `.mtb` with the correct version, and re-run `make getlibs`.

> **Post-`make getlibs` verification:** Confirm each expected library directory exists under `../mtb_shared/`. Do not assume any library arrived transitively — verify the directory is present before proceeding to code generation.

### WPS

If Q6 = yes: add `COMPONENTS+=WPS` to the Makefile after library installation. Confirm `MBEDTLS` is also in `COMPONENTS`.

---

## User Confirmation Pattern

Before installing anything or modifying any file, present a complete summary of all collected answers and ask for confirmation:

```
I'll set up the WiFi stack with the following configuration:

- Mode: <STA / SoftAP / Concurrent>
- Libraries to install: wifi-connection-manager, lwip, lwip-freertos-integration,
  lwip-network-interface-integration, whd, whd-bsp-integration[, ifx-mbedtls,
  secure-sockets, connectivity-utilities — as applicable]
  [Alternative bundle: wifi-core-freertos-lwip-mbedtls (if Q2+Q3 both yes)]
- Config files: lwipopts.h, mbedtls_user_config.h, ifx_psa_crypto_config.h,
  ifx_psa_mxcrypto_config.h, ifx_tfm_config.h
  → Placement: <project root / configs/ subdirectory>
- Makefile COMPONENTS: FREERTOS[, LWIP][, MBEDTLS][, SECURE_SOCKETS]
- Makefile DEFINES: CYBSP_WIFI_CAPABLE CY_RTOS_AWARE CY_RETARGET_IO_CONVERT_LF_TO_CRLF
  [+ MBEDTLS_USER_CONFIG_FILE, MBEDTLS_CONFIG_FILE, MBEDTLS_PSA_CRYPTO_CONFIG_FILE if Q3]
- Credentials: wifi_credentials.h created for SSID '<ssid>' (added to .gitignore)
- Logging: <off / level=INFO via retarget-io / level=DEBUG via RTT / etc.>
- AUTOIP fallback: <enabled / disabled>
- Reconnection task: <yes / no>
- WPS: <enabled / disabled>

Shall I proceed? <Await user response>
```

---

## Post-Install Verification

After `make getlibs` completes:

1. Confirm `deps/wifi-connection-manager.mtb` exists.
2. Confirm `../mtb_shared/wifi-connection-manager/` is populated.
3. Repeat for each conditionally installed library: `lwip`, `lwip-freertos-integration`, `lwip-network-interface-integration`, `whd`, `whd-bsp-integration`, `ifx-mbedtls`, `secure-sockets`, `connectivity-utilities` — as applicable to scope selections.
4. For the config file copy step: use the 5 bundled files from `resources/` — do not search for copies in `../mtb_shared/`. Copy `lwipopts.h`, `mbedtls_user_config.h`, `ifx_psa_crypto_config.h`, `ifx_psa_mxcrypto_config.h`, and `ifx_tfm_config.h` to the chosen placement directory (project root or `configs/` subdirectory — must match the path prefixes used in the Makefile DEFINES).

If any library directory check fails, do not proceed to code generation — report the issue and retry `make getlibs` once before escalating to the user.

---
name: mtb-wifi-stack
description: Set up the WiFi connectivity stack in a ModusToolbox project, including library installation, Makefile configuration, and initialization code. Use when asked to "add WiFi", "connect to a WiFi network", "set up an access point", "enable networking", or "initialize the WiFi stack". Also invoked by higher-level skills (mqtt, http-client) that require a working WiFi connection as a prerequisite. Supports Station (STA), Soft Access Point (SoftAP), and Concurrent (STA+SoftAP) modes.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# wifi-stack

Integrate the Infineon WiFi connectivity stack into a ModusToolbox project. This skill installs the required libraries, copies mandatory configuration files, updates the project Makefile, and generates initialization code for Station (STA), Soft Access Point (SoftAP), or Concurrent mode.

This is a **library-tier skill**. It orchestrates:
1. Scope clarification with the user (operating mode, optional libraries, logging)
2. Library installation via the `mtb-library-installer` skill
3. Configuration file setup (`lwipopts.h`, `mbedtls_user_config.h`, `wifi_credentials.h`)
4. Makefile updates (`COMPONENTS`, `DEFINES`)
5. All code changes needed to initialize, connect, and monitor the WiFi connection

## When to Use This Skill

- User says: "add WiFi", "connect to a network", "set up a WiFi access point", "enable networking", "initialize the WiFi stack", "I need WiFi connectivity"
- A higher-level skill (`mqtt`, `http-client`) has been invoked and requires WiFi as a prerequisite
- WiFi libraries are already installed but initialization code is missing or incomplete

### When Not to Use This Skill

- The project's BSP does not include a WiFi-capable WLAN module — warn the user instead
- **BLE is also planned** — use the `mtb-wifi-ble-coexistence` skill instead, which orchestrates both WiFi and BLE with the critical initialization ordering required for combo-radio BSPs
- FreeRTOS is not present and the user has not agreed to add it — direct the user to the `mtb-freertos` skill first
- The user only needs to modify WiFi credentials or change operating mode on an already-integrated project — make targeted edits instead of re-running the full workflow

## Prerequisites

- A ModusToolbox project with a valid `Makefile` at the project root
- `make` available in the project
- **FreeRTOS must be installed.** Check `deps/freertos.mtb` and `COMPONENTS` in the Makefile for `FREERTOS`. If absent, stop and direct the user to the `mtb-freertos` skill before continuing:

  > *WiFi requires FreeRTOS, which is not detected in this project. Please use the `mtb-freertos` skill to add it first, then return to WiFi setup.*

- This skill is pre-installed at `.github/skills/mtb-wifi-stack/` by the Infineon ModusToolbox AI Assistant extension

## Key Files

| File / Directory | Purpose |
|---|---|
| `deps/wifi-connection-manager.mtb` | Presence confirms WCM library is installed |
| `deps/lwip.mtb` | Presence confirms LwIP library is installed |
| `deps/lwip-freertos-integration.mtb` | Presence confirms LwIP FreeRTOS port layer is installed (provides `arch/cc.h`) |
| `deps/lwip-network-interface-integration.mtb` | Presence confirms LwIP netif integration layer is installed |
| `deps/whd.mtb` | Presence confirms WiFi Host Driver is installed (provides `whd_types.h`) |
| `deps/whd-bsp-integration.mtb` | Presence confirms WHD BSP integration layer is installed |
| `deps/ifx-mbedtls.mtb` | Presence confirms Infineon mbedTLS library is installed |
| `deps/secure-sockets.mtb` | Presence confirms secure-sockets library is installed |
| `../mtb_shared/<library>/<version>/` | Library source fetched by `make getlibs` |
| `lwipopts.h` *(project root)* | Project-level LwIP configuration |
| `mbedtls_user_config.h` *(project root)* | Project-level mbedTLS configuration |
| `ifx_psa_crypto_config.h` *(project root)* | PSA crypto algorithm configuration for hardware acceleration |
| `ifx_psa_mxcrypto_config.h` *(project root)* | PSA MXCrypto driver configuration (active only when `CY_IP_MXCRYPTO` is defined) |
| `ifx_tfm_config.h` *(project root)* | TFM crypto buffer sizes (`CRYPTO_IOVEC_BUFFER_SIZE`, `CRYPTO_ENGINE_BUF_SIZE`) |
| `wifi_credentials.h` *(project root)* | WiFi credentials — **never commit to source control** |
| `Makefile` | `COMPONENTS` and `DEFINES` entries managed by this skill |

**Multi-core projects (PSE84):** The WiFi stack is core-agnostic and can run on either CM33_NS or CM55. Most code examples target CM33_NS; however, the official `mtb-tester-psoc-edge-wifi-bluetooth` runs the full WiFi stack on CM55. Target the sub-project for whichever core your application designates for connectivity. Replace "project root" above with that sub-project root throughout.

## Step-by-Step Workflow

> ⛔ **MANDATORY: Ask ALL 9 scope questions BEFORE doing anything else.** Do not install libraries, modify files, or run any command until all 9 questions below have been explicitly answered by the user in the current session. Do not infer, assume, or pre-fill any answer from the project state, from previously installed libraries, or from any other source. Every answer must come from the user's own words. No exceptions.
>
> The 9 required questions are: **Q1** (operating mode), **Q2** (LwIP), **Q3** (mbedTLS), **Q4** (secure-sockets), **Q5** (logging enable), **Q5a** (log level — mandatory when Q5=yes), **Q5b** (log output routing — mandatory when Q5=yes), **Q6** (WPS), **Q7** (WiFi credentials), **Q8** (reconnection task), **Q9** (AUTOIP fallback). Each must be explicitly confirmed by the user before moving forward.

> ⛔ **MANDATORY: NEVER look outside the current project directory for any file.** Do not read, copy, reference, or use as inspiration any file from `../mtb_shared/`, another project directory, or any path above the current project root. Config files from other projects may be corrupted, outdated, or tuned for different hardware. **All config files MUST come exclusively from this skill's own `assets/` directory.** This rule has no exceptions.

See [workflow.md](./references/workflow.md) for the complete execution sequence. High-level steps:

1. **Check prerequisites** — verify FreeRTOS is present; check for BSP WiFi hardware indicators. Stop with clear guidance if either check fails.
2. **Check for existing installation** — scan `deps/` for already-present WiFi library `.mtb` files only. Note which are present; use this only to avoid reinstalling libraries that already exist. Do **not** use existing installation state to infer or skip any scope question.
3. **Step 0 — Ask ALL 9 scope questions (REQUIRED before any changes)** — ask all questions together in a single numbered-list message. Do not skip any question, even if the answer seems obvious or can be inferred from project state. Wait for the user to answer all questions before proceeding. The questions are: Q1 (operating mode), Q2 (LwIP), Q3 (mbedTLS), Q4 (secure-sockets), Q5 (logging), Q5a (log level — ask inline under Q5, required when Q5=yes), Q5b (output routing — ask inline under Q5, required when Q5=yes), Q6 (WPS — STA/Concurrent only), Q7 (credentials), Q8 (reconnection task), Q9 (AUTOIP — STA + LwIP only). See [workflow.md §Scope Questions](./references/workflow.md#scope-questions).
4. **Present configuration summary and confirm** — show the user a summary of all selected options and ask for confirmation before making any changes. See [workflow.md §User Confirmation Pattern](./references/workflow.md#user-confirmation-pattern).
5. **Install libraries** — use the `mtb-library-installer` skill for each library. Install `wifi-connection-manager`, plus `lwip`, `lwip-freertos-integration`, `lwip-network-interface-integration`, `whd`, `whd-bsp-integration`, `ifx-mbedtls`, `secure-sockets`, and `connectivity-utilities` as applicable to the user's scope answers. When Q2=yes and Q3=yes, the `wifi-core-freertos-lwip-mbedtls` bundle may be used instead of the individual libraries (see [workflow.md §Alternative Bundle](./references/workflow.md#alternative-wifi-core-freertos-lwip-mbedtls-bundle)). After `make getlibs`, verify each expected directory exists under `../mtb_shared/` before proceeding. See [workflow.md §Library Installation](./references/workflow.md#library-installation).
6. **Copy configuration files** — copy all 5 bundled config files from **this skill's own `assets/` directory only** to the chosen placement directory (project root or `configs/` subdirectory): `lwipopts.h`, `mbedtls_user_config.h`, `ifx_psa_crypto_config.h`, `ifx_psa_mxcrypto_config.h`, `ifx_tfm_config.h`. ⛔ Do NOT copy from `../mtb_shared/`, other projects, or any path outside the current project. The placement must match the path prefixes in the Makefile DEFINES. See [config-files.md §Config File Placement](./references/config-files.md#config-file-placement).
7. **Update Makefile** — add `COMPONENTS` and `DEFINES` entries. When mbedTLS is included, set `MBEDTLS_USER_CONFIG_FILE`, `MBEDTLS_CONFIG_FILE`, and `MBEDTLS_PSA_CRYPTO_CONFIG_FILE`. Check for duplicates before adding. See [config-files.md §Makefile](./references/config-files.md#makefile-integration).
8. **Create `wifi_credentials.h`** — use the credentials collected in Step 0. Add to `.gitignore`. See [config-files.md §Credentials](./references/config-files.md#wifi_credentialsh).
9. **Set up debug output (if logging enabled)** — if the user selected `retarget-io` in Step 0, check `deps/retarget-io.mtb`. If absent, **always assume the user wants it configured** — do not ask; invoke the `mtb-retarget-io` skill automatically before generating any WiFi code. For RTT or custom callback, generate the appropriate `log_output` callback. See [code-patterns.md §Logging Setup](./references/code-patterns.md#logging-setup).
10. **Generate initialization code** — generate `wifi_task.c` and update `main.c` using all answers from Step 0. On PSE84, also generate the SDIO init function, PSE84-specific `main()` additions, and PSE84 retarget-io init. See [code-patterns.md](./references/code-patterns.md) and [pse84-patterns.md](./references/pse84-patterns.md).
11. **Verify integration** — build the project; confirm no undefined symbol errors from WCM or networking libraries.

## Troubleshooting

| Condition | Action |
|---|---|
| FreeRTOS not present | Stop; direct user to the `mtb-freertos` skill |
| BSP has no WiFi hardware indicators | Warn user; ask for confirmation before proceeding |
| `make getlibs` fails | Report stderr; check `$CY_TOOLS_PATHS`, network, manifest availability |
| `lwipopts.h` not found in installed libraries | Copy bundled `assets/lwipopts.h` to chosen placement directory silently |
| `mbedtls_user_config.h` not found in installed libraries | Copy bundled `assets/mbedtls_user_config.h` to chosen placement directory silently |
| `ifx_psa_crypto_config.h` / `ifx_psa_mxcrypto_config.h` / `ifx_tfm_config.h` not found | Copy bundled versions from `assets/` to chosen placement directory silently |
| `cy_wcm_init()` returns non-success | Check `CYBSP_WIFI_CAPABLE` in DEFINES; confirm `cybsp_init()` completed before WCM init; on PSE84 confirm `app_sdio_init()` was called first |
| `cy_wcm_connect_ap()` fails all retries | Report last result code; offer to run `cy_wcm_scan()` to verify network visibility |
| SoftAP `cy_wcm_start_ap()` fails | Check channel (1–13 for 2.4 GHz); check for conflicting interface |
| Build errors: undefined WCM/LwIP/mbedTLS symbols | Confirm `COMPONENTS` entries; confirm `make getlibs` succeeded |
| Build errors: undefined `arch/cc.h` | Confirm `lwip-freertos-integration` is installed; check `../mtb_shared/lwip-freertos-integration/` |
| Build errors: undefined `whd_types.h` | Confirm `whd` and `lwip-network-interface-integration` are installed |
| Build errors: PSE84 TLS/SSL compile errors | Add PSE84 re-enable block to `mbedtls_user_config.h`; see [config-files.md §PSE84 Patch](./references/config-files.md#pse84-mbedtls-patch) |
| Build errors: `MBEDTLS_PSA_CRYPTO_CONFIG_FILE` not honoured | Confirm `MBEDTLS_PSA_CRYPTO_CONFIG_FILE` is in Makefile DEFINES with correct path prefix |
| WiFi crashes/hangs after Deep Sleep on PSE84 | SDHC DeepSleep SysPm callback not registered; add `sdhc_deep_sleep_cb` registration inside `app_sdio_init()` — see [pse84-patterns.md §PSE84 SDIO](./references/pse84-patterns.md#pse84-sdio-initialization) |
| No IP address after STA connect | DHCP unreachable or LwIP heap exhausted; check `MEM_SIZE` in `lwipopts.h` |
| Library installed to wrong sub-project | Re-run `mtb-library-installer` targeting the CM33_NS sub-project path |

## References

- [Detailed Workflow](./references/workflow.md) — scope questions, library installation, bundle vs individual libraries, step-by-step execution
- [Configuration Files](./references/config-files.md) — `lwipopts.h`, `mbedtls_user_config.h`, PSA/TFM config files, `wifi_credentials.h`, Makefile COMPONENTS/DEFINES (including `MBEDTLS_PSA_CRYPTO_CONFIG_FILE`), config file placement options, PSE84 mbedTLS patch
- [Code Patterns](./references/code-patterns.md) — FreeRTOS task structure, WCM init, STA connection, SoftAP init, event callback, reconnection task
- [PSE84 Platform Patterns](./references/pse84-patterns.md) — PSE84 main() additions, retarget-io init, SDIO initialization with DeepSleep callback
- [Bundled lwipopts.h](./assets/lwipopts.h) — LwIP config sourced from `wifi-core-freertos-lwip-mbedtls`; uses WHD-aware `PBUF_LINK_HLEN` and `TCP_MSS` constants
- [Bundled mbedtls_user_config.h](./assets/mbedtls_user_config.h) — mbedTLS config sourced from `wifi-core-freertos-lwip-mbedtls`; includes PSE84 patch for `MBEDTLS_HAVE_TIME`, `MBEDTLS_SSL_MAX_FRAGMENT_LENGTH`, `MBEDTLS_SSL_ALPN`
- [Bundled ifx_psa_crypto_config.h](./assets/ifx_psa_crypto_config.h) — PSA crypto algorithm config; hardware-accelerated operations for Infineon MX crypto engine
- [Bundled ifx_psa_mxcrypto_config.h](./assets/ifx_psa_mxcrypto_config.h) — PSA MXCrypto driver config; active only when `CY_IP_MXCRYPTO` is defined
- [Bundled ifx_tfm_config.h](./assets/ifx_tfm_config.h) — TFM crypto buffer sizes
- [wifi-connection-manager API documentation](https://infineon.github.io/wifi-connection-manager/api_reference_manual/html/index.html)
- [wifi-connection-manager GitHub](https://github.com/Infineon/wifi-connection-manager)
- [wifi-core-freertos-lwip-mbedtls GitHub](https://github.com/Infineon/wifi-core-freertos-lwip-mbedtls) — Quick Start reference; all steps must be replicated individually by this skill
- [Working example: PSoC Edge WiFi HTTPS Client](https://github.com/Infineon/mtb-example-psoc-edge-wifi-https-client) — reference implementation for PSE84 WiFi + TLS
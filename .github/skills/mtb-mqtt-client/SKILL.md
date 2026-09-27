---
name: mtb-mqtt-client
description: Add an MQTT client to a ModusToolbox project — broker connection, TLS, publish/subscribe, and FreeRTOS task setup. Use when the user asks to "add MQTT", "connect to an MQTT broker", "publish sensor data", "subscribe to MQTT topics", "set up MQTT messaging", "send telemetry to AWS IoT Core", "integrate with Mosquitto", "connect to HiveMQ", or any request about cloud messaging, IoT data publishing, or MQTT over WiFi. Installs the Infineon mqtt library, generates FreeRTOS task code, configures TLS, and adds reconnection logic. Requires WiFi; automatically invokes the mtb-wifi-stack sub-skill if WiFi is not yet set up. Always use this skill rather than writing MQTT code manually.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# mtb-mqtt-client

Integrate the Infineon MQTT client library into a ModusToolbox project. This skill installs the `mqtt` library, generates `mqtt_config.h`, copies and tunes `core_mqtt_config.h`, configures TLS credentials, updates the Makefile, and generates all FreeRTOS task code needed to connect to a broker, publish, subscribe, handle disconnects, and reconnect automatically.

This is a **library-tier skill**. It orchestrates:
1. Prerequisite verification (WiFi stack, FreeRTOS)
2. Scope clarification — 14 questions covering broker, TLS, auth, topics, task structure, coreHTTP, logging, reconnection, and multi-core
3. Library installation via the `mtb-library-installer` skill
4. Config file creation (`mqtt_config.h`, `mqtt_credentials.h`, `core_mqtt_config.h`)
5. Makefile updates (`COMPONENTS`, `DEFINES`, optional coreHTTP exclusion)
6. Generated C source: `mqtt_task.c`, `mqtt_task.h`, `main.c` changes, WiFi task changes
7. Reconnection logic — event-driven or polling network monitor task
8. Optional VCM setup for cross-core MQTT on PSE84

## When to Use This Skill

- User says: "add MQTT", "connect to a broker", "publish sensor data over MQTT", "subscribe to topics", "send telemetry to AWS IoT Core / HiveMQ / Mosquitto", "set up MQTT messaging", "I need cloud connectivity"
- WiFi is already configured and the user wants to add MQTT on top
- MQTT library files exist but connection code or configuration is incomplete

### When Not to Use This Skill

- The project has no WiFi — invoke `mtb-wifi-stack` first, then return
- FreeRTOS is not installed — direct the user to `mtb-freertos` first
- The user only needs to update broker credentials or topic strings — make targeted edits to `mqtt_config.h` instead of re-running the full workflow
- The kit is CYW955913EVK-01 (ThreadX/CAT5) — stop and inform the user that manual integration is required for this platform

## Prerequisites

- A ModusToolbox project with a valid `Makefile` and `make` available
- **WiFi stack must be installed.** Check for `deps/wifi-connection-manager.mtb` AND `LWIP`/`MBEDTLS` in Makefile `COMPONENTS`. If either is absent, invoke the `mtb-wifi-stack` sub-skill:

  > *MQTT requires a working WiFi stack, which is not detected in this project. Invoking `mtb-wifi-stack` to set it up before continuing with MQTT.*

  > ⚠️ Do **not** use `../mtb_shared/secure-sockets/` presence as a check — `mtb_shared` is workstation-wide. Only project-local `deps/` files and Makefile entries are reliable indicators.

- **FreeRTOS** is implicitly verified by the WiFi stack prerequisite; no separate check needed.
- **PSE84 multi-core:** All changes target the CM33_NS sub-project unless the user specifies otherwise.

## Key Files

| File | Purpose |
|---|---|
| `deps/mqtt.mtb` | Confirms mqtt library is declared in this project |
| `mqtt_config.h` *(project root)* | Broker hostname, port, client ID, keep-alive, credentials |
| `mqtt_credentials.h` *(project root)* | TLS certificate material — **never commit; always add to `.gitignore`** |
| `core_mqtt_config.h` *(project root)* | Tunable timeouts and retry counts — copied from library after `make getlibs` |
| `mqtt_task.c` / `mqtt_task.h` *(project root)* | Generated FreeRTOS task, event callback, reconnection logic |
| `Makefile` | `COMPONENTS+=SECURE_SOCKETS`; optional `CY_IGNORE` for coreHTTP; optional `ENABLE_MQTT_LOGS` |

## Step-by-Step Workflow

> ⛔ **Present scope questions and receive user confirmation BEFORE modifying any files.** See [workflow.md §Scope Questions](./references/workflow.md#scope-questions) for Essential vs Contextual question categories. After answers are confirmed, present the [Configuration Summary Confirmation Gate](./references/workflow.md#configuration-summary-confirmation-gate) and wait for explicit confirmation.

1. **Check prerequisites** — confirm `deps/wifi-connection-manager.mtb` and Makefile COMPONENTS. Invoke `mtb-wifi-stack` if absent. Note whether `deps/mqtt.mtb` already exists (skip install if it does).
2. **Ask scope questions** — see [workflow.md §Scope Questions](./references/workflow.md#scope-questions). Essential questions (Q1–Q4, Q8–Q9, Q13) must be explicitly answered. Contextual questions (Q5–Q7, Q10–Q12, Q14) have recommended defaults — present them for confirmation. After answers are received, present the [Configuration Summary Confirmation Gate](./references/workflow.md#configuration-summary-confirmation-gate) and wait for explicit user confirmation before making any changes.
3. **Acknowledge AWS SDK license** — the `mqtt` library transitively downloads the AWS IoT Device SDK. Present this in a **separate message** from the configuration summary confirmation; do not combine or bundle them. Wait for explicit acknowledgment before proceeding to Step 4.
4. **Install libraries** — use `mtb-library-installer` to install `mqtt`. Query the manifest for the correct version; use a fixed release tag (never `latest-*`). Install `connectivity-utilities` if Q12 = Yes and it is absent. Run `make getlibs`; confirm exit code 0 and that `../mtb_shared/mqtt/` now exists.
5. **Copy `core_mqtt_config.h`** — copy from `../mtb_shared/mqtt/<version>/include/core_mqtt_config.h` to the project root. Inform the user of key tunables (especially `MQTT_RECV_POLLING_TIMEOUT_MS`). See [workflow.md §core_mqtt_config.h](./references/workflow.md#core_mqtt_configh).
6. **Create `mqtt_config.h`** — populate from scope question answers. Add to `.gitignore` if credentials are included. See [code-patterns.md §mqtt_config.h](./references/code-patterns.md#mqtt_configh).
7. **Create `mqtt_credentials.h`** *(TLS only — Q2 = Yes)* — generate PEM placeholders for CA cert (and client cert + key for mTLS). Always add to `.gitignore`. See [code-patterns.md §mqtt_credentials.h](./references/code-patterns.md#mqtt_credentialsh).
8. **Update Makefile** — add `COMPONENTS+=SECURE_SOCKETS`; add `CY_IGNORE` for coreHTTP if Q11 = No; add `ENABLE_MQTT_LOGS` if Q12 = Yes. Check for duplicates before adding. See [workflow.md §Makefile Integration](./references/workflow.md#makefile-integration).
9. **Set up debug output** *(Q12 = Yes)* — check `deps/retarget-io.mtb`; if absent, invoke `mtb-retarget-io` before generating any MQTT code. See [workflow.md §Debug Output](./references/workflow.md#debug-output).
10. **Generate C code** — create `mqtt_task.c`, `mqtt_task.h`; update `main.c` and the WiFi task WCM callback. See [code-patterns.md §Task Code](./references/code-patterns.md#task-code).
11. **Generate reconnection code** *(Q13 = Yes)* — apply event-driven loop or polling `network_monitor_task` per Q13b. See [code-patterns.md §Reconnection](./references/code-patterns.md#reconnection).
12. **Configure multi-core / VCM** *(Q14 = Yes, Q14b = Yes)* — install `virtual-connectivity-manager` on both sub-projects, add Makefile DEFINES, add `cy_vcm_init()` calls. See [workflow.md §Multi-core Support](./references/workflow.md#multi-core-support-pse84).
13. **Verify integration** — build the project; confirm no undefined symbol errors. See [workflow.md §Post-Integration Verification](./references/workflow.md#post-integration-verification).

## Troubleshooting

| Condition | Action |
|---|---|
| WiFi stack not present | Invoke `mtb-wifi-stack`; wait for all its pre-checks to pass before continuing |
| `make getlibs` fails | Report stderr; check `$CY_TOOLS_PATHS`, network access, and manifest availability |
| `cy_mqtt_init()` returns non-success | Confirm `COMPONENTS` contains `LWIP`, `MBEDTLS`, `SECURE_SOCKETS` |
| `cy_mqtt_create()` returns non-success | Verify `MQTT_NETWORK_BUFFER_SIZE` ≥ `CY_MQTT_MIN_NETWORK_BUFFER_SIZE`; check hostname is non-empty; **ensure `descriptor` is not NULL** — `NULL` or empty descriptor returns `CY_RSLT_MODULE_MQTT_BADARG` (`0x08060002`); use `MQTT_CLIENT_ID` as the descriptor |
| `cy_mqtt_connect()` returns non-success | Confirm WiFi is connected; verify broker hostname/IP and port are reachable |
| TLS handshake failure | Confirm CA cert matches broker; check device clock accuracy; verify SNI hostname matches cert CN/SAN; avoid `test.mosquitto.org` secured port (SHA-1 disabled in mbedTLS by default) |
| Frequent `CY_MQTT_DISCONN_TYPE_SND_RCV_FAIL` | Increase `MQTT_RECV_POLLING_TIMEOUT_MS` in `core_mqtt_config.h` — most common cause of spurious disconnects |
| MQTT reconnect fails after disconnect event | Confirm `cy_mqtt_disconnect()` was called in the event callback before retrying `cy_mqtt_connect()` |
| `mqtt_task` never receives `WIFI_CONNECTED_BIT` | Confirm `network_event_group` created before tasks start; confirm WCM callback calls `xEventGroupSetBits` on `CY_WCM_EVENT_CONNECTED` |
| Build errors: undefined `cy_mqtt_*` | Confirm mqtt library installed and `make getlibs` succeeded; check `../mtb_shared/mqtt/` exists |
| Build errors: undefined `cy_socket_*` or `cy_tls_*` | Confirm `COMPONENTS` contains `SECURE_SOCKETS` |
| Publishes cause eventual disconnect over TLS | Rate-limit to 1–2 publishes/second; increase `MQTT_SEND_RETRY_TIMEOUT_MS` in `core_mqtt_config.h` |
| Secured platform build errors (CY8CKIT-064S0S2-4343W) | Add `CY_TFM_PSA_SUPPORTED`, `TFM_MULTI_CORE_NS_OS`, `CY_SECURE_SOCKETS_PKCS_SUPPORT` to DEFINES; put TFM include path before mbedTLS path |
| Library installed to wrong sub-project (PSE84) | Re-run `mtb-library-installer` targeting the CM33_NS sub-project path explicitly |

## References

- [Detailed Workflow](./references/workflow.md) — scope questions (Q1–Q14), library installation, AWS SDK license acknowledgment, Makefile integration, `core_mqtt_config.h` tunables, debug output, multi-core/VCM setup, post-integration verification
- [Code Patterns](./references/code-patterns.md) — config file templates, `mqtt_task.c/h`, `main.c` changes, WiFi task WCM callback, extend-WiFi-task pattern, event-driven reconnection, polling network monitor task, usage notes
- [mqtt GitHub Repository](https://github.com/Infineon/mqtt)
- [mqtt API Reference](https://infineon.github.io/mqtt/api_reference_manual/html/index.html)
- [mtb-example-wifi-mqtt-client](https://github.com/Infineon/mtb-example-wifi-mqtt-client)
- [mtb-library-installer Skill](../mtb-library-installer/SKILL.md)
- [mtb-wifi-stack Skill](../mtb-wifi-stack/SKILL.md)

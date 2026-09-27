# mtb-mqtt-client Workflow Reference

## Scope Questions

Present scope questions to the user before implementation. Questions are categorized as **Essential** (must be explicitly answered) or **Contextual** (have sensible defaults — present with defaults and allow batch confirmation).

The agent should select which questions to ask based on the user's request complexity:
- **Simple/prototyping setup** (plaintext, local broker): Ask Essential questions; present Contextual defaults for batch confirmation
- **Production/TLS setup** (cloud broker, mTLS): Ask all questions — TLS decisions have security implications
- **Extended use case** (the user's scenario requires additional clarification): The agent may ask additional questions beyond this list based on the specific requirements

> ⛔ **Do not install libraries, modify files, or run any command until the user has confirmed the configuration.** After questions are answered, present the [Configuration Summary Confirmation Gate](#configuration-summary-confirmation-gate) and wait for explicit confirmation.

---

### Essential Questions (always ask explicitly)

### Q1: MQTT Broker Address and Port

> *What is the hostname or IP address of your MQTT broker, and which port should the client connect to?*
>
> - Local broker on dev PC: `192.168.1.100`, port `1883` (plaintext) or `8883` (TLS)
> - AWS IoT Core: `<endpoint>.iot.<region>.amazonaws.com`, port `8883`
> - HiveMQ public test: `broker.hivemq.com`, port `1883` (plaintext) or `8883` (TLS with server cert) — **recommended for prototyping**
> - Eclipse Mosquitto public test: `test.mosquitto.org`, port `1883` (plaintext only — secured ports use SHA-1 certs rejected by default mbedTLS config; do NOT use port 8883/8884 without custom mbedTLS tuning)
>
> If unknown, use a placeholder — changeable in `mqtt_config.h` before flashing.

### Q2: Use TLS?

> - **Yes — TLS (port 8883)** *(Recommended for all internet-connected applications)* — encrypts the connection; requires the CA certificate.
> - **No — plaintext (port 1883)** — no encryption; only for isolated local networks or early prototyping.

If No, confirm: *"Unencrypted MQTT is not suitable for production. Proceed without TLS?"*

### Q3: TLS Authentication Mode *(Ask only if Q2 = Yes)*

> - **Server certificate only (one-way TLS)** *(Recommended)* — client verifies broker via CA cert; broker does not verify the client.
> - **Mutual TLS (mTLS)** — both sides authenticate; required by AWS IoT Core and similar platforms.

### Q4: MQTT Authentication

> - **No — anonymous** — no credentials; common for local/test brokers.
> - **Yes — username and password** — credentials sent in the CONNECT packet.

If Yes: ask for username and password. Store in `mqtt_config.h`. Warn the user that `mqtt_config.h` must be added to `.gitignore`.

---

### Contextual Questions (present defaults; confirm in batch or individually)

### Q5: Client ID

> *What Client ID should this device use?*
>
> Must be unique per broker session — two clients with the same ID will knock each other off. Default: `ifx-mqtt-client`.

### Q6: Clean Session

> - **Yes — clean session** *(Recommended)* — broker discards session state on each reconnect; avoids stale message buildup.
> - **No — persistent session** — broker retains QoS 1/2 messages and subscriptions while the device is offline.

### Q7: Keep-Alive Interval

> - **60 seconds** *(Recommended default)*
> - **30 seconds** — more responsive; slightly higher network overhead
> - **Custom** — enter a value between 10 and 3600 s (AWS IoT Core max: 1200 s)

### Q8: Initial Subscribe Topic(s)

> *Which topic(s) should the device subscribe to? Enter topic filter and QoS per topic, or leave blank to skip.*
>
> Examples: `device/commands` at QoS 1 · `home/+/temperature` at QoS 0
>
> QoS 0 = at most once · QoS 1 = at least once · QoS 2 = exactly once (not supported by all brokers)

### Q9: Initial Publish Topic(s)

> *Which topic(s) will the device publish to? Enter topic and QoS per topic, or leave blank — a commented-out publish template will still be generated.*
>
> Examples: `device/status` at QoS 1 · `sensor/temperature` at QoS 0

### Q10: Task Structure

> - **Separate MQTT task** *(Recommended)* — a dedicated `mqtt_task()` waits on a FreeRTOS event group for WiFi readiness. Cleaner separation; easier to maintain independently.
> - **Extend the existing WiFi task** — MQTT init runs inside `wifi_task()` after `cy_wcm_connect_ap()` succeeds. Fewer tasks; best when logic is tightly coupled to WiFi state.

### Q11: Exclude coreHTTP?

> *The `mqtt` library transitively includes both coreMQTT and coreHTTP from the AWS IoT Device SDK. Do you need HTTP client functionality in this project?*
>
> - **No — exclude coreHTTP** *(Recommended if HTTP is not needed)* — adds `CY_IGNORE` to Makefile; reduces build time and binary size.
> - **Yes — keep coreHTTP** — both coreMQTT and coreHTTP will compile.

### Q12: Enable Debug Logging?

> - **Yes — enable MQTT logs** — useful during development; output goes through `printf` via the `retarget-io` path. `retarget-io` will be set up automatically if not already present.
> - **No — keep logs off** *(Recommended for production)*

### Q13: Generate Reconnection Logic?

> *When WiFi drops, the broker closes the MQTT session. Without reconnection logic the device goes permanently offline after any link blip.*
>
> - **Yes — generate reconnection logic** *(Recommended for all real deployments)*
> - **No — I will handle reconnection in my own code**

#### Q13b: Reconnection Style *(Ask only if Q13 = Yes)*

> - **Event-driven** *(Recommended)* — MQTT task blocks on `WIFI_CONNECTED_BIT` set by the WCM event callback. Reconnects immediately when WiFi is restored.
> - **Polling network monitor task** — dedicated task polls `cy_wcm_is_connected_to_ap()` every 10 seconds. Simpler; less coupled to the event group.

### Q14: Multi-core PSE84 Project?

> - **No — single-core or MQTT on one core only** — no multi-core configuration needed.
> - **Yes — PSE84 multi-core project** — MQTT runs on CM33_NS; all library installs, Makefile changes, and generated files target the CM33_NS sub-project.

#### Q14b: Cross-Core MQTT Access? *(Ask only if Q14 = Yes)*

> - **Yes — set up VCM for cross-core MQTT** — installs `virtual-connectivity-manager` on both sub-projects; configures Makefile DEFINES and `cy_vcm_init()` calls so CM33_S can call `cy_mqtt_publish`, `cy_mqtt_subscribe`, etc. over IPC.
> - **No — CM33_S does not need MQTT access** — no VCM setup needed.

---

## Configuration Summary Confirmation Gate

After all answers for Q1–Q14 (and any applicable conditional sub-questions) are received, produce a configuration summary table and wait for explicit user confirmation before doing anything else — no prerequisite checking, no file reads, no library installs.

Populate the table from the user's answers exactly as given:

| # | Question | Answer |
|---|---|---|
| Q1 | Broker hostname:port | |
| Q2 | Use TLS? | |
| Q3 | TLS auth mode | *(N/A if Q2 = No)* |
| Q4 | MQTT authentication | |
| Q5 | Client ID | |
| Q6 | Clean session | |
| Q7 | Keep-alive interval | |
| Q8 | Subscribe topic(s) | *(None if skipped)* |
| Q9 | Publish topic(s) | *(None if skipped)* |
| Q10 | Task structure | |
| Q11 | Exclude coreHTTP? | |
| Q12 | Debug logging? | |
| Q13 | Reconnection logic? | |
| Q13b | Reconnection style | *(N/A if Q13 = No)* |
| Q14 | Multi-core PSE84? | |
| Q14b | Cross-core MQTT (VCM)? | *(N/A if Q14 = No)* |

Then ask:

> *"Does this configuration look correct? Confirm to proceed, or let me know what to change."*

**Do not advance to any next step until the user explicitly confirms.** This is a hard gate.

---

## Library Installation

> ⚠️ Complete ALL Makefile changes before the first build attempt.

Delegate all installation to the `mtb-library-installer` sub-skill.

| Library | Version tag | When to install |
|---|---|---|
| `mqtt` | Query manifest; use fixed release tag | Always |
| `connectivity-utilities` | Query manifest; use fixed release tag | Q12 = Yes AND not already installed |
| `virtual-connectivity-manager` | Query manifest; use fixed release tag | Q14b = Yes (both sub-projects) |

After `make getlibs`, confirm `../mtb_shared/mqtt/` exists before proceeding. Do not assume any transitive dependency arrived without verifying the directory.

### AWS IoT Device SDK License Acknowledgment

Before installing, present this notice **in a separate message** from the configuration summary confirmation — do not bundle them together. This acknowledgment must stand alone as its own interaction:

> *The `mqtt` library will also download the AWS IoT Device SDK onto your computer. Review and accept its license terms before proceeding: https://github.com/aws/aws-iot-device-sdk-embedded-C*

Do not proceed with library installation until the user explicitly acknowledges. This is a second hard gate, separate from the configuration summary confirmation gate.

---

## Makefile Integration

Apply changes to the project-root `Makefile` (or CM33_NS sub-project Makefile on PSE84). Check for existing entries before adding — never create duplicates.

### Required

```makefile
COMPONENTS+=SECURE_SOCKETS
```

`FREERTOS`, `LWIP`, and `MBEDTLS` are already added by `mtb-wifi-stack`; do not duplicate them.

### Exclude coreHTTP *(Q11 = No)*

```makefile
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/standard/coreHTTP
```

#### ⛔ Full AWS SDK CY_IGNORE (MQTT-only projects)

The `mqtt` library transitively pulls the entire AWS IoT Device SDK. When ONLY coreMQTT is needed (no HTTP, OTA, shadow, defender, or jobs), add **all** of these CY_IGNORE lines to prevent build failures from missing config headers:

```makefile
# AWS SDK sub-libraries not needed for MQTT-only usage
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/standard/coreHTTP
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/aws/ota-for-aws-iot-embedded-sdk
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/aws/device-shadow-for-aws-iot-embedded-sdk
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/aws/device-defender-for-aws-iot-embedded-sdk
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/aws/jobs-for-aws-iot-embedded-sdk
CY_IGNORE+= $(SEARCH_aws-iot-device-sdk-port)/source/ota
```

> **Why all five?** Each sub-library expects its own `*_config.h` (e.g., `core_http_config.h`, `ota_config.h`). If not ignored, the build fails with "file not found" for headers that don't exist in a minimal MQTT project. Always apply the full set unless the user explicitly needs one of these features.

**Always apply this block when Q11 = No** — do not apply only `coreHTTP` and wait for the next build failure.

### Debug Logging *(Q12 = Yes)*

```makefile
DEFINES+=ENABLE_MQTT_LOGS
```

### Internal Event Thread Stack *(inform user; do not apply automatically)*

```makefile
DEFINES+=CY_MQTT_EVENT_THREAD_STACK_SIZE=8*1024
```

The MQTT library's internal event thread defaults to 3 KB. Override here if stack overflows are observed.

### Secured Platform (CY8CKIT-064S0S2-4343W)

```makefile
DEFINES+=CY_TFM_PSA_SUPPORTED TFM_MULTI_CORE_NS_OS
DEFINES+=CY_SECURE_SOCKETS_PKCS_SUPPORT
INCLUDES=$(SEARCH_trusted-firmware-m)/COMPONENT_TFM_NS_INTERFACE/include
INCLUDES+=libs/trusted-firmware-m/COMPONENT_TFM_NS_INTERFACE/include
```

Also set `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` ≥ `100` in `FreeRTOSConfig.h` (TFM software ECC is slower than hardware). On this kit, certificates and keys are provisioned into the secure element — `mqtt_credentials.h` is not used.

---

## core_mqtt_config.h

After `make getlibs` succeeds, copy `core_mqtt_config.h` from the library to the project root so the application can tune it:

```
../mtb_shared/mqtt/<version>/include/core_mqtt_config.h  →  <project root>/core_mqtt_config.h
```

Do not modify the library source copy. Inform the user of key tunables:

| Macro | When to change |
|---|---|
| `MQTT_RECV_POLLING_TIMEOUT_MS` | **Increase on high-latency networks or with large payloads** — too small is the most common cause of `CY_MQTT_DISCONN_TYPE_SND_RCV_FAIL` disconnects |
| `MQTT_SEND_RETRY_TIMEOUT_MS` | Increase on congested networks or when TLS publish rate is high |
| `CY_MQTT_ACK_RECEIVE_TIMEOUT_MS` | Increase on high-latency networks (QoS 1/2 ACK wait) |
| `CY_MQTT_MESSAGE_SEND_TIMEOUT_MS` | Increase if sends time out on busy networks |
| `CY_MQTT_MAX_RETRY_VALUE` | Increase for unreliable links |
| `CY_MQTT_MAX_OUTGOING_PUBLISHES` | Increase for concurrent publish use cases |
| `CY_MQTT_MAX_OUTGOING_SUBSCRIBES` | Increase when subscribing to many topics at once |
| `MQTT_PINGRESP_TIMEOUT_MS` | Increase if broker is on a high-latency link |
| `MQTT_MAX_CONNACK_RECEIVE_RETRY_COUNT` | Increase if broker is slow to respond |
| `MQTT_STATE_ARRAY_MAX_COUNT` | Increase for high-throughput QoS 1/2 applications |

---

## Debug Output

*(Apply only if Q12 = Yes)*

`printf` must be functional before the MQTT task starts.

1. Check whether `deps/retarget-io.mtb` exists and `cy_retarget_io_init()` is called in the project. If so, no action is needed.
2. If `retarget-io` is absent, invoke the `mtb-retarget-io` sub-skill to install and integrate it before generating any MQTT code.

---

## Multi-core Support (PSE84)

*(Apply only if Q14 = Yes)*

MQTT and the entire network stack run on CM33_NS. Target all library installation, Makefile changes, and generated source files to the CM33_NS sub-project.

- `mqtt_task.c`, `mqtt_task.h`, `mqtt_config.h`, `mqtt_credentials.h`, and `core_mqtt_config.h` go in the CM33_NS sub-project directory.
- `main.c` modifications (event group creation, task creation) apply to the CM33_NS `main.c`.

### Virtual Connectivity Manager (VCM) *(Q14b = Yes)*

VCM allows CM33_S to call MQTT virtual APIs over IPC; execution happens on CM33_NS.

**Virtual APIs available to CM33_S:**
- `cy_mqtt_get_handle`
- `cy_mqtt_register_event_callback` / `cy_mqtt_deregister_event_callback`
- `cy_mqtt_publish`
- `cy_mqtt_subscribe` / `cy_mqtt_unsubscribe`

**Setup steps:**

1. Install `virtual-connectivity-manager` on **both** sub-projects via `mtb-library-installer`.

2. **CM33_NS Makefile:**
   ```makefile
   DEFINES+=ENABLE_MULTICORE_CONN_MW VCM_ENABLE_MQTT
   ```

3. **CM33_S Makefile:**
   ```makefile
   DEFINES+=ENABLE_MULTICORE_CONN_MW USE_VIRTUAL_API VCM_ENABLE_MQTT
   ```

4. Call `cy_vcm_init()` on **both** cores before any MQTT API call. Ordering is critical:
   - The core that boots first: `config.hal_resource_opt = CY_VCM_CREATE_HAL_RESOURCE`
   - The core that boots second: `config.hal_resource_opt = CY_VCM_USE_HAL_RESOURCE`
   - The first core must complete `cy_vcm_init()` before bringing up the second core.

See [Virtual Connectivity Manager API](https://infineon.github.io/virtual-connectivity-manager/api_reference_manual/html/index.html) for `cy_vcm_config_t`.

For dual-core logging, see [VCM README — Enable logs in dual core application](https://github.com/Infineon/virtual-connectivity-manager#enable-logs-in-dual-core-application).

**If Q14b = No:** skip VCM entirely — do not install it or add the DEFINES.

**IPC for non-VCM application data** (e.g., sensor readings from CM33_S forwarded to MQTT on CM33_NS) is out of scope for this skill. If asked, note the limitation and refer to PSE84 dual-core communication documentation.

---

## Post-Integration Verification

1. **Build** — must compile without errors. Undefined MQTT or `secure-sockets` symbols indicate a missing library or failed `make getlibs`.
2. **Config files** — `mqtt_config.h` and `core_mqtt_config.h` exist in the project root (or CM33_NS root). If TLS enabled, `mqtt_credentials.h` contains actual certificate material (not unmodified placeholders). Files with credentials are in `.gitignore`.
3. **Makefile** — `COMPONENTS` contains `SECURE_SOCKETS`; `DEFINES` contains `ENABLE_MQTT_LOGS` if Q12 = Yes.
4. **Event group** — `network_event_group` is created in `main()` before any task starts; WCM callback sets `WIFI_CONNECTED_BIT` on `CY_WCM_EVENT_CONNECTED` and clears it on `CY_WCM_EVENT_DISCONNECTED`.
5. **Task creation** — `mqtt_task` created via `xTaskCreate()` in `main()` before `vTaskStartScheduler()` (separate-task pattern).
6. **Init sequence** — `cy_mqtt_init()` → `cy_mqtt_create()` → `cy_mqtt_connect()` → `cy_mqtt_subscribe()` in that order, inside `mqtt_task()` (or `wifi_task()` for extend pattern), only after WiFi is confirmed.
7. **Disconnect handling** — `CY_MQTT_EVENT_TYPE_DISCONNECT` case in the event callback calls `cy_mqtt_disconnect()` before clearing `WIFI_CONNECTED_BIT`.

*Runtime (inform the user):* Flash the device and observe UART output. A successful connection prints the broker hostname and port. Use MQTT Explorer or `mosquitto_sub` to confirm the initial publish arrives on the expected topic and that publishing to the subscribed topic causes the device to print the received message.

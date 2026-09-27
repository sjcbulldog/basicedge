# WiFi Stack — Code Patterns

All WiFi initialization runs inside a FreeRTOS task. Do not call WCM APIs from `main()` before the scheduler starts.

---

## main.c — Task Setup

Verify that `main()` contains the following before creating the WiFi task. Add any missing items:

```c
/* In main.c */
#include "FreeRTOS.h"
#include "task.h"

/* Stack size: use 10 KB when TLS (mbedTLS) is included; 5 KB minimum for WiFi only */
#define WIFI_TASK_STACK_SIZE  (1024 * 10)
#define WIFI_TASK_PRIORITY    (5)

extern void wifi_task(void *arg);

/* Application error handler — customize to blink LED, write fault log, etc. */
static inline void handle_app_error(void) { CY_ASSERT(0); for(;;){} }

int main(void)
{
    cy_rslt_t result = cybsp_init();
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    /* See "PSE84 main() Additions" section below for additional
     * initialization required on multi-core PSE84 devices (RTC, LPTimer,
     * CM55 enable, PSE84-specific retarget-io init). */

    __enable_irq();

    if (pdPASS != xTaskCreate(wifi_task, "WiFi Task", WIFI_TASK_STACK_SIZE,
                              NULL, WIFI_TASK_PRIORITY, NULL))
    {
        handle_app_error();
    }
    vTaskStartScheduler();

    handle_app_error();  /* Should never reach here */
}
```

If a `network_monitor_task` was requested (see [Reconnection Task](#application-layer-reconnection-task)), create it here as well, before `vTaskStartScheduler()`.

---

## wifi_task.c — File Structure

Generate a new `wifi_task.c` (or add to an existing connectivity source file). The function contains all WiFi initialization and connection logic. Include `wifi_credentials.h` in this file — not in shared headers.

---

## Logging Setup

Call `cy_log_init()` at the top of `wifi_task()`, **before** `cy_wcm_init()`. Two layers must both be configured:

- **Compile-time (Layer 1):** `ENABLE_WCM_LOGS` and related DEFINES gate whether middleware emits log calls at all — set these in the Makefile first. See [config-files.md §Logging DEFINES](./config-files.md#logging-compile-time-defines-layer-1).
- **Runtime (Layer 2):** `cy_log_init()` sets the log level and output destination. Without this call, no output is produced even when the DEFINES are set.

Generate the appropriate snippet based on the user's Q5a (log level) and Q5b (output routing) choices.

### Option A — retarget-io (stdio / printf)

Pass `NULL` for the output callback. Log messages route through `printf()`, which `retarget-io` redirects to the UART or USB-CDC terminal.

```c
#include "cy_log.h"

/* Call before cy_wcm_init() — replace CY_LOG_INFO with the user's Q5a choice */
cy_rslt_t log_result = cy_log_init(CY_LOG_INFO, NULL, NULL);
/* NULL output → printf() → UART via retarget-io                               */
/* NULL time   → cy_rtos_get_time() used for timestamps                         */
CY_ASSERT(log_result == CY_RSLT_SUCCESS);
```

### Option B — Segger RTT

Declare a static callback that writes to RTT channel 0. No UART or retarget-io required.

```c
#include "cy_log.h"
#include "SEGGER_RTT.h"

static int rtt_log_output(CY_LOG_FACILITY_T facility, CY_LOG_LEVEL_T level, char *logmsg)
{
    (void)facility;
    (void)level;
    return SEGGER_RTT_printf(0, "%s\n", logmsg);
}

/* Call before cy_wcm_init() */
cy_rslt_t log_result = cy_log_init(CY_LOG_INFO, rtt_log_output, NULL);
CY_ASSERT(log_result == CY_RSLT_SUCCESS);
```

### Option C — Custom callback

The callback receives the already-formatted message string along with the facility and level, so routing decisions can be made per-facility if needed.

```c
#include "cy_log.h"

static int my_log_output(CY_LOG_FACILITY_T facility, CY_LOG_LEVEL_T level, char *logmsg)
{
    (void)facility;
    (void)level;
    /* Replace the body with your target: UART write, ring buffer, remote syslog, etc. */
    return printf("%s\n", logmsg);
}

cy_rslt_t log_result = cy_log_init(CY_LOG_INFO, my_log_output, NULL);
CY_ASSERT(log_result == CY_RSLT_SUCCESS);
```

### Per-Facility Level Overrides

After `cy_log_init()`, individual facilities can be tuned independently. For example, to keep WCM output at `INFO` while silencing the verbose WHD driver:

```c
/* Tune per-facility after cy_log_init() */
cy_log_set_facility_level(CYLF_MIDDLEWARE, CY_LOG_INFO);  /* WCM, secure-sockets     */
cy_log_set_facility_level(CYLF_DRIVER,    CY_LOG_ERR);   /* WHD driver — errors only */
```

Relevant facilities for WiFi:

| Facility | What it controls |
|---|---|
| `CYLF_MIDDLEWARE` | WCM, secure-sockets, connectivity middleware |
| `CYLF_DRIVER` | WHD (WiFi Host Driver) — radio and firmware-level messages |
| `CYLF_LP` | Low-power state transitions |
| `CYLF_DEF` | General / unclassified messages |

### Writing Log Messages from Application Code

WiFi task code can write into the same log system using the same facility and level taxonomy:

```c
cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_INFO, "Connecting to '%s'...\n", WIFI_SSID);
cy_log_msg(CYLF_MIDDLEWARE, CY_LOG_ERR,  "Connection failed: 0x%08lx\n", (unsigned long)result);

/* cy_log_printf() bypasses all level/facility checks — always prints */
cy_log_printf("WiFi stack initialized\n");
```

---

## WCM Initialization

Place at the top of `wifi_task()`, before any connect or AP-start call.

```c
#include "cy_wcm.h"
#include "wifi_credentials.h"

void wifi_task(void *arg)
{
    cy_rslt_t result;

    /* See Logging Setup section — call cy_log_init() here before cy_wcm_init() if logging is enabled */

    cy_wcm_config_t wcm_config = {
        .interface = CY_WCM_INTERFACE_TYPE_STA   /* or _AP or _AP_STA — set from Q1 */
    };

    /* On PSE84 (multi-core, COMPONENT_PSE84), the SDIO bus must be
     * initialized first and additional fields populated:
     *
     *   app_sdio_init();   // see "PSE84: SDIO Initialization" section below
     *   wcm_config.wifi_interface_instance = &sdio_instance;
     *   // .wifi_wl_pin and .wifi_host_wake_pin are set inside app_sdio_init()
     *
     * On all other devices (PSoC 6, XMC7000, etc.) the BSP handles SDIO
     * configuration; only .interface is needed.
     */

    result = cy_wcm_init(&wcm_config);
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    /* Register event callback immediately after init */
    /* wcm_event_callback must be forward-declared above wifi_task() — see Event Callback section */
    cy_wcm_register_event_callback(wcm_event_callback);

    /* ... connect or start AP below ... */
}
```

Set `.interface` from the user's operating mode choice (Q1):
- `CY_WCM_INTERFACE_TYPE_STA` — Station
- `CY_WCM_INTERFACE_TYPE_AP` — SoftAP
- `CY_WCM_INTERFACE_TYPE_AP_STA` — Concurrent

---

## STA Mode: Connection with Retry

```c
#define MAX_WIFI_RETRIES     5
#define WIFI_RETRY_DELAY_MS  2000

cy_wcm_connect_params_t connect_params = {0};
memcpy(connect_params.ap_credentials.SSID,     WIFI_SSID,     strlen(WIFI_SSID));
memcpy(connect_params.ap_credentials.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
connect_params.ap_credentials.security = WIFI_SECURITY;
connect_params.itwt_profile            = CY_WCM_ITWT_PROFILE_NONE;

cy_wcm_ip_address_t ip_addr;

for (int retry = 0; retry < MAX_WIFI_RETRIES; retry++)
{
    result = cy_wcm_connect_ap(&connect_params, &ip_addr);
    if (result == CY_RSLT_SUCCESS)
    {
        if (CY_WCM_IP_VER_V4 == ip_addr.version)
        {
            printf("WiFi connected. IP: %s\n",
                   ip4addr_ntoa((const ip4_addr_t *)&ip_addr.ip.v4));
        }
        else if (CY_WCM_IP_VER_V6 == ip_addr.version)
        {
            printf("WiFi connected. IP: %s\n",
                   ip6addr_ntoa((const ip6_addr_t *)&ip_addr.ip.v6));
        }
        break;
    }
    printf("Connect attempt %d/%d failed (0x%08lx), retrying...\n",
           retry + 1, MAX_WIFI_RETRIES, (unsigned long)result);
    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
}

if (result != CY_RSLT_SUCCESS)
{
    printf("WiFi connection failed after %d retries.\n", MAX_WIFI_RETRIES);
    /* Application-specific failure handling */
}
```

---

## SoftAP Mode: Start AP

```c
cy_wcm_ap_config_t ap_config = {0};
memcpy(ap_config.ap_credentials.SSID,     SOFTAP_SSID,     strlen(SOFTAP_SSID));
memcpy(ap_config.ap_credentials.password, SOFTAP_PASSWORD, strlen(SOFTAP_PASSWORD));
ap_config.ap_credentials.security = SOFTAP_SECURITY;
ap_config.channel                  = SOFTAP_CHANNEL;

result = cy_wcm_start_ap(&ap_config);
if (CY_RSLT_SUCCESS != result) { handle_app_error(); }
printf("SoftAP started: SSID='%s', channel=%d\n", SOFTAP_SSID, SOFTAP_CHANNEL);
```

---

## Event Callback

Declare the callback **before** `wifi_task()` in `wifi_task.c`. Always include a forward declaration at the top of the file so the callback can be referenced inside `wifi_task()` even when it is defined after it:

```c
/* Forward declaration — required when callback definition follows wifi_task() */
static void wcm_event_callback(cy_wcm_event_t event,
                                cy_wcm_event_data_t *event_data);
```

The `network_monitor_handle` variable is only needed if the reconnection task is generated (see below).

```c
static TaskHandle_t network_monitor_handle = NULL;

static void wcm_event_callback(cy_wcm_event_t event,
                                cy_wcm_event_data_t *event_data)
{
    switch (event)
    {
        case CY_WCM_EVENT_CONNECTING:
            printf("WiFi: Connecting...\n");
            break;
        case CY_WCM_EVENT_CONNECTED:
            printf("WiFi: Connected to AP\n");
            break;
        case CY_WCM_EVENT_CONNECT_FAILED:
            printf("WiFi: Connection attempt failed\n");
            break;
        case CY_WCM_EVENT_DISCONNECTED:
            printf("WiFi: Disconnected\n");
            if (network_monitor_handle != NULL)
                xTaskNotifyGive(network_monitor_handle);
            break;
        case CY_WCM_EVENT_RECONNECTED:
            printf("WiFi: Reconnected\n");
            break;
        case CY_WCM_EVENT_IP_CHANGED:
            printf("WiFi: IP address changed\n");
            break;
        case CY_WCM_EVENT_INITIATED_RETRY:
            printf("WiFi: WCM auto-retry initiated\n");
            break;

        /* SoftAP / Concurrent mode events — include when Q1 = AP or Concurrent */
        case CY_WCM_EVENT_STA_JOINED_SOFTAP:
            printf("WiFi: STA joined SoftAP (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                   event_data->sta_mac[0], event_data->sta_mac[1],
                   event_data->sta_mac[2], event_data->sta_mac[3],
                   event_data->sta_mac[4], event_data->sta_mac[5]);
            break;
        case CY_WCM_EVENT_STA_LEFT_SOFTAP:
            printf("WiFi: STA left SoftAP\n");
            break;

        default:
            break;
    }
}
```

**WCM built-in reconnection:** WCM automatically attempts to restore the STA link when it drops. `CY_WCM_EVENT_RECONNECTED` fires when recovery succeeds. No application-level polling loop is needed for basic link recovery.

---

## Application-Layer Reconnection Task

Ask the user whether to generate this task:

> *WCM will automatically restore the WiFi link if it drops. However, application-layer sessions (MQTT, HTTP connections) must be re-established manually after a reconnect. Would you like a reconnection task that waits for link recovery and signals your application to restart those sessions?*
>
> - **Yes — generate a reconnection task**
> - **No — I will handle session restart in my own code**

If yes, generate this function and add `xTaskCreate` for it in `main()`:

```c
void network_monitor_task(void *arg)
{
    network_monitor_handle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        /* Block until disconnect notification from wcm_event_callback */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Wait for WCM to restore the link */
        while (!cy_wcm_is_connected_to_ap())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        printf("Network restored — restarting application sessions\n");
        /* Call application-specific session restart functions here */
    }
}
```

In `main.c`, create this task before `vTaskStartScheduler()`:
```c
xTaskCreate(network_monitor_task, "Net Monitor",
            configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL);
```

---

## PSE84 Platform-Specific Patterns

See [pse84-patterns.md](./pse84-patterns.md) for:
- PSE84 `main()` additions (RTC/clib setup, LPTimer for tickless idle, CM55 boot)
- PSE84 retarget-io initialization (`mtb_hal_uart_t` API vs PSOC 6 baud-rate API)
- PSE84 SDIO initialization (`app_sdio_init()` required before `cy_wcm_init()`)
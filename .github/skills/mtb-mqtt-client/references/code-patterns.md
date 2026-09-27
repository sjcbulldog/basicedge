# mtb-mqtt-client Code Patterns Reference

## mqtt_config.h

Create in the project root (or CM33_NS sub-project root for PSE84). Add to `.gitignore` if credentials (Q4 = Yes) are included.

```c
/* mqtt_config.h — MQTT broker connection parameters */
#define MQTT_BROKER_HOSTNAME     "192.168.1.100"   /* Q1 */
#define MQTT_BROKER_PORT         (1883U)            /* 8883 for TLS (Q2) */
#define MQTT_CLIENT_ID           "ifx-mqtt-client"  /* Q5 */
#define MQTT_KEEP_ALIVE_SECONDS  (60U)              /* Q7 */
#define MQTT_CLEAN_SESSION       (true)             /* Q6 */
#define MQTT_NETWORK_BUFFER_SIZE (1024U)            /* must be >= CY_MQTT_MIN_NETWORK_BUFFER_SIZE */

/* Define to enable TLS (Q2 = Yes) */
/* #define MQTT_USE_TLS */

/* Define to enable mutual TLS in addition to server cert (Q3 = mTLS) */
/* #define MQTT_USE_MUTUAL_TLS */

/* Uncomment if broker requires authentication (Q4 = Yes) */
/* #define MQTT_USERNAME  "your-username" */
/* #define MQTT_PASSWORD  "your-password" */
```

Set `MQTT_BROKER_PORT` to `8883` and uncomment `MQTT_USE_TLS` when Q2 = Yes.

---

## mqtt_credentials.h

*(TLS only — Q2 = Yes)* Always add to `.gitignore`. Inform the user they must paste actual certificate material before building.

**One-way TLS template:**

```c
/* mqtt_credentials.h — TLS credentials. DO NOT COMMIT TO SOURCE CONTROL */

/* Root CA certificate of the broker, PEM format, null-terminated */
static const char MQTT_BROKER_CA_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "... paste CA certificate here ...\n"
    "-----END CERTIFICATE-----\n";

static const size_t MQTT_BROKER_CA_CERT_SIZE = sizeof(MQTT_BROKER_CA_CERT);
```

**Additional fields for mutual TLS (Q3 = mTLS):**

```c
/* Client certificate, PEM format */
static const char MQTT_CLIENT_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "... paste client certificate here ...\n"
    "-----END CERTIFICATE-----\n";
static const size_t MQTT_CLIENT_CERT_SIZE = sizeof(MQTT_CLIENT_CERT);

/* Client private key, PEM format */
static const char MQTT_CLIENT_KEY[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "... paste private key here ...\n"
    "-----END RSA PRIVATE KEY-----\n";
static const size_t MQTT_CLIENT_KEY_SIZE = sizeof(MQTT_CLIENT_KEY);
```

**Azure IoT Hub — X.509:** set `password = NULL` in the CONNECT request.
**Azure IoT Hub — SAS token:** set `root_ca`, `private_key`, and `client_cert` to `NULL` in TLS credentials.

---

## Task Code

### Separate MQTT Task *(Q10 = Recommended)*

#### mqtt_task.h

```c
#pragma once
#include "FreeRTOS.h"
#include "event_groups.h"

/* Set by the WCM event callback when WiFi connects; cleared on disconnect */
#define WIFI_CONNECTED_BIT   (1UL << 0)

/* Defined in mqtt_task.c; declared extern here for use in wifi_task.c and main.c */
extern EventGroupHandle_t network_event_group;

void mqtt_task(void *arg);
```

#### mqtt_task.c

```c
#include "mqtt_task.h"
#include "mqtt_config.h"
#include "cy_mqtt_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include <string.h>
#include <stdio.h>

#ifdef MQTT_USE_TLS
#include "mqtt_credentials.h"
#endif

EventGroupHandle_t network_event_group;

static cy_mqtt_t      mqtt_handle;
static uint8_t        mqtt_network_buffer[MQTT_NETWORK_BUFFER_SIZE];

/* Topics from Q8 — adjust as needed */
static cy_mqtt_subscribe_info_t sub_list[] = {
    { .qos = CY_MQTT_QOS1, .topic = "device/commands", .topic_len = 15 }
};

/* Connection info stored at file scope for use by reconnection code */
static cy_mqtt_connect_info_t conn;

static void mqtt_event_callback(cy_mqtt_t handle, cy_mqtt_event_t event, void *user_data);

void mqtt_task(void *arg)
{
    cy_rslt_t result;

    /* Wait for WiFi before starting MQTT */
    xEventGroupWaitBits(network_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    printf("MQTT: WiFi connected — starting MQTT client\n");

    result = cy_mqtt_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);

#ifdef MQTT_USE_TLS
    cy_awsport_ssl_credentials_t tls_creds = {
        .root_ca      = MQTT_BROKER_CA_CERT,
        .root_ca_size = MQTT_BROKER_CA_CERT_SIZE,
#ifdef MQTT_USE_MUTUAL_TLS
        .client_cert      = MQTT_CLIENT_CERT,
        .client_cert_size = MQTT_CLIENT_CERT_SIZE,
        .private_key      = MQTT_CLIENT_KEY,
        .private_key_size = MQTT_CLIENT_KEY_SIZE,
#endif
    };
    cy_awsport_ssl_credentials_t *p_creds = &tls_creds;
#else
    cy_awsport_ssl_credentials_t *p_creds = NULL;
#endif

    cy_mqtt_broker_info_t broker = {
        .hostname       = MQTT_BROKER_HOSTNAME,
        .hostname_len = (uint16_t)(sizeof(MQTT_BROKER_HOSTNAME) - 1U),
        .port         = (uint16_t)MQTT_BROKER_PORT
    };

    /* descriptor must NOT be NULL — the library validates it and returns
     * CY_RSLT_MODULE_MQTT_BADARG (0x08060002) if it is NULL or empty.
     * Use a short unique string that identifies this client instance. */
    result = cy_mqtt_create(mqtt_network_buffer, sizeof(mqtt_network_buffer),
                             p_creds, &broker,
                             MQTT_CLIENT_ID /* descriptor — must be non-NULL */,
                             &mqtt_handle);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    /* Register the event callback separately (v4.X API — not a cy_mqtt_create arg) */
    result = cy_mqtt_register_event_callback(mqtt_handle,
                                             mqtt_event_callback, NULL);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    conn = (cy_mqtt_connect_info_t){
        .client_id      = MQTT_CLIENT_ID,
        .client_id_len  = sizeof(MQTT_CLIENT_ID) - 1,
        .clean_session  = MQTT_CLEAN_SESSION,
        .keep_alive_sec = MQTT_KEEP_ALIVE_SECONDS,
#ifdef MQTT_USERNAME
        .username     = MQTT_USERNAME,
        .username_len = sizeof(MQTT_USERNAME) - 1,
        .password     = MQTT_PASSWORD,
        .password_len = sizeof(MQTT_PASSWORD) - 1,
#endif
    };

    result = cy_mqtt_connect(mqtt_handle, &conn);
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    printf("MQTT: Connected to %s:%u\n", MQTT_BROKER_HOSTNAME, MQTT_BROKER_PORT);

    /* Subscribe — generated from Q8 */
    result = cy_mqtt_subscribe(mqtt_handle, sub_list,
                                sizeof(sub_list) / sizeof(sub_list[0]));
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    printf("MQTT: Subscribed to '%s'\n", sub_list[0].topic);

    /* Initial publish — generated from Q9 */
    const char *payload = "online";
    cy_mqtt_publish_info_t pub = {
        .qos        = CY_MQTT_QOS1,
        .retain     = false,
        .topic      = "device/status",
        .topic_len  = 13,
        .payload    = payload,
        .payload_len = strlen(payload)
    };
    result = cy_mqtt_publish(mqtt_handle, &pub);
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    printf("MQTT: Published initial status\n");

    /* Application loop */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        /* Add application publish/subscribe logic here */
    }
}

static void mqtt_event_callback(cy_mqtt_t handle, cy_mqtt_event_t event, void *user_data)
{
    (void)user_data;

    switch (event.type)
    {
        case CY_MQTT_EVENT_TYPE_PUBLISH_RECEIVE:
        {
            cy_mqtt_publish_info_t *msg = &event.data.pub_msg.received_message;
            printf("MQTT Rx [%.*s]: %.*s\n",
                   (int)msg->topic_len, msg->topic,
                   (int)msg->payload_len, (const char *)msg->payload);
            break;
        }
        case CY_MQTT_EVENT_TYPE_DISCONNECT:
            /*
             * cy_mqtt_disconnect() MUST be called here to release resources
             * from cy_mqtt_connect(). The disconnect event is NOT fired when
             * disconnect is initiated by the application itself.
             *
             * Reason codes:
             *   CY_MQTT_DISCONN_TYPE_NETWORK_DOWN  — WiFi/network link lost
             *   CY_MQTT_DISCONN_TYPE_BROKER_DOWN   — ping response not received
             *   CY_MQTT_DISCONN_TYPE_BAD_RESPONSE  — malformed packet from broker
             *   CY_MQTT_DISCONN_TYPE_SND_RCV_FAIL  — send/receive failure;
             *                                         increase MQTT_RECV_POLLING_TIMEOUT_MS
             */
            printf("MQTT: Disconnected (reason: %d)\n", (int)event.data.reason);
            cy_mqtt_disconnect(handle);
            xEventGroupClearBits(network_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            break;
    }
}
```

#### main.c Changes

```c
#include "mqtt_task.h"

#define MQTT_TASK_STACK_SIZE  (1024 * 5)
#define MQTT_TASK_PRIORITY    (4)

/* In main(), after cybsp_init() and before vTaskStartScheduler(): */
network_event_group = xEventGroupCreate();
CY_ASSERT(network_event_group != NULL);

xTaskCreate(mqtt_task, "MQTT Task", MQTT_TASK_STACK_SIZE, NULL,
            MQTT_TASK_PRIORITY, NULL);
```

`network_event_group` is defined in `mqtt_task.c` and declared `extern` in `mqtt_task.h` — do not declare it a second time in `main.c`.

#### WiFi Task Changes

Add `#include "mqtt_task.h"` to the WiFi task source file. Modify the WCM event callback (generated by `mtb-wifi-stack`) to signal the event group:

```c
#include "mqtt_task.h"   /* for network_event_group and WIFI_CONNECTED_BIT */

static void wcm_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *event_data)
{
    switch (event)
    {
        case CY_WCM_EVENT_CONNECTED:
            printf("WiFi: Connected\n");
            xEventGroupSetBits(network_event_group, WIFI_CONNECTED_BIT);
            break;
        case CY_WCM_EVENT_DISCONNECTED:
            printf("WiFi: Disconnected\n");
            xEventGroupClearBits(network_event_group, WIFI_CONNECTED_BIT);
            break;
        /* ... existing cases ... */
    }
}
```

Only add the `xEventGroupSetBits` / `xEventGroupClearBits` calls if not already present. The WiFi task source must not call any MQTT API directly.

---

### Extend WiFi Task *(Q10 = Extend existing WiFi task)*

Insert the MQTT init sequence into `wifi_task()` after `cy_wcm_connect_ap()` succeeds, before the application loop. Order:

1. `cy_mqtt_init()`
2. `cy_mqtt_create()`
3. `cy_mqtt_connect()`
4. `cy_mqtt_subscribe()` (if Q8 answered)
5. `cy_mqtt_publish()` (if Q9 answered)

Include all headers at the top of `wifi_task.c`. Omit `mqtt_task.h`, `network_event_group`, and the separate task creation in `main.c`.

---

## Reconnection

*(Apply only if Q13 = Yes)*

### Event-Driven Reconnection *(Q13b = Event-driven)*

Replace the simple application loop in `mqtt_task()` with:

```c
    for (;;)
    {
        /* Wait for WiFi to (re)connect */
        xEventGroupWaitBits(network_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        /* cy_mqtt_disconnect() was already called in the event callback;
         * cy_mqtt_connect() reuses the handle from cy_mqtt_create(). */
        result = cy_mqtt_connect(mqtt_handle, &conn);
        if (result != CY_RSLT_SUCCESS)
        {
            printf("MQTT: Reconnect failed (0x%08lx) — retrying in 2 s\n",
                   (unsigned long)result);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        printf("MQTT: Reconnected to %s:%u\n", MQTT_BROKER_HOSTNAME, MQTT_BROKER_PORT);

        /* Re-subscribe when using clean sessions — broker discards state on reconnect */
#if MQTT_CLEAN_SESSION
        result = cy_mqtt_subscribe(mqtt_handle, sub_list,
                                    sizeof(sub_list) / sizeof(sub_list[0]));
        CY_ASSERT(result == CY_RSLT_SUCCESS);
#endif

        /* Wait for a disconnect before looping back */
        xEventGroupWaitBits(network_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);
    }
```

### Polling Network Monitor Task *(Q13b = Polling)*

Add `network_monitor_task()` to `mqtt_task.c` and remove the reconnection loop from `mqtt_task()` to avoid two concurrent reconnection paths:

```c
#define NETWORK_MONITOR_INTERVAL_MS  (10000U)

void network_monitor_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(NETWORK_MONITOR_INTERVAL_MS));

        if (!cy_wcm_is_connected_to_ap())
        {
            printf("Network monitor: WiFi down — waiting for reconnect\n");
            xEventGroupWaitBits(network_event_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdTRUE, portMAX_DELAY);
            printf("Network monitor: WiFi restored — reconnecting MQTT\n");
        }

        cy_rslt_t result = cy_mqtt_connect(mqtt_handle, &conn);
        if (result != CY_RSLT_SUCCESS)
        {
            printf("Network monitor: MQTT reconnect failed (0x%08lx)\n",
                   (unsigned long)result);
        }
        else
        {
            printf("Network monitor: MQTT reconnected\n");
#if MQTT_CLEAN_SESSION
            cy_mqtt_subscribe(mqtt_handle, sub_list,
                              sizeof(sub_list) / sizeof(sub_list[0]));
#endif
        }
    }
}
```

Add task creation to `main()`:

```c
#define NETWORK_MONITOR_TASK_STACK_SIZE  (1024 * 3)
#define NETWORK_MONITOR_TASK_PRIORITY    (3)

xTaskCreate(network_monitor_task, "Net Monitor",
            NETWORK_MONITOR_TASK_STACK_SIZE, NULL,
            NETWORK_MONITOR_TASK_PRIORITY, NULL);
```

`mqtt_handle` and `conn` must be accessible at file scope in `mqtt_task.c`. Declare `network_monitor_task()` in `mqtt_task.h` so `main.c` can reference it.

---

## Usage Notes

These rules apply to all generated code and any guidance given to the user.

- **`cy_mqtt_init()` and `cy_mqtt_deinit()` are not thread-safe.** Call from a single task only — before any handles are created or after all are deleted.
- **The network buffer** passed to `cy_mqtt_create()` must be ≥ `CY_MQTT_MIN_NETWORK_BUFFER_SIZE`. Always use an explicit fixed-size buffer (not NULL) for deterministic memory usage.
- **`cy_mqtt_create()` `descriptor` must NOT be NULL.** Passing `NULL` returns `CY_RSLT_MODULE_MQTT_BADARG` (`0x08060002`) immediately. Pass a short non-empty string — `MQTT_CLIENT_ID` is a good default. The descriptor is used by `cy_mqtt_get_handle()` for handle lookup; it must be ≤ `CY_MQTT_DESCP_MAX_LEN` characters.
- **In v4.X the event callback is NOT a `cy_mqtt_create()` argument.** Call `cy_mqtt_register_event_callback(handle, cb, user_data)` after `cy_mqtt_create()` returns success. The `cy_mqtt_broker_info_t` field is `hostname_len` (not `hostnameLength`).
- **Call `cy_mqtt_init()` before any other MQTT API.** Do not call MQTT APIs after `cy_mqtt_deinit()`.
- **Call `cy_mqtt_delete()`** when the MQTT instance is no longer needed.
- **On `CY_MQTT_EVENT_TYPE_DISCONNECT`, `cy_mqtt_disconnect()` must be called** before any reconnect attempt. Failure to do so leaks resources and causes the reconnect to fail. This event is **not** sent when disconnect is app-initiated.
- **Rate-limit TLS publishes** to 1–2 per second. Publishing faster than TLS can encrypt causes the send queue to back up and eventually produces `CY_MQTT_DISCONN_TYPE_SND_RCV_FAIL`. Increase `MQTT_SEND_RETRY_TIMEOUT_MS` in `core_mqtt_config.h` if higher rates are required.
- **`test.mosquitto.org` secured port is not supported by default** — the broker uses SHA-1 for certificate signing, which mbedTLS disables. Use plaintext port 1883 or a locally hosted broker for testing.
- **Azure IoT Hub — X.509:** set `password = NULL` in the MQTT connect request.
- **Azure IoT Hub — SAS token:** set `root_ca`, `private_key`, and `client_cert` to `NULL` in TLS credentials.

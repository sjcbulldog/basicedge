# BLE Central Patterns — Setup & Connection

Templates for the **Central (client/scanner)** role: platform init through GATT service discovery.
For operations after connection (read, write, subscribe), see `central-operations.md`.

> **Header-first rule:** Verify function signatures against installed headers before writing code.
> Patterns validated against btstack v5.x / btstack-integration v7.x.

---

## Required Headers

```c
/* Platform + BSP */
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"

/* BLE platform init (include BEFORE wiced_bt_stack_init) */
#include "cybt_platform_config.h"   /* cybt_platform_config_init() */
#include "cybsp_bt_config.h"        /* cybsp_bt_platform_cfg */
#include "cybt_platform_trace.h"    /* trace levels */
#include "cybt_result.h"            /* cybt_result_t, CYBT_SUCCESS */

/* BLE stack */
#include "wiced_bt_stack.h"         /* wiced_bt_stack_init() */
#include "wiced_bt_dev.h"           /* management events */
#include "wiced_bt_ble.h"           /* scan, connect */
#include "wiced_bt_gatt.h"          /* GATT client APIs */
#include "wiced_bt_cfg.h"           /* cfg settings structs */
#include "wiced_memory.h"           /* buffer alloc/free */
```

---

## Makefile

```makefile
COMPONENTS += FREERTOS WICED_BLE HCI-UART
# Add if WiFi also used:
COMPONENTS += LWIP MBEDTLS RTOS_AWARE SECURE_SOCKETS
DEFINES += CY_RTOS_AWARE CY_RETARGET_IO_CONVERT_LF_TO_CRLF
DEFINES += CYBSP_WIFI_CAPABLE  # only if WiFi present
```

> `HCI-UART` = PSOC 6 with CYW43xxx. PSE84 uses different transport — see `code-patterns.md`.

---

## Platform Init Sequence

```c
/* 1. BLE transport config (BEFORE WiFi on combo-radio) */
cybt_platform_config_init(&cybsp_bt_platform_cfg);

/* 2. WiFi init (loads shared radio firmware) */
cy_wcm_init(&wcm_config);

/* 3. BLE stack init (AFTER WiFi on combo-radio) */
wiced_bt_stack_init(app_bt_management_callback, &bt_cfg_settings);
```

> ⚠️ **Ordering is critical.** Reversing causes firmware load failure (error 0x1fad).

### PSE84 Exception (btstack-integration v7+)

On **PSE84** with btstack-integration v7.x, `cybt_platform_config_init()` is **not needed**. The platform layer reads BSP macros (`CYBSP_BT_PLATFORM_CFG_*`) directly. The init sequence simplifies to:

```c
/* 1. WiFi init */
cy_wcm_init(&wcm_config);

/* 2. BLE stack init (reads BSP config automatically) */
wiced_bt_stack_init(app_bt_management_callback, &bt_cfg_settings);
```

> **How to detect:** If `btstack-integration` version ≥ 7.0.0 and target is PSE84/PSOC Edge, skip `cybt_platform_config_init()`.

---

## BLE Configuration Structure

```c
static const wiced_bt_cfg_ble_scan_settings_t ble_scan_cfg = {
    .high_duty_scan_interval  = 96,    /* 60 ms */
    .high_duty_scan_window    = 48,    /* 30 ms */
    .high_duty_scan_duration  = 0,     /* infinite — ALWAYS use 0 for Central */
    .low_duty_scan_interval   = 2048,  /* 1.28 s */
    .low_duty_scan_window     = 48,
    .low_duty_scan_duration   = 0,
    /* Connection scan — use same intervals */
    .high_duty_conn_scan_interval = 96,
    .high_duty_conn_scan_window   = 48,
    .high_duty_conn_duration      = 30,
    .low_duty_conn_scan_interval  = 2048,
    .low_duty_conn_scan_window    = 48,
    .low_duty_conn_duration       = 0,
    /* Connection parameters */
    .conn_min_interval        = 12,    /* 15 ms */
    .conn_max_interval        = 12,
    .conn_latency             = 0,
    .conn_supervision_timeout = 200,   /* 2000 ms */
};

/* Advert config — MUST be non-NULL even for Central-only apps */
static const wiced_bt_cfg_ble_advert_settings_t ble_advert_cfg = { 0 };

static const wiced_bt_cfg_ble_t ble_cfg = {
    .ble_max_simultaneous_links   = 1,
    .ble_max_rx_pdu_size          = 251,
    .p_ble_scan_cfg               = &ble_scan_cfg,
    .p_ble_advert_cfg             = &ble_advert_cfg,  /* ⚠️ MUST NOT be NULL */
};

static const wiced_bt_cfg_gatt_t gatt_cfg = {
    .max_db_service_modules = 0,  /* Central — no local GATT DB */
    .max_eatt_bearers       = 0,
};

static wiced_bt_cfg_settings_t bt_cfg_settings = {
    .device_name       = (uint8_t *)"BLE_Central",
    .security_required = BTM_SEC_BEST_EFFORT,
    .p_ble_cfg         = &ble_cfg,
    .p_gatt_cfg        = &gatt_cfg,
};
```

> ⚠️ **`p_ble_advert_cfg` NULL causes hard fault.** Stack dereferences this during init regardless of role.
> Zero-initialized struct is sufficient — no fields need customization for Central.

---

## BT Management Callback

```c
static wiced_result_t app_bt_management_callback(wiced_bt_management_evt_t event,
                                                  wiced_bt_management_evt_data_t *p_event_data)
{
    switch (event)
    {
        case BTM_ENABLED_EVT:
            if (WICED_BT_SUCCESS == p_event_data->enabled.status)
            {
                wiced_bt_gatt_register(app_gatt_callback);
                wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_HIGH_DUTY, WICED_TRUE,
                                  app_scan_result_callback);
            }
            break;
        case BTM_BLE_SCAN_STATE_CHANGED_EVT:
            break;
        default:
            break;
    }
    return WICED_BT_SUCCESS;
}
```

---

## Scan — Name-Based Filter

```c
static void app_scan_result_callback(wiced_bt_ble_scan_results_t *p_scan_result,
                                     uint8_t *p_adv_data)
{
    if (p_scan_result == NULL) return;  /* scan complete */

    uint8_t *p_name = NULL;
    uint8_t name_len = 0;

    /* Try complete name, then short name */
    p_name = wiced_bt_ble_check_advertising_data(
        p_adv_data, BTM_BLE_ADVERT_TYPE_NAME_COMPLETE, &name_len);
    if (!p_name)
        p_name = wiced_bt_ble_check_advertising_data(
            p_adv_data, BTM_BLE_ADVERT_TYPE_NAME_SHORT, &name_len);

    if (p_name && name_len == strlen(TARGET_DEVICE_NAME) &&
        memcmp(p_name, TARGET_DEVICE_NAME, name_len) == 0)
    {
        /* Stop scan, then connect */
        wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_TRUE, NULL);
        wiced_bt_gatt_le_connect(p_scan_result->remote_bd_addr,
                                 p_scan_result->ble_addr_type,
                                 BLE_CONN_MODE_HIGH_DUTY, WICED_TRUE);
    }
}
```

---

## Scan — UUID-Based Filter

```c
static void app_scan_result_callback(wiced_bt_ble_scan_results_t *p_scan_result,
                                     uint8_t *p_adv_data)
{
    if (p_scan_result == NULL) return;

    uint8_t *p_uuid_data = NULL;
    uint8_t uuid_len = 0;

    p_uuid_data = wiced_bt_ble_check_advertising_data(
        p_adv_data, BTM_BLE_ADVERT_TYPE_16SRV_COMPLETE, &uuid_len);
    if (!p_uuid_data)
        p_uuid_data = wiced_bt_ble_check_advertising_data(
            p_adv_data, BTM_BLE_ADVERT_TYPE_16SRV_PARTIAL, &uuid_len);

    if (p_uuid_data)
    {
        for (uint8_t i = 0; i < uuid_len; i += 2)
        {
            uint16_t uuid = p_uuid_data[i] | (p_uuid_data[i + 1] << 8);
            if (uuid == TARGET_SERVICE_UUID)
            {
                wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_TRUE, NULL);
                wiced_bt_gatt_le_connect(p_scan_result->remote_bd_addr,
                    p_scan_result->ble_addr_type, BLE_CONN_MODE_HIGH_DUTY, WICED_TRUE);
                return;
            }
        }
    }
}
```

---

## GATT Callback + Discovery State Machine

```c
static uint16_t ble_conn_id = 0;
static uint16_t svc_start_handle = 0, svc_end_handle = 0;
static uint16_t char_value_handle = 0, cccd_handle = 0;

typedef enum { DISC_IDLE, DISC_SERVICES, DISC_CHARS, DISC_DESCS } disc_state_t;
static disc_state_t disc_state = DISC_IDLE;

static wiced_bt_gatt_status_t app_gatt_callback(wiced_bt_gatt_evt_t event,
                                                 wiced_bt_gatt_event_data_t *p_data)
{
    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            if (p_data->connection_status.connected) {
                ble_conn_id = p_data->connection_status.conn_id;
                app_start_discovery(ble_conn_id);
            } else {
                ble_conn_id = 0;
                app_handle_disconnect(p_data->connection_status.reason);
            }
            break;
        case GATT_DISCOVERY_RESULT_EVT:
            app_discovery_result(&p_data->discovery_result);
            break;
        case GATT_DISCOVERY_CPLT_EVT:
            app_discovery_complete(p_data->discovery_complete.conn_id);
            break;
        case GATT_OPERATION_CPLT_EVT:
            app_operation_complete(&p_data->operation_complete);
            break;
        default:
            break;
    }
    return WICED_BT_GATT_SUCCESS;
}
```

---

## Three-Phase Discovery

```c
#define TARGET_SVC_UUID   0x180D  /* Heart Rate Service (example) */
#define TARGET_CHAR_UUID  0x2A37  /* HR Measurement (example) */

static void app_start_discovery(uint16_t conn_id)
{
    wiced_bt_gatt_discovery_param_t param = { .s_handle = 1, .e_handle = 0xFFFF };
    disc_state = DISC_SERVICES;
    wiced_bt_gatt_client_send_discover(conn_id, GATT_DISCOVER_SERVICES_ALL, &param);
}

static void app_discovery_result(wiced_bt_gatt_discovery_result_t *p_result)
{
    switch (disc_state)
    {
        case DISC_SERVICES: {
            wiced_bt_gatt_group_value_t *g = &p_result->discovery_data.group_value;
            if (g->service_type.len == 2 && g->service_type.uu.uuid16 == TARGET_SVC_UUID) {
                svc_start_handle = g->s_handle;
                svc_end_handle = g->e_handle;
            }
            break;
        }
        case DISC_CHARS: {
            wiced_bt_gatt_char_declaration_t *c = &p_result->discovery_data.characteristic_declaration;
            if (c->char_uuid.len == 2 && c->char_uuid.uu.uuid16 == TARGET_CHAR_UUID)
                char_value_handle = c->val_handle;
            break;
        }
        case DISC_DESCS: {
            wiced_bt_gatt_char_descr_info_t *d = &p_result->discovery_data.char_descr_info;
            if (d->type.len == 2 && d->type.uu.uuid16 == 0x2902)  /* CCCD */
                cccd_handle = d->handle;
            break;
        }
        default: break;
    }
}

static void app_discovery_complete(uint16_t conn_id)
{
    wiced_bt_gatt_discovery_param_t param = {0};
    switch (disc_state)
    {
        case DISC_SERVICES:
            if (svc_start_handle) {
                param.s_handle = svc_start_handle;
                param.e_handle = svc_end_handle;
                disc_state = DISC_CHARS;
                wiced_bt_gatt_client_send_discover(conn_id, GATT_DISCOVER_CHARACTERISTICS, &param);
            } else { disc_state = DISC_IDLE; }
            break;
        case DISC_CHARS:
            if (char_value_handle) {
                param.s_handle = char_value_handle + 1;
                param.e_handle = svc_end_handle;
                disc_state = DISC_DESCS;
                wiced_bt_gatt_client_send_discover(conn_id, GATT_DISCOVER_CHARACTERISTIC_DESCRIPTORS, &param);
            } else { disc_state = DISC_IDLE; }
            break;
        case DISC_DESCS:
            disc_state = DISC_IDLE;
            if (cccd_handle)
                app_enable_notifications(conn_id, cccd_handle);
            break;
        default: break;
    }
}
```

---

## 128-bit UUID Matching

BLE stores 128-bit UUIDs **little-endian**. Reverse from standard representation:

```c
/* Standard: 12345678-1234-5678-9ABC-DEF012345678
 * LE bytes: */
static const uint8_t target_uuid128[16] = {
    0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

/* In discovery: */
if (g->service_type.len == 16 &&
    memcmp(g->service_type.uu.uuid128, target_uuid128, 16) == 0) { /* found */ }
```

---

## Disconnect → Rescan Reconnection

```c
static bool reconnect_enabled = true;

static void app_handle_disconnect(uint16_t reason)
{
    /* Reset ALL discovery state */
    svc_start_handle = svc_end_handle = 0;
    char_value_handle = cccd_handle = 0;
    disc_state = DISC_IDLE;

    if (reconnect_enabled) {
        vTaskDelay(pdMS_TO_TICKS(2000));  /* brief delay before rescan */
        wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_HIGH_DUTY, WICED_TRUE,
                          app_scan_result_callback);
    }
}
```

> ⚠️ Always reset ALL state on disconnect. Add 1-2s delay before rescan.
> Connection failure (0x3E) from `wiced_bt_gatt_le_connect` should also trigger rescan.

---

## Scanning Best Practices

- **ALWAYS use active scan** — passive scan misses scan response data (where most devices put their full name)
- **ALWAYS use duration=0** (infinite) — Central scans until target found
- Passive scan only for: UUID-based filter (UUIDs are in advertising payload) or passive observation

---

## FreeRTOS Task

```c
#define APP_TASK_STACK_SIZE  (1024u * 10u)  /* 10 KB */
#define APP_TASK_PRIORITY    (tskIDLE_PRIORITY + 5u)
```

> **Heap:** WiFi + BLE + MQTT → `configTOTAL_HEAP_SIZE` ≥ 160KB (or use `heap_3` with linker heap).

---

## API Quick Reference

| Function | Signature |
|----------|-----------|
| `wiced_bt_gatt_le_connect` | `(bd_addr, addr_type, conn_mode, is_direct) → wiced_bool_t` |
| `wiced_bt_gatt_client_send_discover` | `(conn_id, discovery_type, *p_param) → status` |
| `wiced_bt_ble_scan` | `(scan_type, is_duplicate_filter, *callback)` |
| `wiced_bt_ble_check_advertising_data` | `(*p_adv, type, *p_length) → *data` |

---

## Debug: BLE Trace Override

```c
cybt_result_t cybt_debug_uart_send_trace(uint16_t length, uint8_t* p_data) {
    printf("[BT] %s\n", (char*)p_data);
    return CYBT_SUCCESS;
}
/* Enable before stack init: */
cybt_platform_set_trace_level(CYBT_TRACE_ID_ALL, CYBT_TRACE_LEVEL_DEBUG);
```

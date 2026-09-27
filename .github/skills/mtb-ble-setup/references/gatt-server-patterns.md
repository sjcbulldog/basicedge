# BLE GATT Server Patterns

Complete GATT event handler, attribute request handler, and stack configuration templates.

**Required headers for GATT server implementation:**
```c
#include "wiced_bt_gatt.h"
#include "wiced_memory.h"       /* wiced_bt_get_buffer(), wiced_bt_free_buffer() */
#include "wiced_bt_cfg.h"
```

---

## GATT Event Handler

> **All 8 GATT callback events must be handled.** Missing `GATT_GET_RESPONSE_BUFFER_EVT` or `GATT_APP_BUFFER_TRANSMITTED_EVT` causes silent GATT failure — the stack cannot send responses without app-provided buffers.

```c
wiced_bt_gatt_status_t app_gatt_event_callback(
    wiced_bt_gatt_evt_t event,
    wiced_bt_gatt_event_data_t *p_event_data)
{
    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            if (p_event_data->connection_status.connected)
            {
                printf("BLE connected (conn_id=%d)\n",
                       p_event_data->connection_status.conn_id);
            }
            else
            {
                printf("BLE disconnected — restarting advertising\n");
                app_bt_start_advertising();
            }
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
            return app_gatt_attr_request_handler(
                &p_event_data->attribute_request);

        case GATT_GET_RESPONSE_BUFFER_EVT:  /* MANDATORY — provide response buffer */
        {
            wiced_bt_gatt_buffer_request_t *p_buf_req =
                &p_event_data->buffer_request;
            p_buf_req->buffer.p_app_rsp_buffer =
                (uint8_t *)wiced_bt_get_buffer(p_buf_req->len_requested);
            p_buf_req->buffer.p_app_ctxt = (void *)wiced_bt_free_buffer;
            break;
        }

        case GATT_APP_BUFFER_TRANSMITTED_EVT:  /* MANDATORY — free transmitted buffer */
        {
            wiced_bt_gatt_buffer_transmitted_t *p_buf =
                &p_event_data->buffer_xmitted;
            if (p_buf->p_app_ctxt)
            {
                ((void (*)(void *))p_buf->p_app_ctxt)(p_buf->p_app_data);
            }
            break;
        }

        case GATT_OPERATION_CPLT_EVT:  /* Central: client operation complete */
            /* Handle read/write/discover completion when acting as Central */
            break;

        case GATT_DISCOVERY_RESULT_EVT:  /* Central: discovery result */
            /* Process service/characteristic discovery results */
            break;

        case GATT_DISCOVERY_CPLT_EVT:  /* Central: discovery complete */
            /* All discovery results received */
            break;

        case GATT_CONGESTION_EVT:  /* Flow control */
            /* Stack indicates congestion state changed — pause/resume notifications */
            break;

        default:
            break;
    }
    return WICED_BT_GATT_SUCCESS;
}
```

### Event Summary

| Event | Role | Required? | Purpose |
|---|---|---|---|
| `GATT_CONNECTION_STATUS_EVT` | Both | Yes | Connection/disconnection lifecycle |
| `GATT_ATTRIBUTE_REQUEST_EVT` | Server | Yes | Handles all ATT read/write requests |
| `GATT_GET_RESPONSE_BUFFER_EVT` | Both | **MANDATORY** | App must provide response buffer — stack crashes without this |
| `GATT_APP_BUFFER_TRANSMITTED_EVT` | Both | **MANDATORY** | App must free buffer after transmission |
| `GATT_OPERATION_CPLT_EVT` | Central | Yes (Central) | Confirms client read/write/discover completed |
| `GATT_DISCOVERY_RESULT_EVT` | Central | Yes (Central) | Individual discovery results |
| `GATT_DISCOVERY_CPLT_EVT` | Central | Yes (Central) | Discovery procedure finished |
| `GATT_CONGESTION_EVT` | Both | Recommended | Flow control for notification-heavy apps |

---

## GATT Attribute Request Handler

> **All mandatory ATT opcodes must be handled.** Missing `GATT_REQ_MTU` causes the peer to stall; missing `GATT_REQ_READ_BY_TYPE` breaks service/characteristic discovery; missing explicit write response for `GATT_REQ_WRITE` causes the peer to timeout.

```c
wiced_bt_gatt_status_t app_gatt_attr_request_handler(
    wiced_bt_gatt_attribute_request_t *p_attr_req)
{
    switch (p_attr_req->opcode)
    {
        case GATT_REQ_MTU:  /* MANDATORY — respond with local MTU */
            wiced_bt_gatt_server_send_mtu_rsp(
                p_attr_req->conn_id,
                p_attr_req->data.remote_mtu,
                wiced_bt_cfg_settings.p_ble_cfg->ble_max_rx_pdu_size);
            return WICED_BT_GATT_SUCCESS;

        case GATT_REQ_READ_BY_TYPE:  /* MANDATORY — characteristic discovery */
            return app_gatt_read_by_type_handler(p_attr_req);

        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            return app_gatt_read_handler(p_attr_req);

        case GATT_REQ_WRITE:
        {
            wiced_bt_gatt_status_t status = app_gatt_write_handler(p_attr_req);
            /* MANDATORY: explicit write response for GATT_REQ_WRITE */
            wiced_bt_gatt_server_send_write_rsp(
                p_attr_req->conn_id, p_attr_req->opcode,
                p_attr_req->data.write_req.handle);
            return status;
        }

        case GATT_CMD_WRITE:  /* Write Without Response — no response needed */
            return app_gatt_write_handler(p_attr_req);

        case GATT_REQ_PREPARE_WRITE:
        case GATT_REQ_EXECUTE_WRITE:
            /* Queued writes — implement if long writes are needed */
            return WICED_BT_GATT_SUCCESS;

        case GATT_HANDLE_VALUE_CONF:  /* Indication confirmed by peer */
            /* Application can now send next indication */
            return WICED_BT_GATT_SUCCESS;

        default:
            return WICED_BT_GATT_REQ_NOT_SUPPORTED;
    }
}
```

### Attribute Request Opcodes Reference

| Opcode | Name | Server Must Handle? | Notes |
|---|---|---|---|
| 0x02 | `GATT_REQ_MTU` | **YES** | Must respond with `wiced_bt_gatt_server_send_mtu_rsp()` |
| 0x08 | `GATT_REQ_READ_BY_TYPE` | **YES** | Required for characteristic/service discovery |
| 0x0A | `GATT_REQ_READ` | Yes | Standard read |
| 0x0C | `GATT_REQ_READ_BLOB` | Recommended | Large values (>MTU) |
| 0x12 | `GATT_REQ_WRITE` | Yes | Must call `wiced_bt_gatt_server_send_write_rsp()` explicitly |
| 0x52 | `GATT_CMD_WRITE` | Optional | Write Without Response — no response sent |
| 0x16 | `GATT_REQ_PREPARE_WRITE` | Optional | Queued/long writes |
| 0x18 | `GATT_REQ_EXECUTE_WRITE` | Optional | Execute queued writes |
| 0x1E | `GATT_HANDLE_VALUE_CONF` | Recommended | Indication ACK from peer |

```c
wiced_bt_gatt_status_t app_gatt_read_handler(
    wiced_bt_gatt_attribute_request_t *p_attr_req)
{
    uint16_t handle = p_attr_req->data.read_req.handle;

    /* Look up attribute by handle — implementation depends on GATT DB structure */
    /* See gatt-db-patterns.md for the attribute lookup table pattern */

    return WICED_BT_GATT_INVALID_HANDLE;
}

wiced_bt_gatt_status_t app_gatt_write_handler(
    wiced_bt_gatt_attribute_request_t *p_attr_req)
{
    uint16_t handle = p_attr_req->data.write_req.handle;

    /* Process write by handle — update application state, send notifications, etc. */

    return WICED_BT_GATT_INVALID_HANDLE;
}
```

---

## BLE Stack Configuration (app_bt_cfg.c)

> ⚠️ **Non-NULL Requirements:** `p_ble_cfg` and `p_gatt_cfg` MUST be non-NULL. Within `p_ble_cfg`, the sub-pointers `p_ble_scan_cfg` and `p_ble_advert_cfg` MUST also be non-NULL. Passing NULL for any of these causes `wiced_bt_stack_init()` to reject the config silently (error 0x1fad on some firmware versions).

```c
#include "wiced_bt_cfg.h"

/* Scan settings — MUST be non-NULL in p_ble_cfg even if not actively scanning */
static const wiced_bt_cfg_ble_scan_settings_t ble_scan_cfg = {
    .high_duty_scan_interval         = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_INTERVAL,
    .high_duty_scan_window           = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_WINDOW,
    .high_duty_scan_duration         = 5,   /* seconds; 0 = scan forever */

    .low_duty_scan_interval          = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_INTERVAL,
    .low_duty_scan_window            = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_WINDOW,
    .low_duty_scan_duration          = 0,   /* 0 = scan forever */

    .high_duty_conn_scan_interval    = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_INTERVAL,
    .high_duty_conn_scan_window      = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_WINDOW,
    .high_duty_conn_duration         = 30,

    .low_duty_conn_scan_interval     = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_INTERVAL,
    .low_duty_conn_scan_window       = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_WINDOW,
    .low_duty_conn_duration          = 30,

    .conn_min_interval               = WICED_BT_CFG_DEFAULT_CONN_MIN_INTERVAL,
    .conn_max_interval               = WICED_BT_CFG_DEFAULT_CONN_MAX_INTERVAL,
    .conn_latency                    = WICED_BT_CFG_DEFAULT_CONN_LATENCY,
    .conn_supervision_timeout        = WICED_BT_CFG_DEFAULT_CONN_SUPERVISION_TIMEOUT,
};

/* Advertising settings — MUST be non-NULL in p_ble_cfg even if not advertising */
static const wiced_bt_cfg_ble_advert_settings_t ble_advert_cfg = {
    .high_duty_min_interval          = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MIN_INTERVAL,
    .high_duty_max_interval          = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MAX_INTERVAL,
    .high_duty_duration              = 30,  /* seconds; 0 = advertise forever */

    .low_duty_min_interval           = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MIN_INTERVAL,
    .low_duty_max_interval           = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MAX_INTERVAL,
    .low_duty_duration               = 0,   /* 0 = advertise forever */

    .high_duty_directed_min_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .high_duty_directed_max_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MAX_INTERVAL,

    .low_duty_directed_min_interval  = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .low_duty_directed_max_interval  = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MAX_INTERVAL,

    .high_duty_nonconn_min_interval  = WICED_BT_CFG_DEFAULT_HIGH_DUTY_NONCONN_ADV_MIN_INTERVAL,
    .high_duty_nonconn_max_interval  = WICED_BT_CFG_DEFAULT_HIGH_DUTY_NONCONN_ADV_MAX_INTERVAL,

    .low_duty_nonconn_min_interval   = WICED_BT_CFG_DEFAULT_LOW_DUTY_NONCONN_ADV_MIN_INTERVAL,
    .low_duty_nonconn_max_interval   = WICED_BT_CFG_DEFAULT_LOW_DUTY_NONCONN_ADV_MAX_INTERVAL,
};

/* BLE configuration — all sub-pointers MUST be non-NULL */
static const wiced_bt_cfg_ble_t wiced_bt_cfg_ble = {
    .ble_max_simultaneous_links = 1,     /* Adjust: max concurrent connections */
    .ble_max_rx_pdu_size        = 251,   /* Max L2CAP PDU size for BLE */
    .appearance                 = APPEARANCE_GENERIC_TAG,  /* Adjust per application */
    .rpa_refresh_timeout        = WICED_BT_CFG_DEFAULT_RANDOM_ADDRESS_NEVER_CHANGE,
    .host_addr_resolution_db_size = 5,
    .p_ble_scan_cfg             = &ble_scan_cfg,     /* ⚠️ MUST be non-NULL */
    .p_ble_advert_cfg           = &ble_advert_cfg,   /* ⚠️ MUST be non-NULL */
};

/* GATT configuration — MUST be non-NULL in top-level settings */
static const wiced_bt_cfg_gatt_t wiced_bt_cfg_gatt = {
    .max_db_service_modules = 0,
    .max_eatt_bearers       = 0,
};

/* Top-level settings — passed to wiced_bt_stack_init() */
const wiced_bt_cfg_settings_t wiced_bt_cfg_settings = {
    .device_name        = (uint8_t *)APP_BT_DEVICE_NAME,
    .security_required  = BTM_SEC_BEST_EFFORT,
    .p_br_cfg           = NULL,              /* NULL is safe for BLE-only */
    .p_ble_cfg          = &wiced_bt_cfg_ble, /* ⚠️ MUST be non-NULL */
    .p_gatt_cfg         = &wiced_bt_cfg_gatt,/* ⚠️ MUST be non-NULL */
    .p_isoc_cfg         = NULL,              /* Deprecated, safe to leave NULL */
    .p_l2cap_app_cfg    = NULL,              /* NULL unless L2CAP channels needed */
};
```

### Non-NULL Config Field Summary

| Field | Required? | If NULL |
|---|---|---|
| `wiced_bt_cfg_settings_t.p_ble_cfg` | **YES** | Stack init fails / silent rejection |
| `wiced_bt_cfg_settings_t.p_gatt_cfg` | **YES** | Stack init fails / silent rejection |
| `wiced_bt_cfg_ble_t.p_ble_scan_cfg` | **YES** | Stack init fails (error 0x1fad) |
| `wiced_bt_cfg_ble_t.p_ble_advert_cfg` | **YES** | Stack init fails (error 0x1fad) |
| `wiced_bt_cfg_settings_t.p_br_cfg` | No (BLE-only) | Safe to leave NULL for BLE-only apps |
| `wiced_bt_cfg_settings_t.p_isoc_cfg` | No (deprecated) | Safe to leave NULL |
| `wiced_bt_cfg_settings_t.p_l2cap_app_cfg` | No | Safe unless using L2CAP fixed channels |

> For the full struct definition, see `wiced_bt_cfg.h` in the btstack library after installation.

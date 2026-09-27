# BLE Central Operations

GATT client operations after connection is established and service discovery is complete.
For setup (init, scan, connect, discovery), see `central-patterns.md`.

> **Static buffers are MANDATORY for ALL write operations.**
> btstack reads buffers asynchronously after the function returns.
> Stack-local buffers cause silent failures.

---

## Notification Subscribe

```c
static void app_enable_notifications(uint16_t conn_id, uint16_t cccd_handle)
{
    static uint8_t cccd_val[2];
    static wiced_bt_gatt_write_hdr_t write_hdr;

    cccd_val[0] = GATT_CLIENT_CONFIG_NOTIFICATION & 0xFF;
    cccd_val[1] = (GATT_CLIENT_CONFIG_NOTIFICATION >> 8) & 0xFF;

    write_hdr.handle   = cccd_handle;
    write_hdr.offset   = 0;
    write_hdr.len      = 2;
    write_hdr.auth_req = GATT_AUTH_REQ_NONE;

    wiced_bt_gatt_client_send_write(conn_id, GATT_REQ_WRITE,
                                     &write_hdr, cccd_val, NULL);
}
```

---

## Indication Subscribe + Confirmation

Same as notification but uses `GATT_CLIENT_CONFIG_INDICATION` and **requires confirmation**:

```c
static void app_enable_indications(uint16_t conn_id, uint16_t cccd_handle)
{
    static uint8_t cccd_val[2];
    static wiced_bt_gatt_write_hdr_t write_hdr;

    cccd_val[0] = GATT_CLIENT_CONFIG_INDICATION & 0xFF;
    cccd_val[1] = (GATT_CLIENT_CONFIG_INDICATION >> 8) & 0xFF;

    write_hdr.handle   = cccd_handle;
    write_hdr.offset   = 0;
    write_hdr.len      = 2;
    write_hdr.auth_req = GATT_AUTH_REQ_NONE;

    wiced_bt_gatt_client_send_write(conn_id, GATT_REQ_WRITE,
                                     &write_hdr, cccd_val, NULL);
}
```

> ⚠️ **Must confirm every indication** — peripheral stalls if unconfirmed.

---

## Handling Notifications and Indications

In `GATT_OPERATION_CPLT_EVT`:

```c
static void app_operation_complete(wiced_bt_gatt_operation_complete_t *p_op)
{
    switch (p_op->op)
    {
        case GATTC_OPTYPE_NOTIFICATION:
        {
            uint8_t *data = p_op->response_data.att_value.p_data;
            uint16_t len  = p_op->response_data.att_value.len;
            uint16_t handle = p_op->response_data.att_value.handle;
            /* Process notification data */
            break;
        }

        case GATTC_OPTYPE_INDICATION:
        {
            uint8_t *data = p_op->response_data.att_value.p_data;
            uint16_t len  = p_op->response_data.att_value.len;
            uint16_t handle = p_op->response_data.att_value.handle;
            /* Process indication data, then MUST confirm: */
            wiced_bt_gatt_client_send_indication_confirm(p_op->conn_id, handle);
            break;
        }

        case GATTC_OPTYPE_WRITE_WITH_RSP:
            /* Write confirmed by peripheral */
            break;

        case GATTC_OPTYPE_READ_HANDLE:
            if (p_op->status == WICED_BT_GATT_SUCCESS) {
                uint8_t *data = p_op->response_data.att_value.p_data;
                uint16_t len  = p_op->response_data.att_value.len;
                /* Process read response */
            }
            break;

        case GATTC_OPTYPE_CONFIG_MTU:
            /* Negotiated MTU in p_op->response_data.mtu */
            break;

        default:
            break;
    }
}
```

---

## Write to Peripheral

### Write with Response (confirmed)

```c
static void app_write_characteristic(uint16_t conn_id, uint16_t handle,
                                     uint8_t *data, uint16_t len)
{
    static uint8_t write_buf[256];
    static wiced_bt_gatt_write_hdr_t write_hdr;

    memcpy(write_buf, data, len);
    write_hdr.handle   = handle;
    write_hdr.offset   = 0;
    write_hdr.len      = len;
    write_hdr.auth_req = GATT_AUTH_REQ_NONE;

    wiced_bt_gatt_client_send_write(conn_id, GATT_REQ_WRITE,
                                     &write_hdr, write_buf, NULL);
}
```

### Write Command (no response, fire-and-forget)

```c
static void app_write_command(uint16_t conn_id, uint16_t handle,
                              uint8_t *data, uint16_t len)
{
    static uint8_t cmd_buf[256];
    static wiced_bt_gatt_write_hdr_t write_hdr;

    memcpy(cmd_buf, data, len);
    write_hdr.handle   = handle;
    write_hdr.offset   = 0;
    write_hdr.len      = len;
    write_hdr.auth_req = GATT_AUTH_REQ_NONE;

    wiced_bt_gatt_client_send_write(conn_id, GATT_CMD_WRITE,
                                     &write_hdr, cmd_buf, NULL);
}
```

> Use `GATT_REQ_WRITE` for config/commands that must succeed.
> Use `GATT_CMD_WRITE` for high-throughput streams (no flow control).

---

## Read Characteristic

```c
static void app_read_characteristic(uint16_t conn_id, uint16_t handle)
{
    static uint8_t read_buf[256];

    wiced_bt_gatt_client_send_read_handle(
        conn_id, handle, 0, read_buf, sizeof(read_buf), GATT_AUTH_REQ_NONE);
}
```

Response arrives via `GATTC_OPTYPE_READ_HANDLE` in `app_operation_complete()` above.

---

## MTU Exchange

Request larger MTU **immediately after connection, before discovery**:

```c
/* Default MTU = 23 bytes (20 payload). Request larger: */
wiced_bt_gatt_client_configure_mtu(conn_id, 247);

/* Query current negotiated MTU: */
uint16_t mtu = wiced_bt_gatt_get_bearer_mtu(conn_id);
/* Usable payload = mtu - 3 (ATT header) */
```

Response arrives via `GATTC_OPTYPE_CONFIG_MTU` in operation complete handler.

---

## API Quick Reference

| Function | Signature |
|----------|-----------|
| `wiced_bt_gatt_client_send_write` | `(conn_id, opcode, *p_hdr, *p_buf, p_app_ctxt) → status` |
| `wiced_bt_gatt_client_send_read_handle` | `(conn_id, handle, offset, *p_buf, len, auth_req) → status` |
| `wiced_bt_gatt_client_configure_mtu` | `(conn_id, mtu) → status` |
| `wiced_bt_gatt_get_bearer_mtu` | `(conn_id) → uint16_t` |
| `wiced_bt_gatt_client_send_indication_confirm` | `(conn_id, handle) → status` |

---

## Common Operation Types (GATT_OPERATION_CPLT_EVT)

| `p_op->op` | Meaning | Action |
|---|---|---|
| `GATTC_OPTYPE_NOTIFICATION` | Notification received | Process data |
| `GATTC_OPTYPE_INDICATION` | Indication received | Process data + **send confirm** |
| `GATTC_OPTYPE_WRITE_WITH_RSP` | Write confirmed | Check `p_op->status` |
| `GATTC_OPTYPE_WRITE_NO_RSP` | Write command sent | Buffer reusable |
| `GATTC_OPTYPE_READ_HANDLE` | Read response | Data in `att_value` |
| `GATTC_OPTYPE_CONFIG_MTU` | MTU negotiated | New MTU in `response_data.mtu` |

# btstack Error Code Reference

Common error codes encountered during BLE Central/Peripheral development with AIROC BTSTACK.

## Stack Init Errors (from `wiced_bt_stack_init` / BTM_ENABLED_EVT)

| Code | Name | Common Cause | Fix |
|------|------|--------------|-----|
| 0x0000 | `WICED_BT_SUCCESS` | — | Success |
| 0x1fad | (internal firmware error) | `p_ble_advert_cfg` is NULL in config struct, OR radio firmware failed to load | Ensure ALL config struct pointers are non-NULL; verify WiFi init before BLE on combo-radio |
| 0x0001 | `WICED_BT_ERROR` | Generic failure — usually config struct issue | Check `wiced_bt_cfg_settings_t` for missing/invalid fields |

## Connection Errors (from `wiced_bt_gatt_le_connect` / disconnect reason)

| Code | Name | Common Cause | Fix |
|------|------|--------------|-----|
| 0x3E | `HCI_ERR_CONN_FAILED_ESTABLISHMENT` | Connection attempt timed out (peripheral not responding, out of range) | Implement disconnect→rescan retry logic; verify peripheral is advertising |
| 0x08 | `HCI_ERR_CONNECTION_TIMEOUT` | Supervision timeout expired (link lost) | Normal for range issues; implement reconnection |
| 0x13 | `HCI_ERR_PEER_USER` | Peripheral intentionally disconnected | Normal — reconnect if appropriate |
| 0x16 | `HCI_ERR_CONN_CAUSE_LOCAL_HOST` | Local host terminated connection | Intentional disconnect from our side |
| 0x22 | `HCI_ERR_LMP_RESPONSE_TIMEOUT` | Link layer response timeout | Radio interference or peripheral crashed; retry |

## GATT Operation Errors (from `wiced_bt_gatt_status_t`)

| Code | Name | Common Cause | Fix |
|------|------|--------------|-----|
| 0x0000 | `WICED_BT_GATT_SUCCESS` | — | Success |
| 0x0005 | `WICED_BT_GATT_INSUF_AUTHENTICATION` | Characteristic requires pairing/bonding | Implement pairing before GATT operations |
| 0x000E | `WICED_BT_GATT_UNLIKELY_ERROR` | Stack internal error | Retry; if persistent, check buffer allocation |
| 0x0081 | `WICED_BT_GATT_INVALID_CONNECTION_ID` | Connection dropped before operation completed | Check `ble_conn_id != 0` before GATT calls |

## Debugging Tips

- **0x1fad during init:** Binary-search config struct fields — comment out optional fields until init succeeds, then re-add one at a time
- **0x3E on connect:** Not a code bug — RF/timing issue. Always implement retry logic.
- **Silent notification failure:** Check that CCCD write buffer is `static` (not stack-local)
- **Discovery returns no results:** Verify handle range (`s_handle` < `e_handle`) and that service actually exists on peripheral (use LightBlue/nRF Connect to verify)

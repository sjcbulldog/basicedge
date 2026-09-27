# BLE Setup — Code Patterns

All BLE initialization runs inside a FreeRTOS task. Do not call BTSTACK APIs from `main()` before the scheduler starts.

---

## main.c — Task Setup

> ⛔ **CRITICAL: All HAL objects and contexts used after `vTaskStartScheduler()` MUST be `static` or global.**
> Once the FreeRTOS scheduler starts, `main()`'s stack frame is reclaimed. Any pointer to a stack-local variable in `main()` becomes a dangling pointer. This includes UART objects passed to `cy_retarget_io_init()`, timer contexts, and any struct whose address is stored for later callback use.
>
> **Failure mode:** HardFault in an unrelated FreeRTOS task (e.g., BT HCI TX/RX) whose stack allocation overwrites the dead `main()` locals. Extremely difficult to diagnose — the crash site (e.g., `Cy_SCB_WriteTxFifo`) appears to be a peripheral access fault, but the real cause is a corrupted object pointer.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "cy_retarget_io.h"
#include "mtb_hal.h"

#define BLE_TASK_STACK_SIZE  (1024 * 4)   /* 4 KB — sufficient for GATT operations */
#define BLE_TASK_PRIORITY    (4)

extern void ble_task(void *arg);

int main(void)
{
    /* ⛔ MUST be static — main's stack is reclaimed by FreeRTOS scheduler */
    static mtb_hal_uart_t debug_uart_hal_obj;
    static cy_stc_scb_uart_context_t debug_uart_context;

    cy_rslt_t result = cybsp_init();
    if (CY_RSLT_SUCCESS != result) { CY_ASSERT(0); }

    __enable_irq();

    /* retarget-io stores a pointer to debug_uart_hal_obj internally */
    cy_retarget_io_init(&debug_uart_hal_obj);

    if (pdPASS != xTaskCreate(ble_task, "BLE Task", BLE_TASK_STACK_SIZE,
                              NULL, BLE_TASK_PRIORITY, NULL))
    {
        CY_ASSERT(0);
    }
    vTaskStartScheduler();
    for (;;) {}  /* Should never reach here */
}
```

> **Stack sizing:** 4096 words is minimum for BLE with GATT operations. Increase to 8192 if using custom security callbacks or complex discovery sequences.

---

## Platform HCI Transport Initialization

HCI transport configuration depends on which HAL the platform uses. The copilot-instructions file declares the active HAL for the target device — use that to select the correct pattern.

### mtb_hal Platforms (PSE84 / PSoC Edge)

On `mtb_hal` platforms, `wiced_bt_stack_init()` automatically configures HCI transport using `CYBSP_BT_PLATFORM_CFG_*` macros from the BSP's `cybsp_bt_config.h`. **No explicit HCI init call is needed.**

```c
#include "wiced_bt_stack.h"
#include "cybsp_bt_config.h"   /* BSP macros used internally by btstack-integration */

void ble_task(void *arg)
{
    /* On mtb_hal: wiced_bt_stack_init() auto-configures HCI transport.
     * Do NOT call cybt_platform_config_init() — it does not exist on this platform. */
    wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings);

    /* FreeRTOS task loop — BLE events arrive via callbacks */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### cyhal Platforms (PSOC 6, CYW20829, XMC7000)

On `cyhal` platforms, `cybt_platform_config_init()` **MUST** be called before `wiced_bt_stack_init()`. This configures the HCI transport to the AIROC radio. Omitting this call causes `wiced_bt_stack_init()` to fail silently or timeout.

```c
#include "cybt_platform_config.h"
#include "cybsp_bt_config.h"          /* BSP-provided HCI config struct */
#include "wiced_bt_stack.h"

void ble_task(void *arg)
{
    /* Step 1: Configure HCI transport using BSP-provided config (MUST be first) */
    cybt_platform_config_init(&cybsp_bt_platform_cfg);

    /* Step 2: Initialize BLE stack */
    wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings);

    /* FreeRTOS task loop — BLE events arrive via callbacks */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### How to Determine Which Pattern to Use

The copilot-instructions file (generated at project creation) specifies the HAL. Additionally:
- If the BSP defines `CY_USING_HAL` → use cyhal pattern (call `cybt_platform_config_init`)
- If the BSP uses `mtb_hal_*` APIs → use mtb_hal pattern (no explicit HCI init call)
- If uncertain: search the BSP for `cybt_platform_config` — if no results, use the mtb_hal pattern

> **To verify on cyhal platforms:** After `make getlibs`, search the BSP directory:
> `grep -r "cybt_platform_config" ../mtb_shared/<bsp-name>/`

For advanced HCI configuration (custom baud rate, sleep mode, trace routing), see:
→ `../mtb_shared/btstack-integration/<ver>/README.md` §Platform HCI transport configuration
→ BSP header files for the default configuration values

---

## BLE Management Callback

This callback handles all stack lifecycle events. The `BTM_ENABLED_EVT` is where GATT registration and advertising must be initiated.

```c
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "app_bt_gatt_db.h"

wiced_result_t app_bt_management_callback(
    wiced_bt_management_evt_t event,
    wiced_bt_management_evt_data_t *p_event_data)
{
    switch (event)
    {
        case BTM_ENABLED_EVT:
            if (WICED_BT_SUCCESS == p_event_data->enabled.status)
            {
                printf("BLE stack initialized successfully\n");

                /* Register GATT event handler */
                wiced_bt_gatt_register(app_gatt_event_callback);

                /* Load GATT database */
                wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);

                /* Start advertising (Peripheral) or scanning (Central) */
                app_bt_start_advertising();  /* See role-specific section below */
            }
            else
            {
                printf("BLE stack init failed: 0x%x\n", p_event_data->enabled.status);
            }
            break;

        case BTM_DISABLED_EVT:
            printf("BLE stack disabled\n");
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            printf("Advertisement state: %d\n",
                   p_event_data->ble_advert_state_changed);
            break;

        default:
            break;
    }
    return WICED_BT_SUCCESS;
}
```

---

## Peripheral — Advertising

> ⚠️ **31-Byte Advertising PDU Limit (BLE 4.x legacy advertising):** The advertising payload has a maximum of 31 bytes total. Each element has 2 bytes overhead (length + type), so usable data space is limited. A 128-bit UUID alone consumes 18 bytes (2 overhead + 16 UUID). Plan content carefully — if the UUID + Flags + Name exceeds 31 bytes, move the UUID to a Scan Response packet.

```c
void app_bt_start_advertising(void)
{
    wiced_bt_ble_advert_elem_t adv_elem[3];
    uint8_t num_elem = 0;

    /* Flags — 3 bytes total (1 len + 1 type + 1 value) */
    uint8_t adv_flags = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
    adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_FLAG;
    adv_elem[num_elem].len = sizeof(adv_flags);
    adv_elem[num_elem].p_data = &adv_flags;
    num_elem++;

    /* Complete Local Name — (2 + strlen) bytes */
    static char device_name[] = APP_BT_DEVICE_NAME;
    adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
    adv_elem[num_elem].len = strlen(device_name);
    adv_elem[num_elem].p_data = (uint8_t *)device_name;
    num_elem++;

    /* Service UUID (16-bit or 128-bit, depending on GATT DB)
     * ⚠️ 128-bit UUID = 18 bytes — may not fit in ADV alongside name.
     *    If total exceeds 31 bytes, move UUID to scan response (see below). */

    wiced_bt_ble_set_raw_advertisement_data(num_elem, adv_elem);
    wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                  BLE_ADDR_PUBLIC, NULL);
}
```

### Scan Response — For 128-bit UUIDs or Long Names

When the primary ADV packet cannot hold all data (especially 128-bit UUIDs), use a scan response packet:

```c
void app_bt_set_scan_response(void)
{
    wiced_bt_ble_advert_elem_t scan_elem[1];
    uint8_t num_elem = 0;

    /* 128-bit service UUID in scan response — 18 bytes (2 overhead + 16 UUID) */
    static const uint8_t service_uuid_128[16] = {
        /* Little-endian byte order — reverse of standard UUID representation */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    scan_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE;
    scan_elem[num_elem].len = sizeof(service_uuid_128);
    scan_elem[num_elem].p_data = (uint8_t *)service_uuid_128;
    num_elem++;

    wiced_bt_ble_set_raw_scan_response_data(num_elem, scan_elem);
}
```

Call `app_bt_set_scan_response()` once, immediately before or after `wiced_bt_ble_set_raw_advertisement_data()`.

### ADV Size Budget

| Element | Bytes | Notes |
|---|---|---|
| AD Flags | 3 | Always required (1 len + 1 type + 1 flags) |
| 16-bit UUID (complete) | 4 | 1 len + 1 type + 2 UUID |
| 128-bit UUID (complete) | 18 | 1 len + 1 type + 16 UUID |
| Complete Name (N chars) | N+2 | 1 len + 1 type + N chars |
| **Total budget** | **31** | Exceeding silently truncates or fails |

> **Restart on disconnect:** Always restart advertising in the GATT disconnect handler, or the device becomes invisible after the first connection ends.

---

## GATT Server Patterns

See [gatt-server-patterns.md](./gatt-server-patterns.md) for:
- Complete GATT event handler (all 8 events with mandatory buffer management)
- Attribute request handler (all mandatory ATT opcodes)
- Read/write handler stubs
- `wiced_bt_cfg_settings_t` configuration template with non-NULL requirements

---

## Central Role Patterns

See [central-patterns.md](./central-patterns.md) for:
- Scanning and advertisement parsing
- Notification subscribe with static buffer requirement
- 128-bit UUID little-endian matching

---

## Common Gotchas

| Issue | Symptom | Root Cause | Fix |
|---|---|---|---|
| **Stack-local HAL objects in `main()`** | HardFault in unrelated task (e.g., `Cy_SCB_WriteTxFifo` with corrupted R0) | `vTaskStartScheduler()` reclaims `main()`'s stack; pointers to locals become dangling | Declare ALL objects passed to `cy_retarget_io_init()` or any `*_setup()` as `static` or global |
| **Printf from BT HCI task context** | Watchdog reset (board appears to reboot in a loop) | `printf()` acquires mutex + UART TX blocks; stalls HCI task → BT stack watchdog fires | NEVER add printf/trace calls inside btstack task context. Use a separate logging task or queue. |
| **Tickless idle as diagnostic step for HCI timeouts** | HCI timeout or intermittent radio communication failures (only after ruling out other causes) | `configUSE_TICKLESS_IDLE=2` theoretically enters deep sleep which could disrupt SCBs during HCI firmware download | If HCI timeouts persist after verifying init order and baud config, try `configUSE_TICKLESS_IDLE=0` as a diagnostic step. If this resolves the issue, investigate BT sleep lock integration. |
| Missing `cybt_platform_config_init()` (cyhal only) | Stack init timeout or silent failure | HCI transport not configured | Add call before `wiced_bt_stack_init() — only on cyhal platforms. On mtb_hal (PSE84), this function does not exist; HCI is auto-configured. |
| Wrong COMPONENT name | Builds but BLE non-functional | Component doesn't match BSP's BLE platform layer | Check BSP for correct `COMPONENT_*BLE*` directory name |
| Stack-local CCCD buffer | Write succeeds, no notifications | BTSTACK async read of freed memory | Make buffers `static` |
| No advertising restart on disconnect | Device invisible after first session | Advertising not restarted | Call `app_bt_start_advertising()` on disconnect |
| `gatt_register` after `gatt_db_init` | Callbacks never fire | Registration order wrong | Register callback FIRST, then init DB |
| BLE task stack < 4096 | Stack overflow during GATT discovery | Insufficient stack for deep call chains | Use 4096+ word stack |
| Missing baud rate DEFINE | HCI timeouts on combo/UART chips | Transport runs at wrong speed | Check btstack-integration README for platform-required DEFINES |
| BLE silent on combo radio | `wiced_bt_stack_init()` returns OK but no RF | Radio FW not loaded (WiFi didn't init first) | See `mtb-wifi-ble-coexistence` skill |
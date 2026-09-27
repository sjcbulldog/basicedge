# WiFi+BLE Coexistence — Code Patterns

## Critical Initialization Ordering

On combo-radio BSPs, the following order is **mandatory**. Deviating from this sequence causes silent failures.

### mtb_hal Platforms (PSE84 / PSoC Edge)

```
1. cybsp_init()                  ← BSP hardware init
2. [SDIO init — if required]     ← Platform-specific; check BSP (see §SDIO below)
3. cy_wcm_init()                 ← Loads shared radio firmware (WiFi + BLE)
4. [Signal BLE task]             ← FreeRTOS task notification
5. wiced_bt_stack_init()         ← BLE stack init (HCI auto-configured by btstack-integration)
```

> On mtb_hal platforms, `cybt_platform_config_init()` does NOT exist. The HCI transport is auto-configured by `btstack-integration` using BSP macros when `wiced_bt_stack_init()` is called.

### cyhal Platforms (PSOC 6, CYW20829, XMC7000)

```
1. cybsp_init()                  ← BSP hardware init
2. [SDIO init — if required]     ← Platform-specific; check BSP (see §SDIO below)
3. cybt_platform_config_init()   ← Configure BLE HCI transport parameters
4. cy_wcm_init()                 ← Loads shared radio firmware (WiFi + BLE)
5. [Signal BLE task]             ← FreeRTOS task notification
6. wiced_bt_stack_init()         ← BLE stack init (HCI transport now available)
```

> ⚠️ **The #1 combo-radio failure:** Calling `wiced_bt_stack_init()` before `cy_wcm_init()` returns. BLE appears to initialize successfully but does nothing — no advertising, no scanning, no connections. The HCI transport silently has no radio firmware behind it.

---

## main.c — Dual-Task Setup

> ⛔ **CRITICAL: All HAL objects and contexts used after `vTaskStartScheduler()` MUST be `static` or global.**
> Once the FreeRTOS scheduler starts, `main()`'s stack frame is reclaimed. Any pointer to a stack-local variable in `main()` becomes a dangling pointer. This includes UART objects passed to `cy_retarget_io_init()`, timer contexts, and any struct whose address is stored for later callback use.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "cy_retarget_io.h"
#include "mtb_hal.h"

#define WIFI_TASK_STACK_SIZE  (1024 * 10)  /* 10 KB — TLS handshakes need deep stack */
#define WIFI_TASK_PRIORITY    (5)
#define BLE_TASK_STACK_SIZE   (1024 * 4)   /* 4 KB — sufficient for GATT operations */
#define BLE_TASK_PRIORITY     (4)

/* BLE task handle — WiFi task signals this when radio FW is loaded */
TaskHandle_t ble_task_handle = NULL;

extern void wifi_task(void *arg);
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

    /* Create WiFi task (higher priority — loads radio FW first) */
    xTaskCreate(wifi_task, "WiFi Task", WIFI_TASK_STACK_SIZE,
                NULL, WIFI_TASK_PRIORITY, NULL);

    /* Create BLE task (blocks until signaled by WiFi task) */
    xTaskCreate(ble_task, "BLE Task", BLE_TASK_STACK_SIZE,
                NULL, BLE_TASK_PRIORITY, &ble_task_handle);

    vTaskStartScheduler();
    for (;;) {}
}
```

> **Task priority:** WiFi task should be equal or higher priority than BLE task to ensure radio firmware loads before BLE attempts initialization.

---

## WiFi Task — Radio Firmware Load + Signal

```c
#include "cy_wcm.h"

extern TaskHandle_t ble_task_handle;

void wifi_task(void *arg)
{
    cy_rslt_t result;
    cy_wcm_config_t wcm_config = {0};
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;

    /* Platform-specific SDIO init — see §SDIO section below */
    /* app_sdio_init(&wcm_config);  ← uncomment if BSP requires it */

    /* Initialize WCM — THIS LOADS THE SHARED RADIO FIRMWARE */
    result = cy_wcm_init(&wcm_config);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[WiFi] WCM init failed: 0x%08lx\n", (unsigned long)result);
        CY_ASSERT(0);
    }

    printf("[WiFi] Radio firmware loaded — signaling BLE task\n");

    /* Signal BLE task that HCI transport is now available */
    xTaskNotifyGive(ble_task_handle);

    /* Continue with WiFi connection */
    wifi_connect();

    /* WiFi task main loop */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## BLE Task — Wait for Radio, Then Initialize

```c
#include "cybt_platform_config.h"
#include "cybsp_bt_config.h"
#include "wiced_bt_stack.h"

void ble_task(void *arg)
{
    printf("[BLE] Waiting for radio firmware...\n");

    /* Block until WiFi task signals that radio FW is loaded */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    printf("[BLE] Radio ready — initializing BLE stack\n");

    /* Configure HCI transport (BSP-provided config) */
    cybt_platform_config_init(&cybsp_bt_platform_cfg);

    /* Initialize BLE stack */
    wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings);

    /* BLE task main loop — events arrive via callbacks */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

> **Why `ulTaskNotifyTake` instead of a semaphore?** Task notifications are lighter weight (no kernel object allocation), and this is a one-shot signal. The BLE task blocks exactly once, then never checks again.

---

## SDIO Initialization (Platform-Specific)

Some combo-radio BSPs require explicit SDIO bus initialization before `cy_wcm_init()`. This is common on PSE84 with the CYW55500 radio.

**How to detect if SDIO init is required:**
- Check BSP documentation or example code for `app_sdio_init()` or SDIO HAL references
- Check if `cy_wcm_init()` hangs — this is the symptom of missing SDIO init

**Pattern (when required):**

```c
#include "cyhal_sdio.h"
#include "cyhal_gpio.h"

static cyhal_sdio_t sdio_obj;

void app_sdio_init(cy_wcm_config_t *wcm_config)
{
    cy_rslt_t result;

    /* Initialize SDIO bus to the radio chip */
    result = cyhal_sdio_init(&sdio_obj, CYBSP_WIFI_SDIO_CMD,
                             CYBSP_WIFI_SDIO_CLK, CYBSP_WIFI_SDIO_D0,
                             CYBSP_WIFI_SDIO_D1, CYBSP_WIFI_SDIO_D2,
                             CYBSP_WIFI_SDIO_D3);
    if (CY_RSLT_SUCCESS != result) { CY_ASSERT(0); }

    /* Pass SDIO instance to WCM config */
    wcm_config->wifi_interface_instance = &sdio_obj;
}
```

> **PSE84 CYW55500:** Also requires a reset override function. See `mtb-wifi-stack` code-patterns for the `_cybsp_wifi_reset_wifi_chip()` pattern.

> **PSOC 6 CYW43xxx:** Typically does NOT require explicit SDIO init — `cy_wcm_init()` handles it internally.

---

## WiFi Connection (After Radio FW Load)

```c
#include "cy_wcm.h"

void wifi_connect(void)
{
    cy_rslt_t result;
    cy_wcm_connect_params_t connect_params = {0};
    cy_wcm_ip_address_t ip_address;

    memcpy(connect_params.ap_credentials.SSID, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(connect_params.ap_credentials.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    connect_params.ap_credentials.security = WIFI_SECURITY_TYPE;

    printf("[WiFi] Connecting to %s...\n", WIFI_SSID);

    result = cy_wcm_connect_ap(&connect_params, &ip_address);
    if (CY_RSLT_SUCCESS == result)
    {
        printf("[WiFi] Connected. IP: %d.%d.%d.%d\n",
               (uint8_t)(ip_address.ip.v4), (uint8_t)(ip_address.ip.v4 >> 8),
               (uint8_t)(ip_address.ip.v4 >> 16), (uint8_t)(ip_address.ip.v4 >> 24));
    }
    else
    {
        printf("[WiFi] Connection failed: 0x%08lx\n", (unsigned long)result);
    }
}
```

---

## BLE Provisioning Flow (Q1 = "BLE provisions WiFi")

When BLE is used to receive WiFi credentials:

```c
/* In the GATT write handler — when WiFi SSID characteristic is written */
wiced_bt_gatt_status_t handle_wifi_credential_write(
    wiced_bt_gatt_attribute_request_t *p_attr_req)
{
    uint16_t handle = p_attr_req->data.write_req.handle;

    if (handle == HDLC_WIFI_SSID_VALUE)
    {
        memcpy(provisioned_ssid, p_attr_req->data.write_req.p_val,
               p_attr_req->data.write_req.val_len);
        provisioned_ssid_len = p_attr_req->data.write_req.val_len;
    }
    else if (handle == HDLC_WIFI_PASSWORD_VALUE)
    {
        memcpy(provisioned_password, p_attr_req->data.write_req.p_val,
               p_attr_req->data.write_req.val_len);

        /* Both credentials received — trigger WiFi connection */
        xTaskNotifyGive(wifi_task_handle);
    }

    return WICED_BT_GATT_SUCCESS;
}
```

The WiFi task waits for provisioned credentials before calling `cy_wcm_connect_ap()`:
```c
void wifi_task(void *arg)
{
    /* ... WCM init and BLE signal (as above) ... */

    /* Wait for BLE to provide credentials */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Connect using BLE-provisioned credentials */
    wifi_connect_with_provisioned_creds();
}
```

---

## Radio Time-Division Behavior

The combo-radio firmware manages WiFi/BLE coexistence internally via time-division multiplexing. Key behaviors:

| Scenario | Behavior |
|---|---|
| WiFi idle + BLE active | BLE gets full radio time — max throughput/latency |
| WiFi active + BLE advertising | Both work; BLE advertisement intervals may stretch slightly |
| WiFi heavy traffic + BLE connected | Both work but throughput degrades on both channels |
| WiFi scanning + BLE scanning | Both complete but take longer |

**Application-level optimization:**
- If WiFi is the priority after connection, stop BLE advertising: `wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, ...)`
- If BLE latency is critical, reduce WiFi polling frequency or use longer WiFi DTIM intervals
- No application-level arbitration code is needed — the firmware handles it

---

## Common Failure Patterns

| Pattern | Symptom | Root Cause | Fix |
|---|---|---|---|
| BLE init before WCM | BLE silent — no advertising/scanning | HCI transport has no radio FW | Move `wiced_bt_stack_init()` after `cy_wcm_init()` completes |
| Missing SDIO init | `cy_wcm_init()` hangs forever | Radio chip not accessible on bus | Add platform-specific SDIO init before WCM |
| Heap too small | HardFault during TLS or BLE discovery | `pvPortMalloc()` returns NULL | Increase `configTOTAL_HEAP_SIZE` to ≥128 KB |
| SRAM too small | Intermittent WiFi alloc failures under load | Buffer pool exhausted | Increase CM33 SRAM to ≥384 KB via Device Configurator |
| MQTT reconnect after WiFi drop | Error 0x08060009 (stale socket) | Socket not properly closed before reconnect | Call `cy_mqtt_disconnect()` before `cy_wcm_connect_ap()` retry |
| BLE + WiFi both scanning | Scans take very long | Radio time split between both | Stagger operations — scan one at a time |
| Task notification missed | BLE task never starts | `ble_task_handle` is NULL when WiFi tries to notify | Ensure BLE task is created before WiFi task runs |

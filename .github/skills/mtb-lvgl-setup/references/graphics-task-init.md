# Steps 7-8: Graphics Task Initialization and UART Sharing

## Quick Reference

| Item | Value |
|------|-------|
| **Task location** | `proj_cm55/main.c` or dedicated `gfx_task.c` |
| **Init sequence** | retarget-io → I2C recovery → I2C init → display init → LVGL init → UI |
| **Loop pattern** | `lv_timer_handler()` + `vTaskDelay()` |
| **Common trap** | Skipping `cy_retarget_io_init()` on CM55 → silent crash |

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | **Missing `cy_retarget_io_init()` on CM55** | `printf()` causes mutex assertion → `CY_HALT()` → silent crash |
| G2 | **SDA stuck LOW on I2C bus** | Display I2C init fails with NAK — need I2C bus recovery before init |
| G3 | **Graphics task stack too small** | Use `configMINIMAL_STACK_SIZE * 16` (32 KB) for graphics task |

---

## Step 7: CM55 main.c — Graphics Task

Replace the CM55 stub task with a graphics initialization sequence:

```c
#include "cy_retarget_io.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "mtb_disp_waveshare_4p3.h"

void cm55_gfx_task(void *arg)
{
    cy_rslt_t result;
    
    /* 1. Initialize retarget-io for CM55 printf debugging */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, 115200);
    if (result != CY_RSLT_SUCCESS) {
        CY_HALT();  /* Cannot debug without UART */
    }
    printf("[CM55] Boot OK\r\n");
    
    /* 2. I2C bus recovery (in case SDA stuck LOW from prior transaction) */
    i2c_bus_recovery();
    
    /* 3. Initialize I2C for display/touch */
    result = Cy_SCB_I2C_Init(DISPLAY_I2C_HW, &DISPLAY_I2C_config, &i2c_context);
    Cy_SCB_I2C_Enable(DISPLAY_I2C_HW);
    
    /* 4. Initialize display hardware */
    result = mtb_disp_waveshare_4p3_init();
    if (result != CY_RSLT_SUCCESS) {
        printf("[CM55] Display init failed: 0x%08lX\r\n", (unsigned long)result);
        CY_HALT();
    }
    
    /* 5. Initialize LVGL */
    lv_init();
    lv_port_disp_init();   /* Double-buffered framebuffer */
    lv_port_indev_init();  /* Touch input */
    
    printf("[CM55] LVGL initialized\r\n");
    
    /* 6. Create your UI here */
    create_main_screen();
    
    /* 7. Main loop — drive LVGL timer */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(time_till_next > 0 ? time_till_next : 5));
    }
}
```

### I2C Bus Recovery (SDA Stuck LOW Fix)

On power-up, the display/touch I2C SDA line may be stuck LOW if a transaction was interrupted.
Call bus recovery **before** `Cy_SCB_I2C_Init()`.

> **Implementation:** See the `mtb-device-configurator` skill → [i2c-bus-patterns.md](../../mtb-device-configurator/references/i2c-bus-patterns.md#bus-recovery-bit-bang-9-clocks--stop) for the full bus recovery pattern (9 SCL clocks + STOP condition with HSIOM restore).

---

## Step 8: Dual-Core UART Sharing

Both CM33 and CM55 can share the debug UART (typically SCB2). **Both cores must call `cy_retarget_io_init()`** — without it, CM55 `printf()` causes a mutex assertion and halts.

> **Full guidance:** See the `mtb-retarget-io` skill for configuration, and the `mtb-multicore` agent (Part 8: UART Sharing Between Cores) for interleaving behavior and IPC-based printf relay options.

---

**Next:** Use the **mtb-ipc-patterns** skill for IPC between CM33 and CM55 (Pattern 4: Bidirectional recommended)

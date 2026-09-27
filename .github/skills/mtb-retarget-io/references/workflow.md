# retarget-io Integration Workflow

Detailed initialization code and placement instructions for all three HAL paths
and both runtime environments. Referenced from [SKILL.md](../SKILL.md).

---

## Placement Rule (All Paths)

- **Bare-metal:** Call `cy_retarget_io_init()` in `main()` **after** `init_cycfg_all()` and BSP initialization, **before** any `printf()` call.
- **RTOS:** Call `cy_retarget_io_init()` in `main()` **before** any `printf()` call. Do **not** call `printf()` or any other stdio function before the RTOS kernel starts (`vTaskStartScheduler()` or equivalent).

---

## Path 1 — MTB-HAL (`COMPONENT_MTB_HAL`)

Default path for modern PSE84 and MTB 3.x projects. Requires Device Configurator UART configuration with alias `DEBUG_UART`.

### Includes

Add to `main.c` or the file containing the initialization code:

```c
#include "cy_retarget_io.h"
```

### Bare-Metal Initialization

Insert after `init_cycfg_all()` and BSP init in `main()`:

```c
/* UART context and HAL object for retarget-io (declare at file or function scope) */
cy_stc_scb_uart_context_t DEBUG_UART_context;
mtb_hal_uart_t            DEBUG_UART_hal_obj;
cy_rslt_t                 result;

/* Initialize the SCB UART using PDL-generated config */
result = Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
CY_ASSERT(result == CY_RSLT_SUCCESS);

/* Set up the MTB-HAL UART object over the initialized PDL context */
result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &CYBSP_DEBUG_UART_hal_config,
                             &DEBUG_UART_context, NULL);
CY_ASSERT(result == CY_RSLT_SUCCESS);

/* Enable the UART peripheral */
Cy_SCB_UART_Enable(DEBUG_UART_HW);

/* Initialize retarget-io to use the HAL UART object */
result = cy_retarget_io_init(&DEBUG_UART_hal_obj);
CY_ASSERT(result == CY_RSLT_SUCCESS);

printf("retarget-io initialized\r\n");
```

### RTOS Initialization (FreeRTOS example)

Call `cy_retarget_io_init()` before any `printf()` call. Initialize the UART and retarget-io in `main()` before starting the scheduler:

> ⛔ **CRITICAL: The UART object and context MUST be `static` or file-scope global.**
> `cy_retarget_io_init()` stores a pointer to the HAL object internally. After `vTaskStartScheduler()`, `main()`'s stack frame is reclaimed by FreeRTOS — any stack-local object becomes a dangling pointer. FreeRTOS task stacks will overwrite the dead memory, corrupting the UART object. Any subsequent `printf()` from a FreeRTOS task will dereference the corrupted pointer, causing a HardFault that appears to be in an unrelated SCB peripheral write.

```c
int main(void)
{
    /* ⛔ MUST be static — main's stack is reclaimed after vTaskStartScheduler() */
    static cy_stc_scb_uart_context_t DEBUG_UART_context;
    static mtb_hal_uart_t            DEBUG_UART_hal_obj;
    cy_rslt_t                        result;

    init_cycfg_all();
    /* ... other BSP init ... */

    result = Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &CYBSP_DEBUG_UART_hal_config,
                                 &DEBUG_UART_context, NULL);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    xTaskCreate(app_task, "AppTask", 1024, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

### Optional — Deepsleep Power Management (MTB-HAL only)

Include if the application uses deepsleep:

```c
#include "mtb_syspm_callbacks.h"

/* Register UART deepsleep callback */
Cy_SysPm_RegisterCallback(&mtb_syspm_scb_uart_deepsleep_callback);
```

Add this after `cy_retarget_io_init()`. Skip if deepsleep is not used.

---

## Path 2 — CY-HAL (`CY_USING_HAL` / `PSOC6HAL`)

Used in PSoC 6 projects with the `PSOC6HAL` component. **No Device Configurator configuration required** — the HAL allocates and configures an SCB automatically from the pin assignments. `CYBSP_DEBUG_UART_TX` and `CYBSP_DEBUG_UART_RX` must be available in `cybsp.h`.

### Includes

```c
#include "cy_retarget_io.h"
#include "cybsp.h"        /* for CYBSP_DEBUG_UART_TX / CYBSP_DEBUG_UART_RX */
```

### Bare-Metal Initialization

```c
cy_rslt_t result;

/* HAL acquires and configures the UART from pin assignments internally */
result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                              CY_RETARGET_IO_BAUDRATE);
CY_ASSERT(result == CY_RSLT_SUCCESS);

printf("retarget-io initialized\r\n");
```

`CY_RETARGET_IO_BAUDRATE` is defined by the library as `115200`. Substitute a different baud rate constant if required.

### With Flow Control (optional — when CTS/RTS pins are available)

```c
result = cy_retarget_io_init_fc(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                 CYBSP_DEBUG_UART_CTS, CYBSP_DEBUG_UART_RTS,
                                 CY_RETARGET_IO_BAUDRATE);
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

### RTOS Initialization

Call `cy_retarget_io_init()` in `main()` before starting the scheduler, the same as bare-metal. No task wrapper is required.

---

## Path 3 — PDL-Only (no HAL)

Used when neither `COMPONENT_MTB_HAL` nor `CY_USING_HAL` is defined. Requires Device Configurator UART configuration with alias `DEBUG_UART`.

> **Warning:** PDL-only mode is **not thread-safe**. This path must not be used in RTOS projects. If the project uses or may use an RTOS in the future, use Path 1 (MTB-HAL) instead.

### Includes

```c
#include "cy_retarget_io.h"
```

### Bare-Metal Initialization Only

```c
cy_rslt_t result;

/* Initialize and enable the SCB UART using PDL (no context needed for PDL-only) */
Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, NULL);
Cy_SCB_UART_Enable(DEBUG_UART_HW);

/* Initialize retarget-io with the SCB peripheral pointer */
result = cy_retarget_io_init(DEBUG_UART_HW);
CY_ASSERT(result == CY_RSLT_SUCCESS);

printf("retarget-io initialized\r\n");
```

---

## Buffer and Line Ending Considerations

Communicate these to the user as part of integration — they are not initialization steps but affect correct terminal output.

### Buffer Flushing

For **GCC_ARM, LLVM, and ARM compilers**, offer to add explicit buffer disabling immediately after `cy_retarget_io_init()`:

```c
#include <stdio.h>
setvbuf(stdin,  NULL, _IONBF, 0);
setvbuf(stdout, NULL, _IONBF, 0);
```

Without this, `printf()` output may be delayed or lost if the buffer does not fill completely.

For **IAR**, buffer flushing is not directly controllable. Instruct the user to always end `printf()` strings with `\n` to trigger auto-flushing.

### Line Endings

If `CY_RETARGET_IO_CONVERT_LF_TO_CRLF` is **not** defined, the user must use `\r\n` in all `printf()` strings for correct line display in most UART terminals (PuTTY, TeraTerm, etc.). Recommend enabling `CY_RETARGET_IO_CONVERT_LF_TO_CRLF` to avoid this requirement.

---

## Using printf in Other Source Files

Add `#include "cy_retarget_io.h"` to any `.c` file that uses `printf()` or other STDIO functions. The standard `<stdio.h>` header is implicitly included by `cy_retarget_io.h`.

---

## scanf / STDIN Input

If the user requests STDIN input:

- No additional initialization is needed — STDIN is also redirected by `cy_retarget_io_init()`
- `scanf()` blocks until a newline is received; this is **not suitable** for bare-metal tight loops
- In RTOS projects, `scanf()` should be called from a dedicated task
- Implement STDIN only when explicitly requested by the user

---

## Deinitialization

When UART resources need to be released for other use:

```c
cy_retarget_io_deinit();
```

After calling this, `printf()` and all STDIO functions will no longer produce output. Call `cy_retarget_io_init()` again to re-enable.

---

## API Reference Summary

| Function                                         | HAL Mode     | Description                                                         |
| ------------------------------------------------ | ------------ | ------------------------------------------------------------------- |
| `cy_retarget_io_init(mtb_hal_uart_t *obj)`       | MTB-HAL      | Init with pre-initialized HAL UART object                           |
| `cy_retarget_io_init(CySCB_Type *uart)`          | PDL-Only     | Init with pre-initialized/enabled PDL SCB pointer                   |
| `cy_retarget_io_init(tx, rx, baudrate)`          | CY-HAL       | Init; HAL allocates and configures UART from pins                   |
| `cy_retarget_io_init_fc(tx, rx, cts, rts, baud)` | CY-HAL       | Init with flow control pins                                         |
| `cy_retarget_io_init_hal(void)`                  | CY-HAL       | Init using pre-initialized `cy_retarget_io_uart_obj`                |
| `cy_retarget_io_deinit(void)`                    | All          | Release UART; printf stops working                                  |
| `cy_retarget_io_is_tx_active(void)`              | All          | Returns `true` if TX transactions are pending                       |
| `cy_retarget_io_change_baud_rate(baud, *actual)` | MTB-HAL only | Change baud rate at runtime                                         |
| `cy_retarget_io_init(void)`                      | RTT mode     | Init for SEGGER RTT (no UART; requires `COMPONENT_RETARGET_IO_RTT`) |

All init functions return `CY_RSLT_SUCCESS` on success or an error code on failure.

# UART Runtime Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### RX ring buffer
Use the PDL ring buffer whenever RX can arrive asynchronously or in bursts.

```c
Cy_SCB_UART_StartRingBuffer(HW, rx_ring_buffer, RX_BUF_SIZE, &uart_context);
/* ISR just forwards to the PDL handler */
void uart_isr(void) { Cy_SCB_UART_Interrupt(HW, &uart_context); }

uint32_t available = Cy_SCB_UART_GetNumInRingBuffer(HW, &uart_context);
uint32_t read = Cy_SCB_UART_GetArrayFromRingBuffer(HW, data, chunk_len, &uart_context);
```

Size the ring for the worst burst, not the average stream. Overflow drops new bytes silently.

### Blocking vs non-blocking send
| Pattern | Use when | Trade-off |
|---|---|---|
| `PutArrayBlocking` / `PutString` | Boot banners, simple bare-metal logging | Easy, but occupies the CPU |
| `PutArray` with retry | FreeRTOS tasks, background logging | More code, but does not monopolize the core |

In RTOS tasks, prefer non-blocking writes plus a short retry delay so lower-priority tasks still run.

### Runtime baud-rate change
Safe baud changes are a drain-disable-retime-reenable sequence:

```c
while (!Cy_SCB_UART_IsTxComplete(HW)) {}
Cy_SCB_UART_Disable(HW, &uart_context);
update_clock_divider();
Cy_SCB_UART_Enable(HW);
```

Do not retime the divider while the block is enabled; you will create framing errors on both TX and RX.

### Tickless deep-sleep protection callback
When tickless idle is on, register a SysPm callback that rejects Deep Sleep until TX is physically complete.

```c
case CY_SYSPM_CHECK_READY:
    return Cy_SCB_IsTxComplete(base) ? CY_SYSPM_SUCCESS : CY_SYSPM_FAIL;
```

This protects both application UART traffic and retarget-io output.

## Gotchas

### FreeRTOS BASEPRI may mask UART interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent SCB UART ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### PSOC Edge retarget-io wrapper
On PSOC Edge, the usual `cy_retarget_io_init()` path is not the portable assumption. Use the BSP-provided retarget-io wrapper for the board, or initialize the UART yourself before expecting `printf()` to work.

### `stdout` is line-buffered
A `printf("hello")` with no newline can sit in the buffer and make the firmware look dead. End diagnostics with `\n` or call `fflush(stdout)` when you need immediate output.

---

## Multi-Core Debug Output

For multi-core projects where multiple cores need debug output on a shared UART (e.g., KitProg3 SCB2), see the dedicated reference:

→ [multi-core-debug-output.md](./multi-core-debug-output.md)

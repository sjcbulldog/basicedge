# SDHC Card Lifecycle Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### Read/write polling flow
DMA-backed SDHC reads and writes return before the transfer is finished. After starting a transfer, poll for transfer-complete before reusing the buffer.

```c
status = Cy_SD_Host_Read(HW, &read_cfg, &ctx);
if (status == CY_SD_HOST_SUCCESS)
{
    while ((Cy_SD_Host_GetNormalInterruptStatus(HW) & CY_SD_HOST_XFER_COMPLETE) == 0U) {}
    Cy_SD_Host_ClearNormalInterruptStatus(HW, CY_SD_HOST_XFER_COMPLETE);
}
```

Use the same completion pattern for writes.

### FreeRTOS semaphore completion flow
In RTOS code, let the ISR signal completion and let the task own the timeout policy.

```c
/* ISR */
if (normal_status & CY_SD_HOST_XFER_COMPLETE)
{
    xSemaphoreGiveFromISR(sd_transfer_sem, &xHigherPriorityTaskWoken);
}

/* task */
Cy_SD_Host_Read(HW, &read_cfg, &ctx);
if (xSemaphoreTake(sd_transfer_sem, pdMS_TO_TICKS(1000)) == pdTRUE)
{
    /* safe to consume buffer */
}
```

### Card-detect and reinsertion handling
If a card-detect switch exists, treat insertion/removal as lifecycle events, not just status bits. On removal: stop using the card, drop voltage, and place data pins in a safe state. On insertion: restore voltage, wait for it to settle, restore drive modes, then rerun card init.

### Voltage-restore sequence
A clean reinsertion sequence is:

1. `EnableCardVoltage()`
2. Delay briefly for rail stabilization
3. Restore DAT pin drive mode
4. Re-run card initialization

That order matters; restoring strong drive before voltage is back invites trouble.

## Gotchas

### FreeRTOS BASEPRI may mask SDHC interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent card-detect ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### DAT pins must go HI-Z on removal
When the card is removed, move DAT pins to HI-Z. Leaving them driven after removal risks latch-up and card/socket damage.

### EVK instance mapping
On KIT_PSE84_EVAL_EPC2, **SDHC0 is WiFi SDIO** and **SDHC1 is the microSD slot**. Using SDHC0 for card traffic on the EVK can collide with WiFi and produce very confusing failures.

### Custom-board dependency
On custom boards, either SDHC instance may be correct. The schematic is the source of truth; do not inherit EVK assumptions blindly.

### No PSOC Edge card CE yet
There is no dedicated PSOC Edge SD card code example today. Treat these as distilled lifecycle patterns from the PSOC 6 example plus current `cy_sd_host` behavior, and validate on the actual board early.

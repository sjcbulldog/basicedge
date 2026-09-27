# I2C Bus Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### STOP then bus-idle check
After every STOP in a multi-transaction loop, poll for bus idle before issuing the next START. This avoids the classic case where a slave is slow to release SDA and the next START returns a timeout.

```c
MasterSendStop(...);
for (uint32_t idle = 1000; Cy_SCB_I2C_IsBusBusy(HW) && idle--; )
{
    Cy_SysLib_DelayUs(1);
}
```

Use this in bus scans, register polling loops, and any sequence with repeated START/STOP pairs.

### Complete bus-scan loop
A reliable scan always does four things: probe 0x08-0x77, classify ACK vs NAK vs timeout, always send STOP, then wait for bus idle before trying the next address.

```c
for (uint8_t addr = 0x08; addr <= 0x77; addr++)
{
    status = MasterSendStart(HW, addr, WRITE, 10, &ctx);
    if (status == CY_SCB_I2C_SUCCESS) { /* device present */ }
    MasterSendStop(HW, 10, &ctx);   /* even after NAK or TIMEOUT */
    wait_for_bus_idle();
}
```

### 9-clock bus recovery + STOP
If SDA is stuck low at boot or before a transfer, temporarily switch SCL/SDA to GPIO, clock SCL nine times, then generate STOP while SCL is high. Restore the I2C HSIOM afterward.

```c
set_hsiom_gpio();
for (int i = 0; i < 9; i++)
{
    pulse_scl();
    if (sda_released()) break;
}
generate_stop();          /* SDA low -> high while SCL high */
restore_i2c_hsiom();
```

Use this at boot as a safety net, or at runtime only when SDA is visibly stuck. If you need recovery between normal transactions, fix sequencing first.

### Tickless deep-sleep protection callback
With FreeRTOS tickless idle enabled, register a SysPm callback that vetoes Deep Sleep while the I2C bus is busy.

```c
case CY_SYSPM_CHECK_READY:
    return Cy_SCB_I2C_IsBusBusy(base) ? CY_SYSPM_FAIL : CY_SYSPM_SUCCESS;
```

This is the right fix for "I2C worked until the first idle period, then the bus wedged."

## Gotchas

### FreeRTOS BASEPRI may mask I2C interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent SCB I2C ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### Bus ownership is per-core
There is no automatic arbitration between CM33 and CM55 for one SCB I2C bus. One core must own the bus, its context, and all transfers. Splitting ownership across cores creates random NAKs and corrupted transactions.

### Clock-divider sensitivity
I2C speed follows the actual clock chain, not the intent in your plan. If you change the HF clock or peri divider without updating the SCB divider, 100 kHz can quietly become something else and devices start failing in non-obvious ways.

### Shared GPIO-port IRQ interaction
SCL/SDA activity still toggles GPIO interrupt state on their port. If a button ISR shares that port and its masks are sloppy, I2C traffic can fire the button handler. On shared-port boards, clean up GPIO masks before blaming the bus.

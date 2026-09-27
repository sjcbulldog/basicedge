# SPI Transfer Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### Transfer-method selection
| Scenario | Best method | Why |
|---|---|---|
| 1-3 byte reads/writes, diagnostics, loopback | Polling | Smallest mental model, no ISR required |
| 4-32 byte command or register frames | ISR-driven transfer | Good balance of simplicity and throughput |
| Sustained repetitive transfers over ~32 bytes | DMA-backed transfer | Worth the setup only when CPU offload matters |

### Polling alternative
Use polling when the transfer is short and deterministic.

```c
assert_cs();
for (uint32_t i = 0; i < len; i++)
{
    Cy_SCB_SPI_Write(HW, tx[i]);
    while (Cy_SCB_SPI_GetNumInRxFifo(HW) == 0U) {}
    rx[i] = (uint8_t)Cy_SCB_SPI_Read(HW);
}
deassert_cs();
```

### CS handling patterns
- **Automatic CS:** best when one SCB transaction maps cleanly to one slave frame.
- **Manual CS:** use when the slave needs custom timing, grouped frames, or GPIO-controlled chip select.
- Manual CS rule: assert **before** the first byte and deassert **after** the last byte; never drop CS mid-frame unless the slave datasheet explicitly requires it.

### Inter-frame timing
Some slaves need CS high for a minimum gap between frames. When CS is manual, add the gap explicitly before the next assertion.

```c
deassert_cs();
Cy_SysLib_DelayUs(1);   /* check the slave datasheet */
assert_cs();
```

If CS is automatic, use the SCB timing settings instead of ad-hoc delays.

### Multi-byte register access
Treat `[command][data...]` as one uninterrupted frame.

```c
uint8_t tx[7] = { reg | READ_BIT, 0, 0, 0, 0, 0, 0 };
assert_cs();
transfer(tx, rx, sizeof(tx));
deassert_cs();
/* rx[1..6] hold the register payload */
```

Do not release CS between the command byte and the payload phase unless the slave explicitly documents split framing.

### DMA threshold guidance
DMA is justified for repeated, high-throughput frames. For ordinary sensor reads and command packets, ISR-driven SCB transfer is usually the pragmatic choice; DMA adds descriptors, routing, and debug surface area without much benefit.

### Tickless deep-sleep protection callback
If FreeRTOS tickless idle is enabled, block Deep Sleep while the SPI bus is active.

```c
case CY_SYSPM_CHECK_READY:
    return Cy_SCB_SPI_IsBusBusy(base) ? CY_SYSPM_FAIL : CY_SYSPM_SUCCESS;
```

## Gotchas

### FreeRTOS BASEPRI may mask SPI interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent SCB SPI ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### CPOL/CPHA mismatch is a silent failure
Mode mismatches usually compile, clock, and return bytes that look plausible but are wrong. If the bus is alive but the payload is nonsense, verify the slave's SPI mode first.

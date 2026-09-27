# Audio PDM + I2S Patterns — Patterns & Gotchas

> These patterns supplement the PDL headers. For API signatures and parameters,
> read the actual PDL header files directly (see `pdl-driven-implementation.md`).

## Patterns

### PDM single-IRQ stereo ISR
For stereo capture, bind the RX trigger interrupt to one PDM channel only, then drain both FIFOs in the same ISR. That keeps left/right samples aligned.

```c
if (status & CY_PDM_PCM_INTR_RX_TRIGGER)
{
    for (uint32_t i = 0; i < fifo_trigger_level; i++)
    {
        left  = read_left_fifo();
        right = read_right_fifo();
        push_interleaved(left, right);
    }
}
clear_right_channel_irq();
```

The drain count must match the configured FIFO trigger level.

### PDM pause/resume
Use deactivate/reactivate to pause capture without tearing down the block.

```c
pause:  DeActivate left + right channels
resume: Activate left + right channels
```

This stops and restarts the PDM clock cleanly.

### PDM -> DMA -> I2S pipeline
The scalable audio path is:

```text
PDM mic -> PDM RX FIFO -> DMA (or ISR) -> SRAM ring/buffer -> I2S TX FIFO -> codec -> speaker
```

Use DMA when capture must continue with minimal CPU involvement. Use ISR-only when you need quick proof-of-life or low-rate experimentation.

### Codec bring-up order
Bring up clocks before the codec.

1. BSP init and routing
2. I2S/TDM init
3. Enable TX so MCK is actually output
4. Initialize the codec over I2C
5. Prefill FIFO and activate playback

If the codec starts before MCK is present, it often fails silently because its PLL never locks.

### Sample-rate verification through the BSP clock chain
Do not trust the requested sample rate; trace the generated clock chain.

```text
HF_CLK -> peri divider -> TDM IF clock -> TDM clkDiv -> BCLK -> fs
PDM fs = CLK_IF_SRSS / ((CLOCK_DIV + 1) * CIC_dec * FIR0_dec * FIR1_dec)
```

The generated BSP clock files are the source of truth.

## Gotchas

### FreeRTOS BASEPRI may mask audio interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent PDM/I2S ISRs from firing — even without starting the scheduler. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

### MCLK mismatch
If codec configuration expects one MCLK and the board actually drives another, the codec may initialize but never produce valid audio. Check the codec expectation against the BSP-generated clock chain, not the intended design target.

### Board-specific PDM channel mapping
On KIT_PSE84_EVAL_EPC2, the on-board stereo mic is on channels 2 and 3, not 0 and 1. Using the wrong pair looks like a dead mic but is usually just a routing mistake.

### 16-bit clipping above about 29 dB
The IM7x mic path can clip the left channel above roughly 29 dB in 16-bit mode because of channel DC-offset mismatch. Use a wider word size plus PCM-side gain if you need more headroom.

### FreeRTOS Sleep vs DeepSleep
For audio builds, keep idle mode in **Sleep**, not **DeepSleep**. DeepSleep gates the clocks that both I2S and UART depend on. Also do not disable tickless idle to "fix" audio; on these BSPs that can break the RTOS tick and deadlock retarget-io output.

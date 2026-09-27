# DMA Runtime Patterns - PSOC Edge E84

> **Applies to:** PSOC Edge E84 only
> **Use with:** DMA DW and DMA HPDMA peripherals configured via Device Configurator

## Rules Before Reusing These Patterns

- Finish Device Configurator setup before copying any runtime pattern.
- Place persistent descriptors at file scope; for CM55-owned descriptors use `CY_SECTION(".cy_socmem_data")` and `CY_ALIGN(32)`.
- `X_COUNT` is 0-indexed, and peripheral register transfer size stays `WORD` even when payload size is BYTE or HALFWORD.
- For FIFO or other level-sensitive triggers, set `WAIT_FOR_DEACT`.
- For continuous descriptor loops, set `channelState = CY_DMA_CHANNEL_ENABLED`.
- DMA runs in Active/Sleep only. For HPDMA on CM55, clean cache before enable and invalidate after completion.

## Pattern 1: Ping-Pong Double Buffer

**Placement:** put descriptors and buffers at file scope in `app_dma_stream.c`; call `app_dma_stream_init()` after `init_cycfg_all()` and before enabling the producer peripheral.

```c
static uint16_t ping_buf[BUF_LEN] CY_ALIGN(32);
static uint16_t pong_buf[BUF_LEN] CY_ALIGN(32);
CY_SECTION(".cy_socmem_data") CY_ALIGN(32)
static cy_stc_dma_descriptor_t ping_descr, pong_descr;

void app_dma_stream_init(void)
{
    cy_stc_dma_descriptor_config_t cfg = {
        .descriptorType = CY_DMA_1D_TRANSFER,
        .dataSize = CY_DMA_HALFWORD,
        .srcTransferSize = CY_DMA_TRANSFER_SIZE_WORD,
        .dstTransferSize = CY_DMA_TRANSFER_SIZE_DATA,
        .srcAddress = (void *)&PERIPH_HW->RX_FIFO_RD,
        .srcXincrement = 0,
        .dstXincrement = 1,
        .xCount = BUF_LEN - 1,
        .triggerInType = CY_DMA_1ELEMENT,
        .interruptType = CY_DMA_DESCR,
        .channelState = CY_DMA_CHANNEL_ENABLED,
    };

    cfg.dstAddress = ping_buf; cfg.nextDescriptor = &pong_descr; Cy_DMA_Descriptor_Init(&ping_descr, &cfg);
    cfg.dstAddress = pong_buf; cfg.nextDescriptor = &ping_descr; Cy_DMA_Descriptor_Init(&pong_descr, &cfg);
}
```

## Pattern 2: Error Recovery

**Placement:** keep retry counters at file scope next to the ISR; use this ISR after the normal `Cy_DMA_Channel_SetInterruptMask()` setup.

```c
#define DMA_MAX_RETRIES 3U
static uint32_t dma_retry_count;
static void *g_dma_src, *g_dma_dst;

void app_dma_isr(void)
{
    uint32_t cause = Cy_DMA_Channel_GetStatus(APP_DMA_HW, APP_DMA_CHANNEL);
    Cy_DMA_Channel_ClearInterrupt(APP_DMA_HW, APP_DMA_CHANNEL);

    if (cause == CY_DMA_INTR_CAUSE_COMPLETION) { dma_retry_count = 0U; return; }
    if (++dma_retry_count <= DMA_MAX_RETRIES)
    {
        Cy_DMA_Descriptor_Init(&APP_DMA_Descriptor_0, &APP_DMA_Descriptor_0_config);
        Cy_DMA_Descriptor_SetSrcAddress(&APP_DMA_Descriptor_0, g_dma_src);
        Cy_DMA_Descriptor_SetDstAddress(&APP_DMA_Descriptor_0, g_dma_dst);
        Cy_DMA_Channel_Enable(APP_DMA_HW, APP_DMA_CHANNEL); /* Error auto-disabled it. */
    }
    else
    {
        app_notify_dma_failure(APP_DMA_CHANNEL, cause);
    }
}
```

## Pattern 3: FreeRTOS Task Notification

**Placement:** define the task handle at file scope in the same module as the DMA ISR; notify from the ISR and process the completed buffer in the task.

```c
static TaskHandle_t dma_task_handle;

void app_dma_isr(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t cause = Cy_DMA_Channel_GetStatus(APP_DMA_HW, APP_DMA_CHANNEL);
    Cy_DMA_Channel_ClearInterrupt(APP_DMA_HW, APP_DMA_CHANNEL);

    if (cause == CY_DMA_INTR_CAUSE_COMPLETION)
    {
        vTaskNotifyGiveFromISR(dma_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void dma_task(void *arg)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        process_completed_buffer();
    }
}
```

## Common DMA Peripheral Recipes (Quick Reference)

| Peripheral | Direction | Src/Dst Address | Increments | Trigger Type |
|------------|-----------|-----------------|------------|--------------|
| SPI TX | Mem->Periph | `buf` -> `SCB_HW->TX_FIFO_WR` | src=1, dst=0 | Level |
| SPI RX | Periph->Mem | `SCB_HW->RX_FIFO_RD` -> `buf` | src=0, dst=1 | Level |
| UART TX | Mem->Periph | `buf` -> `SCB_HW->TX_FIFO_WR` | src=1, dst=0 | Level |
| UART RX | Periph->Mem | `SCB_HW->RX_FIFO_RD` -> `buf` | src=0, dst=1 | Level |
| I2S TX | Mem->Periph | `buf` -> `I2S_HW->TX_FIFO_WR` | src=1, dst=0 | Level |
| I2S RX | Periph->Mem | `I2S_HW->RX_FIFO_RD` -> `buf` | src=0, dst=1 | Level |
| ADC | Periph->Mem | `SAR_HW->RESULT` -> `buf` | src=0, dst=1 | Pulse |
| Mem->Mem | Mem->Mem | `src_buf` -> `dst_buf` | src=1, dst=1 | Software or timer |

**Transfer-size reminder:** peripheral side stays `WORD`; memory side tracks `DATA_SIZE`.

## Gotchas

### FreeRTOS BASEPRI may mask DMA completion interrupts
⚠️ If your project links FreeRTOS libraries, BASEPRI may silently prevent DMA completion ISRs from firing — even without starting the scheduler. DMA completion at priority 1 is typically safe, but verify. See [`freertos-interrupt-priority.md`](./freertos-interrupt-priority.md).

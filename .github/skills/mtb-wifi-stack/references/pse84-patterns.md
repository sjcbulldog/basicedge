# WiFi Stack — PSE84 Platform Patterns

> **PSE84 (COMPONENT_PSE84) only.** These initialization steps are required on PSE84 multi-core devices. They are NOT needed on PSOC 6, CYW20829, or XMC7000 targets.

---

## PSE84 main() Additions

These steps are required in `main()` before `xTaskCreate()`.

```c
#include "cy_time.h"        /* for mtb_clib_support_init */
#include "cyabs_rtos.h"     /* for cyabs_rtos_set_lptimer */
#include "cyabs_rtos_impl.h"

/* --- RTC and CLIB support ---
 * Required so stdlib time() works; mbedTLS uses time() for certificate
 * validity checking. */
static mtb_hal_rtc_t rtc_obj;

static void setup_clib_support(void)
{
    Cy_RTC_Init(&CYBSP_RTC_config);
    Cy_RTC_SetDateAndTime(&CYBSP_RTC_config);
    mtb_clib_support_init(&rtc_obj);
}

/* --- LPTimer for FreeRTOS tickless idle ---
 * Allows the CM33 to enter Deep Sleep when the idle task runs.
 * LPTIMER_0 (MCWDT) is configured in Device Configurator. */
#define LPTIMER_0_WAIT_TIME_USEC    (62U)
#define APP_LPTIMER_INTERRUPT_PRIORITY  (1U)

static mtb_hal_lptimer_t lptimer_obj;

static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

static void setup_tickless_idle_timer(void)
{
    cy_stc_sysint_t lptimer_intr_cfg = {
        .intrSrc      = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&lptimer_intr_cfg,
                                             lptimer_interrupt_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    if (CY_MCWDT_SUCCESS != Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW,
                                            &CYBSP_CM33_LPTIMER_0_config))
    {
        handle_app_error();
    }
    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW, CY_MCWDT_CTR_Msk,
                    LPTIMER_0_WAIT_TIME_USEC);

    if (CY_RSLT_SUCCESS != mtb_hal_lptimer_setup(&lptimer_obj,
                               &CYBSP_CM33_LPTIMER_0_hal_config))
    {
        handle_app_error();
    }
    cyabs_rtos_set_lptimer(&lptimer_obj);
}
```

In `main()`, call both setups and enable CM55 before `__enable_irq()`:

```c
int main(void)
{
    cy_rslt_t result = cybsp_init();
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    setup_clib_support();
    setup_tickless_idle_timer();

    /* Initialize retarget-io — see "PSE84 retarget-io Initialization" below */
    init_retarget_io();

    /* Enable CM55 core (AI/DSP). Update CM55_APP_BOOT_ADDR if flash layout changes. */
    #define CM55_BOOT_WAIT_TIME_US  (10U)
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_US);

    __enable_irq();

    if (pdPASS != xTaskCreate(wifi_task, "WiFi Task", WIFI_TASK_STACK_SIZE,
                              NULL, WIFI_TASK_PRIORITY, NULL))
    {
        handle_app_error();
    }
    vTaskStartScheduler();

    handle_app_error();
}
```

---

## PSE84 retarget-io Initialization

> **PSE84 only.** The standard `cy_retarget_io_init(CY_RETARGET_IO_BAUDRATE)` call used on PSoC 6 takes a baud rate integer; on PSE84 the API takes a pre-configured `mtb_hal_uart_t *` object. Using the PSoC 6 pattern will fail to compile on PSE84.

Generate `retarget_io_init.c` (or add to an existing utility source file):

```c
#include "cy_retarget_io.h"
#include "mtb_hal_uart.h"
#include "cybsp.h"

static cy_stc_scb_uart_context_t DEBUG_UART_context;
static mtb_hal_uart_t            DEBUG_UART_hal_obj;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
/* SysPm DeepSleep callback — required to suspend UART before deep sleep */
static mtb_syspm_uart_deepsleep_context_t retarget_io_syspm_ctx = {
    .uart_context = &DEBUG_UART_context,
    .async_context = NULL,
    .tx_pin = { .port  = CYBSP_DEBUG_UART_TX_PORT,
                .pinNum = CYBSP_DEBUG_UART_TX_PIN,
                .hsiom  = CYBSP_DEBUG_UART_TX_HSIOM }
};
static cy_stc_syspm_callback_params_t retarget_io_syspm_params = {
    .context = &retarget_io_syspm_ctx,
    .base    = CYBSP_DEBUG_UART_HW
};
static cy_stc_syspm_callback_t retarget_io_syspm_cb = {
    .callback    = &mtb_syspm_scb_uart_deepsleep_callback,
    .type        = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &retarget_io_syspm_params
};
#endif /* CY_CFG_PWR_MODE_DEEPSLEEP */

void init_retarget_io(void)
{
    cy_rslt_t result;

    result = (cy_rslt_t)Cy_SCB_UART_Init(CYBSP_DEBUG_UART_HW,
                                          &CYBSP_DEBUG_UART_config,
                                          &DEBUG_UART_context);
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    Cy_SCB_UART_Enable(CYBSP_DEBUG_UART_HW);

    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj,
                                &CYBSP_DEBUG_UART_hal_config,
                                &DEBUG_UART_context, NULL);
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    Cy_SysPm_RegisterCallback(&retarget_io_syspm_cb);
#endif
}
```

Declare `void init_retarget_io(void);` in the corresponding header.

---

## PSE84: SDIO Initialization

> **PSE84 only.** On PSE84 the WiFi chip communicates over SDIO. The BSP does not initialize the SDIO HAL object automatically — `app_sdio_init()` must be called before `cy_wcm_init()`. On PSoC 6 and XMC7000 the BSP handles this; do not generate this function for those targets.

```c
#include "mtb_hal_sdio.h"
#include "cy_sd_host.h"

#define APP_SDIO_INTERRUPT_PRIORITY   (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY (2U)
#define APP_SDIO_FREQUENCY_HZ         (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK       (64U)

static mtb_hal_sdio_t            sdio_instance;
static cy_stc_sd_host_context_t  sdhc_host_context;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
/* SysPm DeepSleep callback — required to preserve SDIO/WiFi state
 * when the device enters Deep Sleep. Failure to register this causes
 * WiFi state corruption on wake-up. */
static cy_stc_syspm_callback_params_t sdcard_ds_params = {
    .context = &sdhc_host_context,
    .base    = CYBSP_WIFI_SDIO_HW
};
static cy_stc_syspm_callback_t sdhc_deep_sleep_cb = {
    .callback    = Cy_SD_Host_DeepSleepCallback,
    .type        = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &sdcard_ds_params
};
#endif /* CY_CFG_PWR_MODE_DEEPSLEEP */

static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/* Call this before cy_wcm_init(). Also registers the SDHC DeepSleep
 * callback when Deep Sleep is the configured idle mode. */
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;

    cy_stc_sysint_t sdio_intr_cfg = {
        .intrSrc      = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };
    cy_stc_sysint_t host_wake_intr_cfg = {
        .intrSrc      = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&sdio_intr_cfg,
                                             sdio_interrupt_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    result = mtb_hal_sdio_setup(&sdio_instance,
                                &CYBSP_WIFI_SDIO_sdio_hal_config,
                                &sdhc_host_context);
    if (CY_RSLT_SUCCESS != result) { handle_app_error(); }

    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                    CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size      = SDHC_SDIO_64BYTES_BLOCK;
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    /* Set up GPIO pins for WiFi reset and host-wake interrupt */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM, CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&host_wake_intr_cfg,
                                             host_wake_interrupt_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    Cy_SysPm_RegisterCallback(&sdhc_deep_sleep_cb);
#endif
}
```

After generating `app_sdio_init()`, update the WCM init call in `wifi_task()`:

```c
/* PSE84 only: initialize SDIO bus and set interface instance */
app_sdio_init();
wcm_config.wifi_interface_instance = &sdio_instance;

result = cy_wcm_init(&wcm_config);
if (CY_RSLT_SUCCESS != result) { handle_app_error(); }
```

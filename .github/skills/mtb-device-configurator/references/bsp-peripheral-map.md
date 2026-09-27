# BSP Peripheral Map — KIT_PSE84_EVAL_EPC2

Quick reference for which BSP define maps to which hardware instance. Use these defines in application code — never hardcode hardware addresses or instance numbers.

## SCB Instances

| BSP Define | HW Instance | Function | Patterns Reference |
|---|---|---|---|
| `CYBSP_DEBUG_UART` | SCB2 | Debug UART (115200 default) | [uart-runtime-patterns.md](uart-runtime-patterns.md) |
| `CYBSP_I2C_CONTROLLER` | SCB0 | I2C controller (sensors, PMIC, touch) | [i2c-bus-patterns.md](i2c-bus-patterns.md) |
| `CYBSP_BT_UART` | SCB4 | Bluetooth HCI UART | [uart-runtime-patterns.md](uart-runtime-patterns.md) |
| `CYBSP_EZ_I2C_TARGET` | SCB5 | EZ-I2C target (KitProg3 bridge) | [i2c-bus-patterns.md](i2c-bus-patterns.md) |
| `CYBSP_SPI_CONTROLLER` | SCB10 | SPI controller (radar, ext flash) | [spi-transfer-patterns.md](spi-transfer-patterns.md) |

## TCPWM Instances

| BSP Define | HW Instance | Function | Patterns Reference |
|---|---|---|---|
| `CYBSP_GENERAL_PURPOSE_TIMER` | TCPWM0[0] | General-purpose timer | [pwm-runtime-patterns.md](pwm-runtime-patterns.md) |
| `CYBSP_PWM_LED_CTRL` | TCPWM0[5] | PWM for LED brightness | [pwm-runtime-patterns.md](pwm-runtime-patterns.md) |
| `CYBSP_DEAD_TIME_PWM` | TCPWM0[7] | Dead-time PWM (motor drive) | [pwm-runtime-patterns.md](pwm-runtime-patterns.md) |

## Other Peripherals

| BSP Define | HW Instance | Function | Patterns Reference |
|---|---|---|---|
| `CYBSP_AUTONOMOUS_ANALOG` | AUTANALOG | ADC (autonomous controller) | [autanalog-sampling-patterns.md](autanalog-sampling-patterns.md) |
| `CYBSP_WIFI_SDIO` | SDHC0 | WiFi SDIO interface (CYW55500) — do NOT use for SD card on EVK | — |
| `CYBSP_SDCARD` | SDHC1 | microSD card slot (Port 7, 4-bit bus, CLK_HF[6] @ 100 MHz) | [sdhc-card-lifecycle-patterns.md](sdhc-card-lifecycle-patterns.md) |
| `CYBSP_I3C_CONTROLLER` | I3C_CORE | I3C controller | — |
| `CYBSP_PDM` | PDM0 | Digital microphone (Channels 2/3 on EVK) | [audio-pdm-i2s-patterns.md](audio-pdm-i2s-patterns.md) |
| `CYBSP_TDM_CONTROLLER_0` | TDM_STRUCT0 | Audio TDM/I2S | — |
| `GFXSS_HW` | GFXSS | Graphics subsystem (GPU + Display Controller) | — |

## LED and Button Polarity

LED and button active states vary between boards. **Always use the polarity macros from `cybsp_types.h`** — do NOT assume polarity with raw `Cy_GPIO_Set()` / `Cy_GPIO_Clr()`:

- **LEDs:** Use `CYBSP_LED_STATE_ON` / `CYBSP_LED_STATE_OFF` with `Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_ON)`
- **Buttons:** Use `CYBSP_BTN_PRESSED` / `CYBSP_BTN_OFF` when checking `Cy_GPIO_Read()` return value

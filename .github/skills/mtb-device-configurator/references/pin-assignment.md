# Pin Assignment

When settings require specific pin assignments (e.g., UART RX/TX, I2C SCL/SDA):

1. Check the BSP's `cybsp.h` for named pin macros (e.g., `CYBSP_DEBUG_UART_RX`, `CYBSP_I2C_SCL`) — use these if present
2. To determine which peripherals can be routed to a given pin, inspect the BSP's `gpio_*.h` file (e.g., `gpio_pse84_bga_220.h`) and look up the pin in the `en_hsiom_sel_t` enum. Each entry lists the peripheral signal that can be connected to that pin:
   ```c
   /* P15.1 */
   P15_1_GPIO               =  0,   /* GPIO controls 'out' */
   P15_1_SCB9_SPI_MOSI      = 18,   /* Digital Active - scb[9].spi_mosi */
   P15_1_SCB9_I2C_SDA       = 19,   /* Digital Active - scb[9].i2c_sda:0 */
   P15_1_SCB9_UART_TX       = 20,   /* Digital Active - scb[9].uart_tx:0 */
   ```
   Use this to confirm that the chosen peripheral instance can actually be routed to the requested pin before presenting settings to the user.
3. If no BSP macros exist and the pin routing is ambiguous: check EVK schematic or board documentation
4. If still unknown: prompt the user to confirm pin assignments before presenting the settings table

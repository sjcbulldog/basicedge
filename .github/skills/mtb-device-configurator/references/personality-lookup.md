# Personality Lookup Algorithm

How to determine Device Configurator configuration parameters and their GUI
equivalents from personality XML files. Referenced from [SKILL.md](../SKILL.md).

---

## Personality File Location

```
../mtb_shared/mtb-dsl-<target-device-family>/device-info/personalities/
```

Personality files use `.personality` or `.cypersonality` extensions.
Subdirectories group them by peripheral category. The version number in the
filename matches the personality name shown in Device Configurator
(e.g., `uart3.0.cypersonality` → personality name `"uart3.0"`).

---

## Algorithm

### Step 1 — Find the Personality File

Search for a personality file matching the needed peripheral type:

| Peripheral    | Search pattern                               |
| ------------- | -------------------------------------------- |
| SCB UART      | `uart*.cypersonality`                        |
| SCB I2C       | `i2c*.cypersonality`                         |
| SCB SPI       | `spi*.cypersonality`                         |
| TCPWM PWM     | `pwm*.cypersonality`                         |
| TCPWM Counter | `counter*.cypersonality`                     |
| SAR ADC       | `sar*.cypersonality` or `adc*.cypersonality` |
| DMA (DW)      | `dma*.cypersonality`                         |
| HPDMA (AXI)   | `axidmac*.cypersonality`                     |
| GPIO          | `pin*.cypersonality`                         |
| PDM-PCM       | `pdm_pcm_v2*.cypersonality`                  |
| SDHC (SD/MMC) | `sd_host*.cypersonality`                     |
| I2S/TDM       | `i2s*.cypersonality` or `tdm*.cypersonality` |

Note the full filename — the base name and version become the personality name to
specify in Device Configurator (e.g., `uart3.0`).

---

### Step 2 — Locate ConfigFirmware

Open the file and find the `<ConfigFirmware>` element. **Ignore all other content.**
Only this section contains firmware-relevant information.

```xml
<ConfigFirmware>
  <!-- All relevant content is here -->
  <ConfigDefine ... />
  <ConfigStruct ... />
</ConfigFirmware>
```

---

### Step 3 — Extract ConfigDefine Entries

`<ConfigDefine>` elements produce preprocessor macros in `GeneratedSource/`.
`${INST_NAME}` is replaced by the alias the user enters in Device Configurator.

**Example (SCB UART):**
```xml
<!-- Hardware pointer — pass to PDL/HAL APIs as the hardware instance -->
<ConfigDefine name='`${INST_NAME}`_HW'
              value='SCB`${InstNumber}`'
              public='true' include='true' />

<!-- IRQ enum — use when registering the interrupt handler -->
<ConfigDefine name='`${INST_NAME}`_IRQ'
              value='scb_`${InstNumber}`_interrupt_IRQn'
              public='true' include='true' />
```

With alias `DEBUG_UART`, these generate:
- `DEBUG_UART_HW` → pointer to the SCB hardware register block
- `DEBUG_UART_IRQ` → IRQ number enum for interrupt registration

---

### Step 4 — Extract ConfigStruct Entries

`<ConfigStruct>` elements produce configuration struct instances in `GeneratedSource/`.
The `type` attribute identifies the PDL or HAL configuration struct.

```xml
<ConfigStruct name='`${INST_NAME}`_config'
              type='cy_stc_scb_uart_config_t'
              const='`${inFlash}`'
              public='true' include='true'>
  <Member name='uartMode' value='CY_SCB_UART_STANDARD' />
  <Member name='oversample' value='8' />
  <Member name='dataWidth' value='8' />
  ...
</ConfigStruct>
```

**Important constraint:** Only the struct members that appear as `<Member>` child elements
directly under the `<ConfigStruct>` element in the personality XML are actually configurable
in the Device Configurator GUI. Do not suggest settings for struct members that are absent
from the `<Member>` list — they are either derived automatically, fixed by hardware, or
controlled by other configuration constraints. Furthermore, not all listed `<Member>` elements
may be editable in all configurations due to dependency constraints within the GUI.

With alias `DEBUG_UART`, this generates `DEBUG_UART_config` of type `cy_stc_scb_uart_config_t`.

**Struct type prefix conventions:**
- `cy_stc_` → PDL struct
- `mtb_stc_` → MTB PDL extension struct
- `mtb_hal_` → HAL struct

---

### Step 5 — Look Up the Struct in PDL/HAL Headers

Search the PDL or HAL headers for the struct definition identified in Step 4.
Each member corresponds to a configurable parameter visible in the Device Configurator GUI.

**Example — `cy_stc_scb_uart_config_t` (partial):**
```c
typedef struct {
    cy_en_scb_uart_mode_t       uartMode;        /* Standard, SmartCard, IrDA, LIN */
    uint32_t                    oversample;       /* Oversample factor */
    uint32_t                    dataWidth;        /* 5–9 bits */
    bool                        msb;             /* MSB first */
    cy_en_scb_uart_parity_t     parity;          /* None, Odd, Even */
    cy_en_scb_uart_stop_bits_t  stopBits;        /* 1, 1.5, 2 */
    bool                        enableInputFilter;
    /* ... */
} cy_stc_scb_uart_config_t;
```

---

### Step 6 — Map Struct Members to GUI Fields

Match struct member names to Device Configurator GUI parameter names. The mapping
is generally intuitive but not always 1:1.

**Example mapping (SCB UART):**

| Struct Member | GUI Section  | GUI Setting         |
| ------------- | ------------ | ------------------- |
| `dataWidth`   | General      | Data Width          |
| `parity`      | General      | Parity              |
| `stopBits`    | General      | Stop Bits           |
| `oversample`  | General      | Oversample (derived from baud rate) |
| `msb`         | General      | Bit Order           |
| `enableInputFilter` | General | Enable Input Filter |
| Pin RX        | Connections  | RX                  |
| Pin TX        | Connections  | TX                  |
| Clock divider | Connections  | Clock               |

---

### Step 7 — Build the Settings Table

Using the GUI field names and the values needed for the use case, assemble the
settings table for the user instruction:

- Include **only** settings that differ from Device Configurator defaults for this use case
- Add "leave all others as default" in the instruction
- Use human-readable values matching the GUI dropdown/checkbox labels
  (e.g., `"8 bits"` not `8`, `"None"` not `CY_SCB_UART_NONE`)
- For pin values, use port/pin notation (e.g., `P6[5]`), not `cybsp.h` macro names
- For numeric parameters (dividers, periods, thresholds): check the `<ParamRange>` element
  in the personality XML to confirm the `min` and `max` values. If `min='1'`, the GUI is
  1-based — present the division ratio, not a 0-based register value

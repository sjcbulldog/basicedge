# User Instruction Template

Template and filled example for instructing the user to configure a peripheral
in Device Configurator. Referenced from [SKILL.md](../SKILL.md).

**Output ordering is critical for UX.** The user needs the detailed settings table
visible while Device Configurator is open. If you present details first, they scroll
off screen before the user opens Device Configurator.

---

## Template

```
**Summary:** Configure <PERIPHERAL_BLOCK> as <ALIAS> with <1-line description of key settings>.

⛔ INVOKE `vscode/askQuestions` tool NOW (do NOT output as plain text):

vscode/askQuestions:
  question: "Device Configurator settings are ready. Would you like me to open Device Configurator now?"
  options:
    - "Yes — open Device Configurator"
    - "I'll open it myself"
    - "Skip — already configured"
```

After user responds and Device Configurator is open (or user is ready), present the detailed instructions:

```
Here are the detailed Device Configurator settings:

* Enable <PERIPHERAL_BLOCK> with the alias <ALIAS>
  * Select the "<personality-name.version>" personality
  * Navigation: <Tab> (tab) → <Category> → <Peripheral Block Name>
    (path is approximate — explore nearby tabs/categories if it does not match)
* Set the following parameters (leave all others as default):

| Section   | Setting       | Value  |
| --------- | ------------- | ------ |
| <Section> | <Setting>     | <Value>|

Make sure you SAVE CHANGES before or upon exiting Device Configurator. Let me
know when you are done and I will proceed.
```

---

## Filled Example — Debug UART for retarget-io

**Message 1 (before Device Configurator opens):**
```
**Summary:** Configure SCB2 as DEBUG_UART — 115200 baud, 8N1, RX on P6[5], TX on P6[7].

⛔ INVOKE `vscode/askQuestions` tool here.
```

**Message 2 (after Device Configurator opens):**
```
Here are the detailed Device Configurator settings:

* Enable SCB2 with the alias DEBUG_UART
  * Select the "uart-3.0" personality
  * Navigation: Peripherals (tab) → Communication → Serial Communication Block (SCB) 2
* Set the following parameters (leave all others as default):

| Section      | Setting             | Value                                          |
| ------------ | ------------------- | ---------------------------------------------- |
| General      | Baud Rate (bps)     | 115200                                         |
| General      | Data Width          | 8 bits                                         |
| General      | Parity              | None                                           |
| General      | Stop Bits           | 1 bit                                          |
| Flow Control | Enable Flow Control | False / unchecked                              |
| Connections  | Clock               | 16-bit divider (any reasonable clock source)   |
| Connections  | RX                  | P6[5]                                          |
| Connections  | TX                  | P6[7]                                          |
| API Mode     | API Mode            | High Level                                     |

Make sure you SAVE CHANGES before or upon exiting Device Configurator. Let me
know when you are done and I will proceed.
```

---

## Multi-peripheral Example — UART + DMA

**Message 1 (before Device Configurator opens):**
```
**Summary:** Configure SCB3 (MAIN_UART, 9600 baud) + DMA Channel 0 (UART_DMA_RX) in one session.

⛔ INVOKE `vscode/askQuestions` tool here.
```

**Message 2 (after Device Configurator opens):**
```
Here are the detailed Device Configurator settings. Configure both in the same session:

**1. Enable SCB3 with the alias MAIN_UART**
  * Select the "uart-3.0" personality
  * Navigation: Peripherals (tab) → Communication → Serial Communication Block (SCB) 3

| Section | Setting         | Value  |
| ------- | --------------- | ------ |
| General | Baud Rate (bps) | 9600   |
| General | Data Width      | 8 bits |
| ...     | ...             | ...    |

**2. Enable DMA Channel 0 with the alias UART_DMA_RX**
  * Select the "dma1.0" personality
  * Navigation: DMA (tab) → DMA Channel 0

| Section | Setting          | Value |
| ------- | ---------------- | ----- |
| General | Trigger Input    | SCB3  |
| General | Transfer Width   | Byte  |
| ...     | ...              | ...   |

Make sure you SAVE CHANGES before or upon exiting Device Configurator. Let me
know when you are done and I will proceed.
```

---

## Authoring Notes

- **Two-message pattern:** The brief summary + askQuestion goes in message 1. The detailed
  settings table goes in message 2 (after the user responds to the tool prompt). This ensures
  the detailed instructions are visible while Device Configurator is open.
- **Alias naming**: Use descriptive uppercase names (`DEBUG_UART`, `USER_I2C`, `MAIN_SPI`).
  Avoid generic names like `SCB0` or `UART1` — they are not meaningful to library APIs.
- **Navigation paths**: Always include tab name, category, and peripheral instance name.
- **"Leave all others as default"**: Always state this explicitly to avoid users accidentally
  changing unrelated settings.
- **Settings table**: Include only values that differ from defaults for the use case. Rows for
  default values add noise and distract the user.
- **Pin values**: Use the actual port/pin notation (e.g., `P6[5]`) derived from `cybsp.h` macros
  or board documentation. Do not use macro names in the settings table — Device Configurator
  shows port/pin values in its UI.
- **Clock divider values**: Always present the **1-based division ratio** (what the GUI accepts),
  NOT the 0-based register value. DC subtracts 1 internally. For example, to divide a 100 MHz
  source to 50 kHz, tell the user to enter **2000** — not 1999.

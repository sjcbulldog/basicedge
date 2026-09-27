---
name: mtb-code-style
description: Code style, file organization, README, and documentation standards for ModusToolbox projects.
applyTo: "**/*.c,**/*.h,**/README*"
---

# Code Style & Documentation Standards

## Source File Organization

**Recommendation:** `main.c` should not exceed 150 lines. If it does, refactor to minimize length and move content to new files.

| File Pattern           | Contents                                                            |
| ---------------------- | ------------------------------------------------------------------- |
| `main.c`               | Task creation, `app_main()`, `vTaskStartScheduler()` — startup only |
| `<feature>_task.c/h`   | One file per FreeRTOS task or functional domain                     |
| `app_<peripheral>.c/h` | Peripheral-specific init and runtime logic                          |
| `app_config.h`         | Application-wide constants, buffer sizes, pin mappings              |

**When to decompose:**

- Each FreeRTOS task gets its own source file
- Each peripheral with non-trivial init (DMA, SPI, ADC) gets its own source file
- Shared types/defines go in a common header

**Example:**

```
source/
├── main.c              (task creation, startup)
├── acq_task.c/h        (ADC acquisition + DMA buffer management)
├── spi_task.c/h        (SPI DMA transmission + verification)
├── ui_task.c/h         (button handling, LED, UART reporting)
├── app_dma.c/h         (DMA descriptor init, channel config, error recovery)
└── app_config.h        (buffer sizes, timing constants, pin aliases)
```

## README Documentation

**Rule:** Every project must have README files created early and updated as the project develops.

### Multi-Core Projects (PSOC Edge E84)

| File                  | Content                                                                |
| --------------------- | ---------------------------------------------------------------------- |
| `README.md` (root)    | Application overview, architecture, hardware requirements, build/flash |
| `proj_cm33/README.md` | CM33 responsibilities, peripherals owned, task descriptions            |
| `proj_cm55/README.md` | CM55 responsibilities, ML/DSP workloads, performance notes             |
| `proj_cm0p/README.md` | CM0+ role (if used)                                                    |

### Create READMEs at Phase 1 completion with:

- Project title and one-line description
- Hardware requirements (board, external wiring)
- How to build and flash
- Expected output (serial or hardware observation)
- Which core owns which functionality

⛔ **README is a Phase 1 deliverable.** Create/update BEFORE outputting Phase 1 verification.

### Update at project completion with:

- Full feature description
- Architecture overview (task diagram or data flow)
- Device Configurator settings summary
- Pin connections table
- Inter-core communication summary (if multi-core)
- Troubleshooting notes

## Code Comments

### File Headers

Every `.c` and `.h` file must have a brief header:

```c
/******************************************************************************
 * File: sensor_task.c
 * Description: FreeRTOS task for periodic ADC sampling via DMA transfer.
 *              Owns: SAR ADC, DW Channel 0, sampling timer.
 ******************************************************************************/
```

### Function Headers (Doxygen-style)

All non-trivial functions require a Doxygen header:

```c
/**
 * @brief Initialize DMA channel for ADC continuous sampling.
 * @param[in] buffer Pointer to destination buffer (must be 32-byte aligned)
 * @param[in] length Number of samples per transfer
 * @return CY_DMA_SUCCESS on success, error code on failure
 */
```

### Inline Comments — Rules

| Do                                                       | Don't                                |
| -------------------------------------------------------- | ------------------------------------ |
| Explain **why** (non-obvious decisions, hardware quirks) | State **what** obvious code does     |
| Document register value meanings                         | `/* increment counter */ counter++;` |
| Note timing/ordering dependencies                        | Restate the function name            |
| Mark workarounds with `/* WORKAROUND: <reason> */`       | Comment every single line            |

### Mandatory Comment Points

- **ISR/callbacks:** Document trigger source and expected call frequency
- **Magic numbers:** Use named constants OR inline comment explaining value
- **Init sequences:** Comment ordering dependencies
- **DC dependencies:** `/* Requires DC: <peripheral> configured as <setting> */`

### Example

```c
/* WORKAROUND: SAR2 requires a dummy read after enable to flush stale data
 * (see TRM §25.3.4 — first conversion result is undefined) */
(void)Cy_SAR2_Channel_GetResult(SAR0, 0, NULL);

/* Sampling at 1kHz — Timer period set in Device Configurator (TCPWM0 CNT1) */
/* Requires DC: TCPWM0_CNT1 configured with 1ms period, compare = period/2 */
```

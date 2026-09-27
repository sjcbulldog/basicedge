# Steps 4-6: Dependencies, Driver Files, and FreeRTOSConfig

## Quick Reference

| Item | Value |
|------|-------|
| **Location** | `.mtb` files in `proj_cm55/deps/` |
| **Required libs** | `lvgl`, `display-dsi-waveshare-4-3-lcd`, `touch-ctp-ft5406`, `retarget-io` |
| **After adding** | Run `make getlibs` from project root |
| **Common trap** | `COMPONENTS+=GFXSS` only in CM55 → CM33 build fails |

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | **`COMPONENTS+=GFXSS` only in CM55 Makefile** | CM33 build fails with "cy_graphics.h not found" — add to `common.mk` |
| G2 | **FreeRTOS stack too small** | Blank display, silent crash — set `configMINIMAL_STACK_SIZE = 512` |
| G3 | **Missing `make getlibs`** | Build fails with "library not found" |
| G4 | **Tickless idle enabled** | SCB deep sleep conflict — set `configUSE_TICKLESS_IDLE = 0` for graphics |

---

## Step 4: Add LVGL Dependencies to CM55

### 4A: Create .mtb dependency files in `proj_cm55/deps/`

```
# lvgl.mtb
https://github.com/lvgl/lvgl#v9.2.0#$$ASSET_REPO$$/lvgl/release-v9.2.0

# display-dsi-waveshare-4-3-lcd.mtb
https://github.com/Infineon/display-dsi-waveshare-4-3-lcd#release-v1.0.0#$$ASSET_REPO$$/display-dsi-waveshare-4-3-lcd/release-v1.0.0

# touch-ctp-ft5406.mtb
https://github.com/Infineon/touch-ctp-ft5406#release-v1.0.0#$$ASSET_REPO$$/touch-ctp-ft5406/release-v1.0.0

# retarget-io.mtb (for CM55 printf debugging)
https://github.com/Infineon/retarget-io#release-v1.9.0#$$ASSET_REPO$$/retarget-io/release-v1.9.0
```

Run `make getlibs` from project root after creating files.

### 4B: Update `common.mk` (workspace root)

```makefile
# GFXSS component must be in common.mk (not just CM55 Makefile)
# because Device Configurator generates cy_graphics.h includes in ALL projects
COMPONENTS += GFXSS
```

### 4C: Update CM55 Makefile

```makefile
# Display and touch defines
DEFINES += MTB_DISPLAY_W4P3INCH_RPI
DEFINES += MTB_CTP_FT5406
DEFINES += CY_RETARGET_IO_CONVERT_LF_TO_CRLF

# Include shared headers (for IPC structs, etc.)
INCLUDES += ../include

# CY_IGNORE — exclude unused display/touch drivers and LVGL demo code
CY_IGNORE += $(SEARCH_lvgl)/demos
CY_IGNORE += $(SEARCH_lvgl)/tests
CY_IGNORE += $(SEARCH_lvgl)/examples
```

---

## Step 5: Copy Hardware Driver Files from Reference Project

Copy these files from a working graphics reference project (e.g., Smart Home Panel) to `proj_cm55/`:

| File | Purpose |
|------|---------|
| `lv_conf.h` | LVGL configuration (GPU, fonts, theme) |
| `lv_port_disp.c` / `lv_port_disp.h` | Display driver (framebuffer, GFXSS flush) |
| `lv_port_indev.c` / `lv_port_indev.h` | Touch input driver (FT5406) |
| `display_i2c_config.h` | I2C controller defines (SCB alias) |
| 4× VG-Lite override files | GPU rendering fixes |

> **TIP:** These files are board-specific, not project-specific. If using the same board and display, they can be copied directly without modification.

### Key lv_conf.h Settings for PSOC Edge

```c
#define LV_USE_DRAW_VGLITE          1    /* VG-Lite GPU */
#define LV_USE_VGLITE_BLIT          1
#define LV_USE_VGLITE_DRAW          1
#define LV_USE_VGLITE_DRAW_ASYNC    0    /* Sync for simplicity */
#define LV_USE_OS                   LV_OS_FREERTOS
#define LV_TICK_CUSTOM              1    /* Use FreeRTOS tick */
#define LV_TICK_CUSTOM_INCLUDE      "FreeRTOS.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (xTaskGetTickCount() * (1000 / configTICK_RATE_HZ))
```

---

## Step 6: Update CM55 FreeRTOSConfig.h

The existing CM55 project likely has a small FreeRTOS stack configuration that will crash silently.

```c
// BEFORE (typical for a stub CM55 project):
#define configMINIMAL_STACK_SIZE  128   // 128 words × 4 = 512 bytes — TOO SMALL

// AFTER (required for graphics):
#define configMINIMAL_STACK_SIZE  512   // 512 words × 4 = 2048 bytes minimum
```

The graphics task typically uses `configMINIMAL_STACK_SIZE * 16` = 32 KB.

### Also verify:

```c
#define configTOTAL_HEAP_SIZE     (200 * 1024)  /* At least 200KB for LVGL */
#define configUSE_TICKLESS_IDLE   0             /* Disable for graphics — see SCB deep sleep issue */
```

---

**Next:** [graphics-task-init.md](./graphics-task-init.md) — Graphics task initialization sequence

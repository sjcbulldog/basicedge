# Steps 2-3: Device Configurator GFXSS + MPU Configuration

## Quick Reference

| Item | Value |
|------|-------|
| **Personality** | GFXSS (Graphics Subsystem) in CM55 `design.modus` |
| **DC generates** | `cy_gfxss_config`, clock configs, layer configs |
| **App must verify** | Display shows solid color (not garbled) |
| **Common trap** | MPU not configured non-cacheable → garbled display |

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | **MPU regions do NOT auto-sync with Memory Configurator** | If you move/resize `gfx_mem`, manually update MPU addresses |
| G2 | **Device Configurator may silently fail to save MPU changes** | Verify `cycfg_system.c` after saving — if wrong, close/reopen and re-enter |
| G3 | **Stride alignment for 4.3" display** | 800px must be padded to 832px (32-byte GPU alignment) |
| G4 | **I2C too fast for display** | Error 0x00AA2001 if SCL > 200 kHz — use clock divider = 31 (~184 kHz) |
| G5 | **Dual-core I2C bus conflict** | Only ONE core owns each SCB instance |

---

## Step 2A: GFXSS Personality (CM55 design.modus)

Enable the Graphics Subsystem in the CM55 Device Configurator. These exact parameters are for the **Waveshare 4.3" 800×480 MIPI-DSI display**:

| Parameter | Value | Notes |
|-----------|-------|-------|
| Display Type | `GFX_DISP_TYPE_DSI_DPI` | MIPI-DSI interface |
| Display Width | `832` | 800 + 32 padding (stride alignment) |
| Display Height | `480` | |
| Format | `vivD24` | 24-bit display format |
| FPS | `60` | |
| GPU Enabled | `true` | VG-Lite 2D acceleration |
| Layer 0 Enabled | `true` | |
| Layer 0 Format | `vivRGB565` | 16-bit color (saves memory) |
| Layer 0 Width/Height | `832` / `480` | Match display + stride |
| DSI Lanes | `1` | Single lane |
| Max Per-Lane Mbps | `850` | |
| Mode Flags | `VID_MODE_TYPE_BURST` | Burst mode |
| HBP/HFP/HSYNC | `20` / `210` / `10` | Horizontal timing |
| VBP/VFP/VSYNC | `20` / `20` / `5` | Vertical timing |

> ⚠️ **Stride alignment:** VG-Lite GPU requires 32-byte aligned stride. 800px × 2 bytes (RGB565) = 1600 bytes → pad to 1664 bytes (832px). LVGL display is created at 832×480, visible area clamped to 800×480 via `lv_display_set_resolution()`.

## Step 2B: Clock Configuration

Verify these clocks exist (add if missing):

| Clock | Frequency | Purpose |
|-------|-----------|---------|
| CLK_HF0 | 200 MHz | CM33 core |
| CLK_HF1 | 400 MHz | CM55 core + GPU |
| CLK_HF4 | (GPU clock) | VG-Lite GPU |
| CLK_HF12 | 24 MHz | MIPI DPHY reference |

## Step 2C: I2C Configuration (Touch Controller)

The touch controller (FT5406) uses I2C. If your existing CM33 project uses the same SCB for a sensor:

1. **Transfer SCB ownership to CM55** in Device Configurator
2. Configure: I2C Controller mode, clock divider = 31, highPhaseDutyCycle = 16
3. Target SCL frequency: ~184 kHz (Standard mode)

> ⚠️ **CRITICAL:** The Waveshare 4.3" display requires ~184 kHz I2C. If SCB is configured at 400+ kHz, the display will fail with error 0x00AA2001 (corrupted reads at high speed).

## Step 2D: Display GPIO Pins

Ensure these pin aliases are configured in Device Configurator:

- `CYBSP_DISP_STBYB` — Display standby
- `CYBSP_DISP_RST_PORT` / `CYBSP_DISP_RST_PIN` — Display reset
- `CYBSP_DISP_TE` — Tearing effect sync
- `CYBSP_DISP_BACKLIGHT_PWM` — Backlight brightness

## Step 2E: Secure Config — Non-Secure Access

In `proj_cm33_s` Device Configurator, ensure:
- GFXSS peripheral has non-secure access enabled (PPC/PPU)
- `gfx_mem` SOCMEM region is accessible from non-secure world

---

## Step 3: MPU Configuration — Non-Cacheable Framebuffers

The CM55 D-Cache is enabled by BSP startup code. Framebuffers in SOCMEM default to cacheable. The Display Controller reads directly from physical memory, bypassing CPU cache → **garbled display**.

### Configure via Device Configurator (CM55 MPU panel)

**Add region for `gfx_mem`:**

| Parameter | Value |
|-----------|-------|
| Region | Next available (e.g., Region 3) |
| Start Address | Match `gfx_mem` start (e.g., 0x26200000) |
| Size | Match `gfx_mem` size (e.g., 3 MB = 3145728) |
| Writable | Yes |
| Cacheable | **4 (Normal, Non-Cacheable)** |
| Executable | No |

**If using IPC, add region for `m33_m55_shared`:**

| Parameter | Value |
|-----------|-------|
| Region | Next available (e.g., Region 2) |
| Start Address | Match `m33_m55_shared` start |
| Size | Match shared region size (e.g., 256 KB) |
| Writable | Yes |
| Cacheable | **4 (Normal, Non-Cacheable)** |
| Executable | No |

---

**Next:** [graphics-dependencies.md](./graphics-dependencies.md) — Add dependencies and driver files

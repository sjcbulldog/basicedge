---
name: mtb-lvgl-setup
description: Add LVGL v9 touchscreen graphics with VG-Lite GPU acceleration to a PSOC Edge E84 ModusToolbox project. Covers display drivers, touch input, Device Configurator GFXSS personality, lv_conf.h, framebuffer layout, and the full initialization sequence. Supports both template-based and from-scratch approaches.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Add LVGL Graphics to PSOC Edge E84 Project

## Quick Reference

| Topic | Key Point |
|-------|-----------|
| **Two valid approaches** | Start from LVGL template, or add LVGL to existing project |
| **From-scratch guide** | See [lvgl-integration-overview.md](./references/lvgl-integration-overview.md) — chunked step-by-step workflow |
| **Memory requirement** | ~3 MB SOCMEM for framebuffers + GPU heap |
| **Core assignment** | LVGL runs on **CM55** (graphics subsystem owner) |
| **Key DC personality** | GFXSS (Graphics Subsystem) in CM55 design.modus |

## ⛔ Gotchas

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | **D-Cache coherency** — CM55 D-Cache must be disabled or managed for DMA-driven display. Default `cybsp_init()` enables D-Cache. | Display corruption, partial updates |
| G2 | **Stride alignment** — VG-Lite GPU requires 32-byte stride alignment. 800px → 832px at RGB565. | GPU rendering artifacts |
| G3 | **I2C bus ownership** — Touch controller I2C must be owned by CM55, not CM33. If Matter/WiFi owns I2C on CM33, migrate or use IPC. | Touch not working |
| G4 | **Memory rebalance required** — Default memory layout doesn't allocate SOCMEM for graphics. Must use Memory Configurator. | Linker failures, no FB space |
| G5 | **GFXSS personality is CM55-only** — Don't try to add GFXSS to CM33 design.modus. | DC errors |

---

## Strategy: Choose Your Approach

### Option 1: Start from LVGL Template (Recommended for new projects)

Start from the **Infineon LVGL demo CE** (`mtb-example-psoc-edge-gfx-lvgl-demo`), then add your application logic on top. This is the fastest approach when:
- Starting a new project
- LVGL/display is the primary feature
- No existing WiFi/BLE/Matter code to preserve

```
python .github/skills/mtb-tools/scripts/project_creator_wrapper.py \
  --board-id KIT_PSE84_EVAL_EPC2 \
  --app-id mtb-example-psoc-edge-gfx-lvgl-demo \
  --target-dir <output-dir> \
  --user-app-name MyLVGLProject
```

Then add WiFi, MQTT, BLE, sensors incrementally — these are easier to retrofit than graphics.

### Option 2: Add LVGL to Existing Project (From-scratch)

When your project already has WiFi, BLE, Matter, or sensor configurations that you need to preserve:

**Reference files (read in order):**

| Step | File | What It Covers |
|------|------|----------------|
| Overview | [lvgl-integration-overview.md](./references/lvgl-integration-overview.md) | Prerequisites, architecture, pitfalls checklist |
| 1 | [graphics-memory-layout.md](./references/graphics-memory-layout.md) | Memory Configurator — allocate `gfx_mem` |
| 2-3 | [gfxss-display-config.md](./references/gfxss-display-config.md) | Device Configurator GFXSS + MPU non-cacheable |
| 4-6 | [graphics-dependencies.md](./references/graphics-dependencies.md) | Dependencies, Makefile, FreeRTOSConfig |
| 7-8 | [graphics-task-init.md](./references/graphics-task-init.md) | Graphics task init, UART sharing |
| 9 | Use the **mtb-ipc-patterns** skill | IPC between CM33 and CM55 (Pattern 4: Bidirectional) |

---

## 1. Display Hardware Options

| Display | Size | Resolution | FB Res | Driver Library | Touch Library | `CONFIG_DISPLAY` |
|---|---|---|---|---|---|---|
| Waveshare 4.3" DSI | 4.3" | 800×480 | **832×480** ¹ | `display-dsi-waveshare-4-3-lcd` | `touch-ctp-ft5406` | `W4P3INCH_DISP` |
| Waveshare 7" DSI (C) | 7" | 1024×600 | 1024×600 | `display-dsi-waveshare-7-0-lcd-c` | `touch-ctp-gt911` | `WS7P0DSI_RPI_DISP` |
| EK79007AD3 10.1" TFT | 10.1" | 1024×600 | 1024×600 | `display-tft-ek79007ad3` | `touch-ctp-ili2511` | `WF101JTYAHMNB0_DISP` |

> ¹ 4.3" display: VG-Lite GPU requires stride alignment — 800px padded to 832px at RGB565. LVGL display created at 832×480, visible area clamped to 800×480 via `lv_display_set_resolution()`.

---

## 2. Dependencies — `proj_cm55/deps/` `.mtb` Files

### Core (always required)

```
# lvgl.mtb
https://github.com/lvgl/lvgl#v9.2.0#$$ASSET_REPO$$/lvgl/release-v9.2.0

# freertos.mtb
https://github.com/Infineon/freertos#release-v10.6.202#$$ASSET_REPO$$/freertos/release-v10.6.202

# retarget-io.mtb
https://github.com/cypresssemiconductorco/retarget-io#release-v1.9.0#$$ASSET_REPO$$/retarget-io/release-v1.9.0
```

### Display + touch drivers (include ALL three — unused ones are CY_IGNOREd)

```
# display-dsi-waveshare-4-3-lcd.mtb
https://github.com/Infineon/display-dsi-waveshare-4-3-lcd#release-v1.0.0#$$ASSET_REPO$$/display-dsi-waveshare-4-3-lcd/release-v1.0.0

# display-dsi-waveshare-7-0-lcd-c.mtb
https://github.com/Infineon/display-dsi-waveshare-7-0-lcd-c#release-v1.0.0#$$ASSET_REPO$$/display-dsi-waveshare-7-0-lcd-c/release-v1.0.0

# display-tft-ek79007ad3.mtb
https://github.com/Infineon/display-tft-ek79007ad3#release-v1.0.0#$$ASSET_REPO$$/display-tft-ek79007ad3/release-v1.0.0

# touch-ctp-ft5406.mtb
https://github.com/Infineon/touch-ctp-ft5406#release-v1.0.0#$$ASSET_REPO$$/touch-ctp-ft5406/release-v1.0.0

# touch-ctp-gt911.mtb
https://github.com/Infineon/touch-ctp-gt911#release-v1.0.0#$$ASSET_REPO$$/touch-ctp-gt911/release-v1.0.0

# touch-ctp-ili2511.mtb
https://github.com/Infineon/touch-ctp-ili2511#release-v1.0.0#$$ASSET_REPO$$/touch-ctp-ili2511/release-v1.0.0
```

Run `make getlibs` after creating all `.mtb` files.

---

## 3. Makefile Configuration

### `common.mk` (workspace root — shared by all cores)

```makefile
CONFIG_DISPLAY = W4P3INCH_DISP
COMPONENTS+=GFXSS
```

### `proj_cm55/Makefile`

```makefile
CORE=CM55
CORE_NAME=CM55_0

COMPONENTS+=FREERTOS RTOS_AWARE
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF _BAREMETAL=0 CY_RTOS_AWARE

# Display-conditional driver selection
ifeq ($(CONFIG_DISPLAY), WF101JTYAHMNB0_DISP)
DEFINES += MTB_DISPLAY_EK79007AD3 MTB_CTP_ILI2511
CY_IGNORE += $(SEARCH_display-dsi-waveshare-7-0-lcd-c) $(SEARCH_touch-ctp-gt911)
CY_IGNORE += $(SEARCH_display-dsi-waveshare-4-3-lcd) $(SEARCH_touch-ctp-ft5406)
else ifeq ($(CONFIG_DISPLAY), WS7P0DSI_RPI_DISP)
DEFINES += MTB_DISPLAY_WS7P0DSI_RPI MTB_CTP_GT911
CY_IGNORE += $(SEARCH_display-tft-ek79007ad3) $(SEARCH_touch-ctp-ili2511)
CY_IGNORE += $(SEARCH_display-dsi-waveshare-4-3-lcd) $(SEARCH_touch-ctp-ft5406)
else ifeq ($(CONFIG_DISPLAY), W4P3INCH_DISP)
DEFINES += MTB_DISPLAY_W4P3INCH_RPI MTB_CTP_FT5406
CY_IGNORE += $(SEARCH_display-tft-ek79007ad3) $(SEARCH_touch-ctp-ili2511)
CY_IGNORE += $(SEARCH_display-dsi-waveshare-7-0-lcd-c) $(SEARCH_touch-ctp-gt911)
endif

# LVGL: ignore unused modules and stock files replaced by Infineon overrides
CY_IGNORE += $(SEARCH_lvgl)/src/others/vg_lite_tvg $(SEARCH_lvgl)/src/libs/thorvg $(SEARCH_lvgl)/tests
CY_IGNORE += $(SEARCH_lvgl)/draw/nxp $(SEARCH_lvgl)/draw/renesas $(SEARCH_lvgl)/examples
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_vg_lite_utils.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite_img.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/sw/blend/helium $(SEARCH_lvgl)/src/draw/sw/blend/neon
CY_IGNORE += $(SEARCH_lvgl)/src/core/lv_refr.c
CY_IGNORE += $(SEARCH_lvgl)/demos
```

> ⚠️ The CY_IGNORE for `lv_draw_vg_lite.c`, `lv_draw_vg_lite_img.c`, `lv_vg_lite_utils.c`, and `lv_refr.c` are **critical** — Infineon provides custom overrides for these files (see §7). Without CY_IGNORE you get duplicate symbol errors.

---

## 4. Device Configurator — GFXSS Personality

**The single most important non-obvious requirement.** The GFXSS personality in `design.modus` generates:

| Symbol | Purpose |
|---|---|
| `GFXSS_DC_IRQ` | Display Controller interrupt |
| `GFXSS_GPU_IRQ` | GPU interrupt |
| `GFXSS_config` | GFXSS configuration structure |
| `GFXSS_GFXSS_MIPIDSI` | MIPI-DSI base address |
| `GFXSS_GFXSS_GPU_GCNANO` | GPU register base |
| `DISPLAY_I2C_CONTROLLER_HW` | I2C for touch controller |

**Without it:** Build fails with dozens of undefined symbol errors. No workaround with `#define`.

**How to get it:**
- **From LVGL template:** Already configured (no action needed)
- **Adding to existing project:** Copy entire `bsps/` directory from a working LVGL demo CE built for the same BSP, then rebuild

---

## 5. `lv_conf.h` — Critical Settings

Place in `proj_cm55/` root. Must be on include path before LVGL headers.

```c
#define LV_COLOR_DEPTH 16                           /* RGB565 for DSI */
#define LV_USE_OS   LV_OS_FREERTOS                  /* FreeRTOS integration */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* GPU acceleration */
#define LV_USE_DRAW_VG_LITE 1
#define LV_USE_DRAW_SW 1                            /* Software fallback */

/* GPU stride/buffer alignment — required for VG-Lite DMA */
#define LV_DRAW_BUF_STRIDE_ALIGN   64
#define LV_DRAW_BUF_ALIGN          64
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 64
#define LV_ATTRIBUTE_MEM_ALIGN      __attribute__((aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))

/* Memory placement */
#define LV_ATTRIBUTE_FAST_MEM       CY_SECTION(".cy_itcm")          /* ITCM for hot functions */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY CY_SECTION(".cy_socmem_data")  /* SoC shared memory */

/* Float + matrix for transforms */
#define LV_USE_FLOAT  1
#define LV_USE_MATRIX 1

/* Refresh: 18ms ≈ 55 FPS */
#define LV_DEF_REFR_PERIOD  18

/* Font strategy: enable only sizes you use (~20-60 KB each) */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
```

> **Full file:** Copy `lv_conf.h` from the LVGL demo CE and modify. The file is ~800 lines.

---

## 6. Display Driver — `lv_port_disp.c`

```c
/* Framebuffers MUST be in .cy_gpu_buf section (GPU DMA accessible) */
CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN
    uint8_t disp_buf1[MY_DISP_HOR_RES * MY_DISP_VER_RES * BYTE_PER_PIXEL];
CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN
    uint8_t disp_buf2[MY_DISP_HOR_RES * MY_DISP_VER_RES * BYTE_PER_PIXEL];

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    vg_lite_finish();  /* CRITICAL: wait for GPU before swapping */

    /* Drain stale DC VSYNC notification — prevents tearing on continuous animations.
     * Without this, ulTaskNotifyTake() below returns immediately with a stale
     * notification queued during GPU rendering, before DC latches new buffer. */
    ulTaskNotifyTake(pdTRUE, 0);  /* Non-blocking drain */

    Cy_GFXSS_Set_FrameBuffer((GFXSS_Type*) GFXSS, (uint32_t*) color_p, &gfx_context);

    /* Wait for fresh VSYNC confirming DC latched the new buffer address */
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
        lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    lv_display_t *disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_tick_set_cb(xTaskGetTickCount);
    lv_display_set_buffers(disp, disp_buf1, disp_buf2, sizeof(disp_buf1),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_resolution(disp, ACTUAL_DISP_HOR_RES, ACTUAL_DISP_VER_RES);
}
```

---

## 7. VG-Lite Override Files (MUST copy from Infineon LVGL demo)

Copy these 4 files from `mtb-example-psoc-edge-lvgl-demo` into `proj_cm55/`:

| File | Replaces |
|---|---|
| `lv_draw_vg_lite.c` | `lvgl/src/draw/vg_lite/lv_draw_vg_lite.c` |
| `lv_draw_vg_lite_img.c` | `lvgl/src/draw/vg_lite/lv_draw_vg_lite_img.c` |
| `lv_vg_lite_utils.c` | `lvgl/src/draw/vg_lite/lv_vg_lite_utils.c` |
| `lv_refr.c` | `lvgl/src/core/lv_refr.c` |

These contain Infineon-specific adaptations for the GFXSS hardware. **Do NOT write from scratch.**

---

## 8. Initialization Sequence (order matters)

> **CRITICAL — FreeRTOS Stack Sizing:** Set `configMINIMAL_STACK_SIZE = 512` (32 KB) in CM55's `FreeRTOSConfig.h`. Non-graphics PSOC Edge projects typically default to 128 (1 KB), which causes immediate stack overflow when LVGL initializes. Stack overflow manifests as HardFault with no useful backtrace. Also set `configENABLE_MVE = 1` for Helium/MVE SIMD register preservation during context switches — VG-Lite uses MVE instructions.

### `main()` — before FreeRTOS scheduler

```
1. cybsp_init()
2. SCB_InvalidateDCache_by_Addr(0x240BD000, 0x43000)  ← Flush stale D-cache
3. __enable_irq()
4. xTaskCreate(cm55_gfx_task, "GFX", 8192, ...)
5. vTaskStartScheduler()
```

> **Boot-time D-Cache invalidation:** After `cybsp_init()` in CM55 `main.c`, invalidate D-Cache for BOTH the IPC shared memory region AND the `gfx_mem` framebuffer region. Stale cache lines from the boot window (before MPU configured non-cacheable regions) persist and cause garbled display output or IPC corruption:
>
> ```c
> cybsp_init();
> SCB_InvalidateDCache_by_Addr((void *)IPC_SHARED_BASE, IPC_SHARED_SIZE);
> SCB_InvalidateDCache_by_Addr((void *)GFX_MEM_BASE, GFX_MEM_SIZE);
> init_retarget_io();
> ```

### `cm55_gfx_task()` — inside FreeRTOS context

```
1.  Configure GFXSS_config (MIPI, dimensions, framebuffer)
2.  Cy_GFXSS_Init()              ← Graphics Subsystem
3.  Register DC + GPU interrupts via NVIC
4.  Cy_SCB_I2C_Init()            ← I2C for touch
5.  vTaskDelay(500ms)            ← Let display stabilize
6.  Display panel init           ← mtb_disp_waveshare_4p3_init() etc.
7.  vg_lite_init_mem()           ← VGLite heap allocation
8.  vg_lite_init(W/4, H/4)      ← VGLite engine
9.  lv_init()                    ← LVGL core
10. lv_port_disp_init()          ← Display driver
11. lv_port_indev_init()         ← Touch input
12. your_ui_create()             ← Your UI
13. Loop: lv_timer_handler()     ← LVGL main loop (clamp delay ≥ 1ms)
```

### Sensor Migration (Shared I2C Bus)

If your existing project has sensors on the same SCB as the display touch controller (typically SCB0), only one core can own that SCB. To resolve:
1. Remove sensor initialization from CM33 code
2. Add sensor driver to CM55 — initialize on the same SCB after display/touch I2C init
3. Publish sensor readings via IPC shared memory back to CM33
4. Add stale-data timeout on CM33 (if CM55 stops publishing, use last-known value)

> **I2C SDA stuck LOW:** If the display or sensor leaves SDA low at boot (common after power glitches), I2C init fails. Implement bus recovery by configuring SCL as GPIO and toggling 9 clock cycles before I2C init.

---

## 9. Memory Budget and Device Configurator Memory Configuration

### 9a. Display Memory Sizing

| Item | Formula | 4.3" (832×480) | 7"/10.1" (1024×600) |
|---|---|---|---|
| Framebuffer × 2 | `FB_W × FB_H × 2 (RGB565) × 2` | ~1.6 MB | ~2.4 MB |
| VGLite heap | `LV_VG_LITE_THORVG_BUF_ADDR_ALIGN` region | ~245 KB | ~245 KB |
| **Total gfx_mem needed** | FB + VGLite + headroom | **~2.0 MB min** | **~2.85 MB min** |
| FreeRTOS heap (min) | Depends on task count | 50 KB | 50 KB |
| GFX task stack | `lv_timer_handler` depth | 32 KB | 32 KB |
| LVGL working memory | Widgets, styles, draw buffers | 200–400 KB | 300–600 KB |

> **Framebuffer formula:** `width × height × bytes_per_pixel × num_buffers`
> For 4.3" DSI: `832 × 480 × 2 × 2 = 1,597,440 bytes` (~1.52 MB)
> VG-Lite GPU requires stride-aligned width (832, not 800) for DMA.

### 9b. gfx_mem Sizing (CRITICAL for Display Projects)

The `gfx_mem` region in SOCMEM holds framebuffers and VG-Lite GPU heap. Undersized = display corruption or GPU faults.

**Sizing formula:**
```
gfx_mem_bytes = (FB_WIDTH × FB_HEIGHT × 2 × NUM_BUFFERS) + VGLITE_HEAP_SIZE + HEADROOM

Example (4.3" display, 3 MB gfx_mem = 0x300000):
  Framebuffers: 832 × 480 × 2 × 2 = 1,597,440 bytes (1.52 MB)
  VG-Lite heap: 250,880 bytes (0.24 MB)
  Total used:   1,808 KB of 3,072 KB (59% — healthy headroom)
```

**Utilization target:** 50-75%. Below 50% = wasting SOCMEM that CM55 could use. Above 80% = risk of VG-Lite allocation failures under load.

### 9c. Device Configurator Memory Configuration

The **Memory Configuration** personality in `design.modus` controls how SOCMEM is partitioned. Changes generate linker scripts in `bsps/TARGET_*/config/GeneratedSource/`.

**To adjust gfx_mem:**
1. Open `design.modus` → Memory Configuration
2. Resize `gfx_mem` based on the formula above
3. Adjust `m55_data_secondary` to compensate (regions are contiguous)
4. Verify totals: SOCMEM regions must sum to `0x500000` (5 MB)
5. Save → clean rebuild required: `make clean && make build`

| Common Issue | Symptom | Fix |
|---|---|---|
| `gfx_mem` too small | Display corruption, partial rendering | Grow `gfx_mem`, shrink `m55_data_secondary` |
| CM33 data overflow (WiFi+MQTT) | Linker error: `.data`/`.bss` overflow | Shrink `m33_code`, grow `m33_data` in System SRAM |

> **Resize ordering:** When rebalancing memory regions (e.g., shrinking `m55_data_secondary` to grow `gfx_mem`), always shrink first and save, then grow and save again. The Memory Configurator validates intermediate states — attempting both changes simultaneously causes overlap rejection errors.

---

## 10. VG-Lite Performance — Avoiding Flicker and Frame Drops

### The Layer Compositing Trap (CRITICAL)

**When a container/parent object has `opa < LV_OPA_COVER` (255) and has children, LVGL allocates a temporary layer buffer, renders all children into it, then composites the layer onto the framebuffer at the target opacity — EVERY FRAME the object is dirty.**

This is the #1 cause of animation flicker on PSOC Edge E84.

**Bad pattern — container opacity animation (forces layer compositing):**
```c
/* DON'T: animating opa on a group with children */
lv_obj_set_style_opa(group, LV_OPA_TRANSP, LV_PART_MAIN);
lv_anim_set_exec_cb(&a, set_opa);  /* changes group opa → layer alloc per frame */
```

**Good pattern — per-child opacity animation (no layers needed):**
```c
/* DO: animate opa on each child individually (no children → no layer) */
static void set_children_opa(void *obj, int32_t v) {
    uint32_t cnt = lv_obj_get_child_count((lv_obj_t *)obj);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_set_style_opa(lv_obj_get_child((lv_obj_t *)obj, i), v, LV_PART_MAIN);
    }
}
/* Container stays fully opaque, children get per-element opa */
lv_anim_set_exec_cb(&a, set_children_opa);
```

### GPU-Accelerated vs. Software Fallback

The PSOC Edge E84 GCNANO (ChipID 0x265) has a preference score of 80/255 in LVGL's draw dispatch.

| Render Type | GPU? | Notes |
|---|---|---|
| FILL (solid, gradient) | ✅ Yes | Linear gradients only (no radial) |
| BORDER | ✅ Yes | |
| IMAGE (RGB565, ARGB8888) | ✅ Yes | Format-dependent |
| ARC, LINE, TRIANGLE | ✅ Yes | `line_rounded` may fall back to SW |
| BOX_SHADOW | ✅ if enabled | Set `LV_VG_LITE_USE_BOX_SHADOW 1` in lv_conf.h |
| **LABEL (text)** | ❌ Always SW | No font→GPU path exists in LVGL |
| Radial gradient | ❌ No | Hardware doesn't support it |
| Gaussian blur | ❌ No | Hardware doesn't support it |

### Animation Performance Rules

1. **Never animate opacity on containers with children** — use per-child opa or pre-rendered layers
2. **Text (labels) are always SW-rendered** — avoid high-frequency opacity animations on text
3. **`translate_x`/`translate_y` animations** invalidate both old and new positions — minimize moving area
4. **`LV_VG_LITE_FLUSH_MAX_COUNT = 0`** enables CPU/GPU parallelism — critical for text-heavy UIs
5. **Keep concurrent animations low** — 4-5 simultaneous animations is the practical limit
6. **`shadow_width > 0` on ANY animated object is expensive** — even without text children, shadow rendering at 30fps causes frame drops. Use `bg_color` + `bg_opa` on a larger circle/rect instead. Shadows are only acceptable on static (non-animated) objects
7. **Prefer LVGL drawing primitives over bitmap assets for animated elements** — arcs, lines, circles are GPU-native, resolution-independent, and zero asset management. Reserve bitmaps for static content (hero images, photos)
8. **Guard periodic refresh callbacks with state caching** — `lv_obj_set_style_*()` ALWAYS marks the object dirty (triggers full-frame re-render), even if the value hasn't changed. Cache previous state and skip unchanged updates

### Recommended `lv_conf.h` Tuning (Performance)

```c
/* GPU/CPU parallelism — 0 means flush immediately, letting CPU do SW work while GPU renders */
#define LV_VG_LITE_FLUSH_MAX_COUNT  0

/* GPU-accelerated box shadows (layered borders, not true shadows) */
#define LV_VG_LITE_USE_BOX_SHADOW   1

/* Right-size GPU caches to save memory — defaults are 32 each */
#define LV_VG_LITE_GRAD_CACHE_CNT   4   /* Increase if using many gradients */
#define LV_VG_LITE_STROKE_CACHE_CNT 8   /* Increase if using many stroked paths */
```

---

## 11. Top Pitfalls

1. **Missing GFXSS personality** → dozens of undefined symbols. See §4.
2. **Framebuffers not in `.cy_gpu_buf`** → black screen or GPU hard fault
3. **Missing CY_IGNORE for LVGL originals** → duplicate symbol errors
4. **4.3" uses 832px width** (not 800) for GPU stride alignment
5. **`vg_lite_finish()` missing before FB swap** → screen tearing
6. **FreeRTOS heap < 50 KB** → silent crash during `lv_init()`
7. **`lv_timer_handler()` returns 0** → clamp to 1ms or lower-priority tasks starve
8. **FT5406 touch coordinates inverted** → invert x/y in read callback
9. **`COMPONENTS+=GFXSS` missing from `common.mk`** → `cy_graphics.h` not found
10. **D-cache stale lines after `cybsp_init()`** → IPC/shared-memory corruption
11. **Animating opacity on containers with children** → layer compositing every frame → flicker (§10)

## Verification Checklist

- [ ] `make getlibs` completes (all `.mtb` deps resolved)
- [ ] `make build` succeeds for all three cores
- [ ] Device Configurator shows GFXSS personality enabled
- [ ] Memory Configuration: `gfx_mem` ≥ 2× framebuffer + VG-Lite heap (§9b)
- [ ] Memory Configuration: SOCMEM regions total `0x500000`
- [ ] 4 VG-Lite override files present in `proj_cm55/`
- [ ] `lv_conf.h` has `LV_COLOR_DEPTH 16`, `LV_USE_OS LV_OS_FREERTOS`, `LV_USE_DRAW_VG_LITE 1`
- [ ] Display shows UI content (not black, not garbled)
- [ ] Touch input activates correct UI elements

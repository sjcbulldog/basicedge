[Click here](../README.md) to view the README.

## Design and implementation

This project supports four displays :

   **[Waveshare 4.3-inch Raspberry Pi DSI LCD display](https://www.waveshare.com/4.3inch-DSI-LCD.htm):** The LCD houses a Chipone ICN6211 display controller and uses the MIPI DSI interface. This display is supported by default in the code example for all BSPs except KIT_PSE84_HMI

   **[Waveshare 7-inch Raspberry Pi DSI LCD C display](https://www.waveshare.com/7inch-dsi-lcd-c.htm)**: The LCD houses a Chipone ICN6211 display controller and uses the MIPI DSI interface

   **[10.1 inch WF101JTYAHMNB0](https://www.winstar.com.tw/products/tft-lcd/ips-tft/ips-touch.html):** The TFT LCD houses a [EK79007AD3](https://www.crystalfontz.com/controllers/Fitipower/EK79007AD3/505/) display controller and uses the MIPI DSI interface

   **[ST7701S 4-inch MIPI DSI display driver](https://www.rocktech.com.hk/lcd-product/rk040hf001):** The TFT LCD houses a [ST7701S](https://datasheet4u.com/pdf-down/S/T/7/ST7701-Sitronix.pdf) display controller and uses the MIPI DSI interface. This display by default equipped with the KIT_PSE84_HMI

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM33 core is subsequently put to Deep Sleep mode.

The CM55 application drives the LCD and renders the image using the PSOC&trade; Edge graphics subsystem, which houses an independent 2.5D GPU, a display controller (DC), and a MIPI DSI host controller with a MIPI D-PHY physical layer interface.

**cm55_gfx_task** initializes the Graphics subsystem and configures the DC and GPU interrupts. After that it initializes the LCD panel based on the `CONFIG_DISPLAY` selection in _<application\>/common.mk_. Once the panel is initialized, the required amount of memory is allocated for VGLite draw/blit functions to be consumed by LVGL library. The `lv_init()` function is used to initialize LVGL and set up the essential components required for LVGL to work correctly. The display and touch drivers are initialized using `lv_port_disp_init()` and `lv_port_indev_init()` functions, respectively. The LVGL demo music player is displayed on the display by calling the LVGL demo widget API `lv_demo_music()`. In order to switch to other available LVGL demos, user need to enable the `LV_USE_DEMO_<demo_name>` macro in *lv_conf.h* file and call the corresponding `lv_demo_<demo_name>()` API in place of `lv_demo_music()`.

This application allows users to evaluate the performance of PSOC&trade; Edge's graphics subsystem using the built-in system monitor component of LVGL. Observe the performance data (FPS, CPU usage) in bottom-left corner of the display.

This application allows users to run the LVGL benchmark demo. Set `DEMO_BENCHMARK` to `1` in the *common.mk* file to enable the benchmark demo.
> **Note:**
> - When the `DEMO_BENCHMARK` is set to 1, the build automatically switches to Release mode for accurate measurements.
> - The benchmark demo configuration in this code example is optimized for the MIPI DSI video mode displays (rectangle displays). Refer to the [PSOC&trade; Edge MCU: Smartwatch demo using LVGL](https://github.com/Infineon/mtb-example-psoc-edge-gfx-lvgl-smartwatch) example for optimal configuration for the MIPI DSI command mode displays (round displays).

**Table 2. LVGL benchmark summary**

| Name | Avg. CPU | Avg. FPS | Avg. time | Render time | Flush time |
|------|----------|----------|-----------|-------------|------------|
| Empty screen | 16% | 51 | 7 | 2 | 5 |
| Moving wallpaper | 20% | 54 | 14 | 9 | 5 |
| Single rectangle | 11% | 57 | 9 | 3 | 6 |
| Multiple rectangles | 13% | 56 | 11 | 4 | 7 |
| Multiple RGB images | 17% | 56 | 11 | 5 | 6 |
| Multiple ARGB images | 18% | 57 | 11 | 5 | 6 |
| Rotated ARGB images | 14% | 57 | 10 | 5 | 5 |
| Multiple labels | 41% | 53 | 12 | 7 | 5 |
| Screen sized text | 59% | 31 | 29 | 19 | 10 |
| Multiple arcs | 19% | 57 | 9 | 4 | 5 |
| Containers | 28% | 55 | 13 | 9 | 4 |
| Containers with overlay | 39% | 54 | 13 | 9 | 4 |
| Containers with opa | 29% | 55 | 14 | 9 | 5 |
| Containers with opa_layer | 51% | 34 | 29 | 19 | 10 |
| Containers with scrolling | 35% | 55 | 12 | 8 | 4 |
| Widgets demo | 74% | 23 | 36 | 30 | 6 |
| **All scenes avg.** | **30%** | **50** | **14** | **9** | **5** |

> **Note:** These performance summary is for the default GCC_ARM toolchain.

### Display rendering modes

The rendering behavior is selected by the `USE_PARTIAL_RENDER_MODE` and
`USE_SINGLE_BUFFER_MODE` flags in *lv_port_disp.h*. The two flags are
independent: `USE_PARTIAL_RENDER_MODE` chooses *what* LVGL redraws (the whole
screen vs. only the dirty areas) and `USE_SINGLE_BUFFER_MODE` chooses *how many*
frame buffers are allocated. This gives four combinations:

**Full-buffer rendering** (`USE_PARTIAL_RENDER_MODE = 0`, `USE_SINGLE_BUFFER_MODE = 0`, default):
Double-buffer `LV_DISPLAY_RENDER_MODE_FULL` mode - two full-screen frame buffers. LVGL renders the next frame into the back buffer while the display controller scans out the front buffer; `disp_flush()` programs the new frame-buffer address with `Cy_GFXSS_Set_FrameBuffer()` and waits for the vsync interrupt before returning. The two buffers alternate every frame, giving tear-free output.

**Single-buffer full rendering** (`USE_PARTIAL_RENDER_MODE = 0`, `USE_SINGLE_BUFFER_MODE = 1`):
A single-buffer variant of full-buffer rendering. Only one full-screen frame buffer is allocated, reducing frame-buffer RAM by ~50%. LVGL re-renders the *entire* screen into the same buffer that the display controller scans out and waits for vsync before reusing it. Because the video-mode panel re-scans the buffer continuously while LVGL rewrites the whole frame, tearing can be visible across the entire panel on any animation - this is the trade-off for the RAM saving.

**Partial (direct) rendering** (`USE_PARTIAL_RENDER_MODE = 1`):
`LV_DISPLAY_RENDER_MODE_DIRECT` mode with VGLite GPU acceleration. LVGL redraws only the dirty (invalidated) areas of the frame buffer each refresh instead of the whole screen, reducing per-frame render and blit work. The number of buffers follows `USE_SINGLE_BUFFER_MODE`:

- **Double-buffer direct** (`USE_SINGLE_BUFFER_MODE = 0`): two full-screen buffers are kept so the previous frame content is preserved across refreshes (LVGL synchronizes the dirty areas to the other buffer). Saves render bandwidth/CPU, but not RAM.
- **Single-buffer direct** (`USE_SINGLE_BUFFER_MODE = 1`, recommended for single-buffer): only one full-screen buffer is allocated (~50% RAM saving). LVGL redraws just the invalidated regions straight into the live buffer that the display controller is scanning. Because the static parts of the UI are never rewritten, any tearing is confined to the small animated regions (for example, the music-player spectrum and album art) rather than the whole screen. This is the preferred single-buffer configuration for mostly-static UIs such as the music-player demo - it keeps the ~50% RAM saving while greatly reducing visible tearing compared to single-buffer full rendering.

**Table 3. Frame-buffer RAM usage by mode (default 4.3-inch display)**

Mode | Flags | Buffers | Frame-buffer RAM | Notes
-----|-------|---------|-----------------|------
Double-buffer full (default) | `USE_PARTIAL_RENDER_MODE = 0`, `USE_SINGLE_BUFFER_MODE = 0` | 2 × 832×480×2 B | **~1560 KB** | No tearing; back/front buffers swapped on vsync
Single-buffer full | `USE_PARTIAL_RENDER_MODE = 0`, `USE_SINGLE_BUFFER_MODE = 1` | 1 × 832×480×2 B | **~780 KB (~50% saving)** | Whole screen rewritten each frame; tearing can be visible across the panel on animation
Double-buffer direct | `USE_PARTIAL_RENDER_MODE = 1`, `USE_SINGLE_BUFFER_MODE = 0` | 2 × 832×480×2 B | **~1560 KB** | Only dirty areas redrawn; saves render time, not RAM
Single-buffer direct | `USE_PARTIAL_RENDER_MODE = 1`, `USE_SINGLE_BUFFER_MODE = 1` | 1 × 832×480×2 B | **~780 KB (~50% saving)** | Only dirty areas redrawn into the live buffer; tearing confined to the small animated regions

> **Note:**
> - For fully tear-free output on these MIPI DSI video-mode panels, use a double-buffer mode. Single-buffer modes trade some tearing for ~50% frame-buffer RAM saving; single-buffer direct minimizes that tearing by only rewriting the changed regions.
> - The RAM figures scale with the selected display resolution. For the 10.1-inch / 7-inch 1024×600 panels, each full-screen buffer is 1024×600×2 = 1,228,800 bytes (~1200 KB).

### Arm&reg; Helium (M-Profile Vector Extension) acceleration for Graphics

This code example by default enables Arm&reg; Helium (M-Profile Vector Extension) acceleration for LVGL's software blend operations. The following macros in *lv_conf.h* control this:

```c
#define LV_USE_NATIVE_HELIUM_ASM  1
#define LV_USE_DRAW_SW_ASM        LV_DRAW_SW_ASM_HELIUM
```

When enabled, LVGL uses optimized Helium vector intrinsics for pixel blending, alpha compositing, and color-format conversions. This significantly improves software rendering throughput on the CM55 core. To revert to scalar C code, set `LV_USE_NATIVE_HELIUM_ASM` to `0` and `LV_USE_DRAW_SW_ASM` to `LV_DRAW_SW_ASM_NONE`.

> **Note:** Enabling Helium ASM requires that the CM55 core's MVE unit is active.

> **Note (IAR toolchain):** The IAR assembler cannot process the GCC-style `.S` assembly files used by the Helium blend routines. When building with the IAR toolchain (`TOOLCHAIN=IAR`), `LV_USE_NATIVE_HELIUM_ASM` and `LV_USE_DRAW_SW_ASM` are automatically set to `0` and `LV_DRAW_SW_ASM_NONE` respectively. Software rendering falls back to scalar C code with no code changes required. FPS and CPU usage are therefore toolchain-dependent; IAR builds are not expected to match Helium-accelerated GCC_ARM.

### Memory placement strategy

To maximize rendering performance, the LVGL library code is split across two fast memory regions using the linker scripts:

- **ITCM (zero-wait-state):** The hot-path rendering modules - `src/core`, `src/draw` (including Helium blend), `src/display`, `src/misc`, `src/tick`, `src/indev`, and `src/layouts`, plus `src/osal` - are copied from QSPI flash into ITCM at startup. This gives the CM55 core instruction-fetch latency of 0 cycles for the innermost rendering loops. (For the LLVM_ARM toolchain only, `src/misc`, `src/indev`, and `src/layouts` are placed in SOCMEM instead - see the note below.)

- **SOCMEM:** The LVGL demo code (`demos/music`, `demos/widgets`) and remaining library modules - `src/fonts`, `src/widgets`, `src/themes`, and `src/stdlib` - are placed in SOCMEM. These are accessed less frequently and tolerate the slightly higher latency of this region. The catch-all pattern (`*/lvgl/src/*.*`) captures any remaining `src/` code not already matched by an ITCM pattern; it relies on the ITCM patterns appearing first (first-match order for GCC_ARM / LLVM_ARM, pattern specificity for IAR), so do not reorder it ahead of the specific ITCM entries.

- **QSPI flash:** All other application code (CM55 startup, glue drivers, VGLite wrappers) resides in the external QSPI flash and executes in XIP mode.

This placement is defined in the BSP linker scripts under `bsps/TARGET_<kit>/COMPONENT_CM55/TOOLCHAIN_<toolchain>/pse84_ns_cm55.ld` (GCC_ARM / LLVM_ARM) and the equivalent `.sct` / `.icf` files for ARM and IAR toolchains.

> **Note:** The LLVM_ARM toolchain generates slightly larger code in Debug configuration than other toolchains. To fit within the ITCM limit, `src/misc`, `src/indev`, and `src/layouts` are placed in SOCMEM for LLVM_ARM while remaining in ITCM for other toolchains.

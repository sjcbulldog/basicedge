# BasicEdge Desktop

An Electron app, written in TypeScript, that emulates the BasicEdge PSOC Edge firmware (`../fw`) on the desktop. It provides:

- the classic BASIC described in [`../docs/basiclang.md`](../docs/basiclang.md);
- the same console commands as the device;
- an emulated SD card and an always-connected WIFI radio;
- the `GFX_*` LVGL graphics primitives.

## Running

```powershell
npm install
npm start          # builds into dist/ and launches Electron
npm test           # interpreter, console, SD card, WIFI and graphics tests
npm run typecheck
```

When the app starts, the console shows the familiar `Starting MY-BASIC 1.2.2 on CM55` banner and the `BASIC>` prompt. Type `HELP` to list the commands.

## How it works

TypeScript ports of the firmware components live in `src/core`:

| Module | Firmware source |
| --- | --- |
| `translator.ts` | `basic_compat.c`: converts classic numbered BASIC into MY-BASIC text plus a line map |
| `vm.ts` | The MY-BASIC subset that the translator emits, with the same error codes and messages |
| `console.ts` | `basic_console.c` / `basic_task.c`: line storage, LIST, RENUM, TRACE, file, SDCARD, WIFI and GFX commands |
| `graphics.ts` | `basic_graphics.c`: an LVGL-like object tree (64 handles) with layout, styles and touch input |
| `catalog.ts` | `basic_catalog.c`: the program catalog used by `WIFI LIST` and `WIFI BLOAD` |

`src/main` contains the Electron main process, which owns:

- the windows;
- the SD card (`sdcard.ts`, a port of `file_service.c` path handling);
- the WIFI service (`wifi.ts`).

`src/renderer` contains the xterm console and the graphics painter.

## Graphics window

The first time a program creates or changes a graphics object, a separate window opens at the display size of 800 × 480. To change the scale, use **Graphics → Scale 1x/2x/3x/4x** or Ctrl+1 to Ctrl+4. The default is 2x and the choice is saved.

To reopen a closed window, use **Graphics → Show Graphics Window** (Ctrl+G).

Mouse clicks and drags act as the touch panel, so buttons, switches, checkboxes, sliders, arcs and dropdowns respond. Keys typed while the graphics window has focus go to the BASIC console (`INKEY$`, Ctrl-C).

## Emulated SD card

The card is a folder named `sdcard` in the Electron user-data directory (on Windows, `%APPDATA%\basicedge-desktop\sdcard`). To open it, use **File → Open SD Card Folder**.

On first run the folder is seeded with `amazing.bas` and `grtest.bas`. The card reports a 1024 MiB SDHC card with FAT32, and `SDCARD FORMAT YES` empties the folder. Paths follow the firmware rules:

- `/` is the root and backslashes are rejected;
- `.` and `..` are supported;
- programs must be smaller than 32 KiB.

## Emulated WIFI

The desktop is treated as always connected. `WIFI LOAD`, `WIFI LIST` and `WIFI BLOAD` download over the host network. Downloads follow redirects, require HTTP 200 and are limited to 32767 bytes.

`WIFI SCAN` behaves like the device while it is connected: the scan is unavailable. `WIFI DISCONNECT` simulates a lost connection until the next `WIFI CONNECT "SSID" "PASSWORD"`, which always succeeds and stores the SSID in `wifi.json`.

## Differences from the device

- `LOAD` accepts CRLF line endings and blank lines, which are removed.
- `&H` hexadecimal literals are accepted, as used by `grtest.bas`.
- `GFX_TEXT` on a `BUTTON` creates a centred label, and on a `CHECKBOX` it sets the checkbox text.
- While a program runs, keys other than Ctrl-C become the pending `INKEY$` key, and any key wakes `SLEEP`.
- Rendering approximates the LVGL default theme. As on the device, label backgrounds are transparent, so `GFX_BG_COLOR` on a label has no visible effect.
- `READ`/`DATA` are unsupported, as on the firmware.

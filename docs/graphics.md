# BASIC Graphics

The CM55 BASIC interpreter exposes the LVGL display through the `GFX_`
functions and the resident `GFX` commands. LVGL remains owned by the graphics
task; BASIC requests are sent through a bounded queue and are applied between
LVGL timer-handler calls.

## Display size

`GFX_WIDTH()` and `GFX_HEIGHT()` return the active LVGL logical resolution.
They return `0` until the graphics task has initialized the display.

```basic
10 W = GFX_WIDTH()
20 H = GFX_HEIGHT()
30 PRINT "DISPLAY "; W; " X "; H
```

## Objects

`GFX_SCREEN()` returns the active screen handle. Create supported objects with
`GFX_CREATE(type [, parent])`; handles are positive integers and are opaque.
The current implementation supports `OBJ`, `LABEL`, `BUTTON`, `BAR`,
`SLIDER`, `CHECKBOX`, `SWITCH`, `DROPDOWN`, `TEXTAREA`, `KEYBOARD`, `IMAGE`,
`ARC`, and `LED`.

```basic
10 S = GFX_SCREEN()
20 L = GFX_CREATE("LABEL", S)
30 GFX_SET_POS(L, 20, 20)
40 GFX_TEXT(L, "Hello from BASIC")
50 GFX_COLOR_SET(L, GFX_COLOR(255, 255, 255))
```

Available lifecycle and geometry functions are `GFX_DELETE`, `GFX_PARENT`,
`GFX_SET_POS`, `GFX_SET_SIZE`, `GFX_X`, `GFX_Y`, `GFX_WIDTH_OF`,
`GFX_HEIGHT_OF`, `GFX_ALIGN`, `GFX_SHOW`, `GFX_HIDE`, `GFX_ENABLED`, and
`GFX_INVALIDATE`.

Text is supported for labels and widgets with a label child. `GFX_VALUE` and
`GFX_RANGE` provide value and range control for bars, sliders, arcs, and
switches. `GFX_SELECTED` and `GFX_OPTIONS` control dropdowns, while
`GFX_CHECKED` controls checkboxes and switches. `GFX_BG_COLOR`,
`GFX_COLOR_SET`, `GFX_OPACITY`, `GFX_RADIUS`, and `GFX_FONT` set common object
styles. `GFX_COLOR(r,g,b)` and `GFX_RGB(hex)` both return packed RGB values.

## Resident commands

| Command | Behavior |
| --- | --- |
| `GFX INIT` | Reports whether LVGL is ready. |
| `GFX INFO` | Reports readiness and logical display size. |
| `GFX LIST` | Lists live handles, widget types, and parent handles. |
| `GFX CLEAR` | Deletes user-created objects below the active screen. |
| `GFX RESET` | Same destructive reset behavior as `GFX CLEAR`. |
| `GFX UPDATE` | Reports that updates are serviced by the graphics task. |
| `GFX HELP` | Prints the command summary. |

## Limits and current limitations

The handle table holds 64 live objects, requests are queued in an eight-entry
bounded queue, and text is limited to 127 characters. Queue operations wait up
to 500 ms and return a BASIC error on timeout or invalid handles.

Event callbacks, pointer coordinates, and keyboard text are not yet exposed;
the current bridge deliberately omits them because callback dispatch must be
scheduled in the BASIC task rather than from an LVGL callback. `GFX_UPDATE`
does not call `lv_timer_handler()` a second time.
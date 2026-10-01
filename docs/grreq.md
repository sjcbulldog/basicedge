# BASIC Graphics Subsystem Requirements

## 1. Purpose

Add a graphics subsystem to the BASIC interpreter that exposes a stable,
resource-bounded BASIC API over the LVGL v9 graphics library already running
on CM55. A BASIC program must be able to create and update a small interactive
user interface without including C code or knowing which supported display is
attached.

The subsystem is an adapter, not a second graphics engine. Rendering, input
dispatch, invalidation, layout, styles, animation, and widget behavior remain
LVGL responsibilities. The adapter translates BASIC values into LVGL calls and
returns opaque integer handles where LVGL uses pointers.

## 2. Scope and compatibility

- Target LVGL v9.x APIs used by this project.
- Support the display and input device initialized by the existing CM55
  graphics task.
- Keep BASIC execution in its existing FreeRTOS task. Graphics work must be
  serialized with LVGL's timer/rendering task; BASIC must never call LVGL
  directly from an unprotected context.
- Use integer coordinates and dimensions in display pixels. The origin is the
  top-left corner and coordinates are zero-based.
- Use RGB color values expressed as six hexadecimal digits (`RRGGBB`) and
  opacity values from 0 through 255.
- Return `0` for an invalid or unavailable object handle and a negative value
  for an API error. BASIC runtime errors must identify the graphics operation.
- Enforce fixed limits for live object handles, queued operations, text
  lengths, and event callbacks. Limits must be documented and configurable at
  build time; no graphics API may perform an unbounded allocation.

The first implementation targets the default display and one LVGL display
instance. The public BASIC names must not expose display-driver or GPU-specific
details.

## 3. BASIC naming and values

Graphics functions use the `GFX_` prefix. Graphics commands use the `GFX`
prefix followed by a space and an operation name. Function names are
case-insensitive, matching the existing BASIC interpreter behavior.

An object handle is a positive BASIC integer returned by `GFX_CREATE`. Handles
are opaque and must not be treated as LVGL pointers. A handle remains valid
until `GFX_DELETE` or an automatic deletion by LVGL; subsequent use reports an
invalid-handle error. The adapter must reject stale handles rather than
dereferencing them.

## 4. Required functions

### 4.1 Display and frame control

| Function | Result and arguments | Requirement |
| --- | --- | --- |
| `GFX_WIDTH()` | Integer | Return the active LVGL display's horizontal resolution. |
| `GFX_HEIGHT()` | Integer | Return the active LVGL display's vertical resolution. |
| `GFX_COLOR(r, g, b)` | Integer | Return an RGB color value. Each component is 0 through 255. |
| `GFX_RGB(hex)` | Integer | Return a color from a six-digit integer such as `&H336699`. |
| `GFX_TICK()` | Integer | Return the LVGL tick value in milliseconds for animation timing. |
| `GFX_INVALIDATE(handle)` | Integer status | Invalidate an object so LVGL redraws it. `0` means success. |
| `GFX_REFRESH()` | Integer status | Queue a graphics refresh and return without blocking on a frame. |

`GFX_WIDTH()` and `GFX_HEIGHT()` must return the LVGL logical resolution, not a
hard-coded panel size or the physical connector resolution. They must be safe
to call after graphics initialization and return `0` before initialization.

### 4.2 Object lifecycle and geometry

| Function | Result and arguments | Requirement |
| --- | --- | --- |
| `GFX_SCREEN()` | Object handle | Return the active screen handle. |
| `GFX_CREATE(type [, parent])` | Object handle | Create a supported widget type under `parent`, or the active screen when omitted. |
| `GFX_DELETE(handle)` | Integer status | Delete an object and all of its children. |
| `GFX_PARENT(handle)` | Object handle | Return the object's parent, or `0` at the screen root. |
| `GFX_SET_POS(handle, x, y)` | Integer status | Set the object's position relative to its parent. |
| `GFX_SET_SIZE(handle, width, height)` | Integer status | Set the object's size in pixels. |
| `GFX_X(handle)` | Integer | Return the object's x coordinate. |
| `GFX_Y(handle)` | Integer | Return the object's y coordinate. |
| `GFX_WIDTH_OF(handle)` | Integer | Return the object's current width. |
| `GFX_HEIGHT_OF(handle)` | Integer | Return the object's current height. |
| `GFX_ALIGN(handle, mode [, offset_x, offset_y])` | Integer status | Align an object using a named alignment mode and optional offsets. |
| `GFX_SHOW(handle)` | Integer status | Clear the hidden state. |
| `GFX_HIDE(handle)` | Integer status | Set the hidden state. |
| `GFX_ENABLED(handle, enabled)` | Integer status | Enable or disable user interaction. |

Supported `type` values are `SCREEN`, `OBJ`, `LABEL`, `BUTTON`, `BAR`,
`SLIDER`, `CHECKBOX`, `SWITCH`, `DROPDOWN`, `TEXTAREA`, `KEYBOARD`, `IMAGE`,
`ARC`, and `LED`. Unsupported types must fail with a clear error rather than
creating a generic object silently.

Supported alignment modes are `TOP_LEFT`, `TOP_MID`, `TOP_RIGHT`, `LEFT_MID`,
`CENTER`, `RIGHT_MID`, `BOTTOM_LEFT`, `BOTTOM_MID`, and `BOTTOM_RIGHT`.

### 4.3 Text and widget values

| Function | Result and arguments | Requirement |
| --- | --- | --- |
| `GFX_TEXT(handle [, value])` | String or status | Get a widget's text when `value` is omitted; set it otherwise. |
| `GFX_VALUE(handle [, value])` | Integer or status | Get or set the numeric value of bars, sliders, arcs, and switches. |
| `GFX_RANGE(handle, min, max)` | Integer status | Set the range for a bar, slider, or arc. |
| `GFX_SELECTED(handle [, index])` | Integer or status | Get or set the selected item for a dropdown or checkbox group. |
| `GFX_OPTIONS(handle, text)` | Integer status | Replace dropdown options with newline-separated text. |
| `GFX_CHECKED(handle [, state])` | Integer or status | Get or set the checked state of a checkbox or switch. |
| `GFX_COLOR_SET(handle, color)` | Integer status | Set the primary widget color. |
| `GFX_BG_COLOR(handle, color)` | Integer status | Set the background color. |
| `GFX_OPACITY(handle, opacity)` | Integer status | Set object opacity from 0 through 255. |
| `GFX_RADIUS(handle, radius)` | Integer status | Set the corner radius in pixels. |
| `GFX_FONT(handle, name)` | Integer status | Select a compiled-in font by name. Unknown fonts fail. |

String arguments are copied into adapter-owned bounded storage or into LVGL
storage according to the widget contract. A BASIC temporary string must not be
retained as a pointer after the call returns.

### 4.4 Input and events

| Function | Result and arguments | Requirement |
| --- | --- | --- |
| `GFX_ON(handle, event, line)` | Integer status | Register a BASIC line number as the handler for an event. |
| `GFX_OFF(handle, event)` | Integer status | Remove the handler for an event. |
| `GFX_EVENT()` | Integer | Return the event code while an event handler is running. |
| `GFX_EVENT_OBJECT()` | Object handle | Return the object that generated the current event. |
| `GFX_POINTER_X()` | Integer | Return the latest pointer x coordinate, or `-1` if unavailable. |
| `GFX_POINTER_Y()` | Integer | Return the latest pointer y coordinate, or `-1` if unavailable. |
| `GFX_KEY()` | String | Return the latest submitted keyboard text, or an empty string. |

Event names initially include `CLICKED`, `PRESSED`, `RELEASED`, `VALUE_CHANGED`,
`FOCUSED`, `DEFOCUSED`, `READY`, and `DELETE`. Event callbacks must be queued
to the BASIC task and must not execute BASIC from an LVGL callback or interrupt
context. A callback line is a BASIC line number in the current program; it is
not an arbitrary source string.

## 5. Required commands

Commands are handled by the resident BASIC console layer and are available
outside a running program.

| Command | Behavior |
| --- | --- |
| `GFX INIT` | Verify that LVGL and the default display are initialized. Safe to repeat. |
| `GFX CLEAR` | Delete all objects below the active screen and clear the screen to the configured background color. |
| `GFX UPDATE` | Process queued graphics operations and request a redraw; it must not run a second LVGL timer handler. |
| `GFX INFO` | Print initialization state, logical width and height, live handle count, and queued operation count. |
| `GFX LIST` | List live handles, widget types, parent handles, and object text, subject to bounded output. |
| `GFX RESET` | Remove BASIC event bindings and delete all user-created objects. The active screen remains available. |
| `GFX HELP` | Print the graphics command and function summary. |

Invalid command arguments must print usage and leave the current graphics
state unchanged. `GFX CLEAR` and `GFX RESET` are explicit destructive
operations; they must not be implicit side effects of `GFX INIT`.

## 6. Runtime and concurrency requirements

1. Graphics initialization must complete before `GFX_WIDTH()` or
   `GFX_HEIGHT()` report nonzero values.
2. The graphics task remains the sole owner of LVGL calls and continues to run
   `lv_timer_handler()` at its existing cadence.
3. BASIC-to-graphics calls are transferred through a bounded command queue.
   Calls that mutate objects return only after the graphics task has applied
   the operation or report a timeout error.
4. Event notifications use a separate bounded queue. Queue overflow is
   counted and reported by `GFX INFO`; it must not corrupt BASIC memory.
5. A long-running BASIC program must not prevent LVGL timers, display flushes,
   or touch input from being serviced.
6. Graphics APIs are unavailable while the interpreter is shutting down and
   must return an error rather than touching deinitialized LVGL state.
7. All handle, string, event, and queue limits are checked before copying or
   allocating data.

## 7. Diagnostics and errors

The adapter must use stable error codes and include the operation name in
diagnostics. At minimum it must distinguish:

- graphics not initialized;
- invalid or stale handle;
- unsupported widget, event, alignment, or font name;
- wrong argument type or count;
- value outside the permitted range;
- queue full or operation timeout;
- object limit or string limit reached.

Errors from a BASIC graphics function must be returned through the MY-BASIC
native-function convention. Resident command errors must not terminate the
interpreter or alter the stored BASIC program.

## 8. Acceptance criteria

- A BASIC program can create a label and button, position them using
  `GFX_WIDTH()` and `GFX_HEIGHT()`, set their text and colors, and display them
  on each supported panel.
- A slider or switch can be changed through touch and its registered BASIC
  event line runs without calling BASIC from the LVGL task or interrupt.
- `GFX CLEAR`, `GFX RESET`, and interpreter restart leave no stale handles or
  callbacks.
- Invalid handles, wrong types, oversized strings, queue exhaustion, and
  pre-initialization calls produce bounded diagnostics and do not crash the
  board.
- A graphics-enabled BASIC program and the existing Trek program can run while
  the LVGL timer handler continues to service the display.
- `docs/graphics.md` documents the implemented subset, examples, limits,
  display-size behavior, and known limitations after implementation.

## 9. Delivery order

1. Add the native graphics adapter and handle table.
2. Add display-size functions and object lifecycle/geometry functions.
3. Add text, colors, basic widgets, and resident commands.
4. Add queued events and BASIC callback dispatch.
5. Add focused host tests for parsing, handles, bounds, and command errors,
   followed by hardware verification on each supported display.
6. Write `docs/graphics.md` from the implemented behavior and record any
   intentional deviations from this requirements document.
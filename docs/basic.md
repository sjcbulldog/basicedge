# BASIC Interpreter Plan

Last updated: 2026-10-10

## Goal

Run `basic/trek.bas` on the CM55 core. BASIC `INPUT` reads from standard input
and BASIC `PRINT` writes to standard output; CM55 retarget-io maps both streams
to the KitProg UART at 115200 baud.

## Interpreter

The interpreter is [MY-BASIC](https://github.com/paladin-t/my_basic), pinned to
commit `ff5bed781187309396b6ec7bf0d5fe9222085d19` (version 1.2.2). It is an
embeddable interpreter written in standard C and distributed under the MIT
License. The vendored copyright and license are retained in
`proj_cm55/third_party/my_basic/LICENSE` and in the upstream source headers.

TinyBasic Plus was rejected because its inherited licensing history includes a
personal-use-only restriction and its language lacks the string and array
support required by `trek.bas`.

## Constraints

- Keep the CM55 graphics task responsive while BASIC runs in its own FreeRTOS
  task.
- Use the existing CM55 retarget-io instance. No second core may initialize or
  own the same UART.
- Do not rewrite `basic/trek.bas` by hand. Compatibility transformations must be
  deterministic and tested.
- Preserve the upstream MIT notice in redistributed source or binaries where
  required.
- Treat interpreter input as bounded data; no unchecked copies or unbounded
  line buffers.

## Console Commands

Phase 1 provides the resident commands expected from an early BASIC machine:

| Command | Behavior |
| --- | --- |
| `LIST` | Lists the stored program in line-number order. |
| `LIST n` / `LIST n-m` | Lists one line or an inclusive line-number range. |
| `DIR` | Lists the current SD-card directory. |
| `PWD` | Prints the current SD-card directory. |
| `WIFI SCAN` | Lists nearby networks while disconnected. |
| `WIFI STATUS` | Reports whether WiFi is connected and, if connected, the SSID. |
| `WIFI STORED` | Lists SSIDs with stored WiFi credentials. |
| `WIFI CLEAR` | Clears all stored WiFi credentials. |
| `WIFI CONNECT "SSID" "PASSWORD"` | Connects to a station and saves successful credentials in Em_EEPROM. |
| `WIFI DISCONNECT` | Disconnects the station interface. |
| `WIFI LOAD "URL"` | Downloads an HTTP/HTTPS BASIC program and loads it into the program store. |
| `WIFI LIST` | Downloads the online program catalog and lists its programs. |
| `WIFI BLOAD "program"` | Downloads and loads a program named in the online catalog. |
| `SDCARD STATUS` | Reports whether an SD card is inserted and provides its type, capacity, size, and filesystem. |
| `SDCARD FORMAT [YES]` | Formats the SD card; a valid filesystem requires `SDCARD FORMAT YES` confirmation. |
| `SDCARD FREE` | Reports the free space available on the SD card. |
| `CD ["path"]` | Changes the SD-card directory; no path returns to `/`. |
| `MKDIR "path"` | Creates a directory relative to the current SD-card directory. |
| `DEL "filename"` | Deletes a file from the current SD-card directory. |
| `RUN` | Clears variables, parses classic numbered BASIC, and runs the program. |
| `LOAD "filename"` | Loads a numbered BASIC program from the CM33-owned SD card. |
| `SAVE "filename"` | Saves the current numbered BASIC program to the CM33-owned SD card. |
| `NEW` | Clears the stored program and variables. |
| `CLEAR` | Clears variables without deleting the stored program. |
| `RENUM [start[,step]]` | Renumbers lines and numeric branch targets. |
| `HELP` | Lists the available console commands. |
| `CLS` | Clears the terminal screen and homes the cursor. |

An input beginning with a line number adds or replaces that program line. A
line number entered by itself deletes that line. Other input executes
immediately. File paths use forward slashes and reject backslashes. CM55
accesses the SD card through a bounded chunked IPC file service; CM33 remains
the sole owner of the emFile/SD-card mount.
WiFi runs on CM33_NS and is controlled by CM55 over IPC. Runtime credentials
are not stored in source. Up to five successful SSID/password pairs are retained
in Em_EEPROM; when full, the least-recently successful entry is evicted. On
startup, CM33 scans and connects to the last saved SSID that appears in scan
results. Em_EEPROM data is not encrypted. Scanning is rejected while associated
with an access point, and WCM automatically attempts link recovery after an
unexpected disconnect.
`WIFI LOAD` requires an active WiFi connection, accepts HTTP 200 responses up to
32 KiB, and does not follow redirects. HTTPS server certificates are not
verified, as selected for this project; use only trusted networks and sources.
`WIFI LIST` and `WIFI BLOAD` use the same downloader with the catalog at
`https://www.mewserver.org/basic/progs.json`, whose format matches
[progs.json](../basic/progs.json). The catalog is fetched on each command and
must be a `programs` array of objects with `filename` and non-negative integer
`size`; `type` and `description` are optional strings. It holds at most 32
programs; filenames are limited to 63 characters, types to 31, and
descriptions to 191. Filenames may contain only letters, digits, `.`, `_`, and
`-`, may not start with `.` or contain `..`, and must be unique ignoring case.
`WIFI BLOAD` matches the filename ignoring case, with `.bas` optional, then
downloads `https://www.mewserver.org/basic/<filename>`. It loads the program
only when the downloaded length equals the catalog size. Any catalog error or
HTTP status other than 200 (including 403) reports a download failure without
changing the current program. The downloader prints the HTTP status, for example
`WIFI LOAD: server returned HTTP status 403.` A TLS receive error logged after
a non-200 response is the server closing its connection, not the cause.
CM33 performs the download and TLS handshake in its Message RX task, which
has a 16 KiB stack. A smaller 4 KiB stack overflows during TLS hashing and
leaves CM55 waiting until its download request times out. Socket option
failures report the option, level, and result code through WiFi logging.
The WiFi startup task uses a 10 KiB stack, expressed as FreeRTOS stack words
rather than bytes, so it does not reserve 40 KiB and exhaust the shared C heap
when TLS allocates its record buffers.
CM33 also initializes the BSP-configured RTC and registers its HAL handle
with CLIB support before networking starts. TLS 1.3 calls `time()` even
with certificate verification disabled; without this per-core registration
CLIB asserts during the handshake. The RTC starts at the Device Configurator
date, not a network-synchronized time.
The initial current directory is the user-visible `/`; filenames without a leading slash are resolved relative
to the current directory.


While a program is running, UART characters are discarded between BASIC
statements so type-ahead cannot leak into a later `INPUT`. Ctrl-C (`0x03`) is
the exception: it interrupts the running program and returns to `BASIC>`. Once
an `INPUT` statement is actively waiting, normal characters are accepted and
Ctrl-C still interrupts the program.

## Current Status

Phase 1 is complete and verified on `TARGET_APP_KIT_PSE84_EVAL_EPC2` using the
KitProg UART at 115200 baud. The following behaviors have been exercised on
hardware:

- MY-BASIC 1.2.2 starts on CM55 while the graphics task remains active.
- Immediate mode evaluates statements and preserves variables between commands.
- Numbered lines are inserted in sorted order, replaced by matching line number,
  and deleted by entering the line number alone.
- `LIST`, `RUN`, `NEW`, `CLEAR`, `RENUM`, and `HELP` return to `BASIC>`.
- `RENUM 100,100` renumbers stored lines and a `GOTO` target correctly.
- `RUN` translates numeric labels to MY-BASIC labels and executes a tested
  multi-line program with `GOTO`.
- CR, LF, and CRLF terminal line endings are accepted.
- Ordinary UART type-ahead is discarded during execution.
- Ctrl-C cleanly interrupts both a tight loop and a blocking `INPUT`, prints
  `Break`, and returns to `BASIC>` without retaining stale input.

Current limitations:

- Both SD-card `LOAD "trek.bas"` and built-in `LOAD "TREK"` are available.
  Trek's classic language features and gameplay are now exercised by the host
  regression harness and on the board.
- `LOAD` and `SAVE` use the chunked inter-core storage protocol. Hardware
  validation of SD file persistence is still pending.
- `DIR` and `CD` use the same CM33-owned SD service. The service maintains the
  current directory and resolves relative file paths against it.
- Console and SD diagnostics share the UART and can interleave during startup.

## Classic BASIC Parser

`basic_compat.c` parses the stored source into bounded MY-BASIC runtime text.
The original program, SD file, embedded asset, `LIST`, and `SAVE` remain unchanged.
Immediate statements use the same parser. Runtime errors and `TRACE` identify
the original numbered BASIC line, rather than a generated runtime row.

Supported Trek constructs include:

- Single-expression `DEF FNname(parameter)=expression`, with local parameters
  and shared global variables. Definitions and array declarations are bound
  before forward references are parsed.
- Numeric `GOTO`, `GOSUB`/`RETURN`, numeric `IF ... THEN`/`ELSE`, nested inline
  conditions, and colon-separated statements. An inline IF controls the rest of
  its source line. `ON expression GOTO/GOSUB` uses one-based target selection;
  out-of-range selectors fall through.
- `FOR`/`NEXT`, including negative `STEP`, subroutine calls within loops, and
  branches leaving loops. These use label-based control flow, not MY-BASIC's
  structured-loop call stack.
- Comma-separated `DIM` declarations with inclusive constant upper bounds.
  Index zero remains valid; fractional subscripts truncate toward zero.
  An array and scalar may share a classic name (`N` and `N(3)` in Trek).
- `INT` rounds down; `RND` produces a real in `[0,1)`, with zero repeating the
  last value and negative arguments reseeding. `RANDOMIZE` accepts a seed.
- ASCII `LEFT$`, `RIGHT$`, and one-based `MID$`, including empty/clamped results;
  `STR$`, `CHR$`, and one-based `TAB`.
- Semicolon print concatenation, trailing-separator suppression of the newline,
  and empty `PRINT`. Numeric formatting remains MY-BASIC's `%g` format.
- Prompted string/numeric `INPUT`, semicolon or comma prompt separators, and
  comma-separated variable lists (up to eight). Invalid numeric input or field
  counts (including numbers outside the interpreter's finite numeric range)
  report an interpreter error before assigning any fields.
- `STOP`/`END` terminate normally. `TIMER` returns seconds since boot.
  `SLEEP` takes **seconds**, yields to FreeRTOS, and ends early on a key press.
  `INKEY$` consumes that pending key or returns an empty string. Consequently
  Trek's `SLEEP 500` instructions pause can be ended by pressing a key, without
  redefining its duration as milliseconds. Ctrl-C interrupts sleep and INPUT.

The parser is a Trek-focused classic dialect, not a complete QBasic
implementation. DIM bounds must be integer constants, inline IF nesting is
limited to 16, lexical FOR nesting to 32, and helper identifiers beginning
`BC` are reserved. The source store remains 32 KiB; generated runtime text is
bounded to 96 KiB and 4096 source-map rows. Native MY-BASIC functions remain
available, but the console deliberately uses classic PRINT/DIM/loop semantics.

The vendored interpreter has narrow local extensions for classic function/global
scope, fractional array subscripts, owned INPUT strings, and allocation alignment.
Its compact memory-size tag is padded to preserve malloc alignment. Without the
padding, the first CM55 floating-point array store HardFaults on a two-byte-aligned
address. Upstream MIT notices remain intact.

### Regression Tests

From the workspace root with a host GCC compiler:

```powershell
.\tests\run_basic_compat_tests.ps1 -Compiler C:\cygwin64\bin\gcc.exe
```

The harness covers function scope, inclusive arrays, random values, branches,
negative-step loops, subroutine returns, string edge cases, INPUT, invalid syntax,
buffer bounds, and the unchanged full Trek program. Scripted sessions exercise
instructions both accepted and skipped, scans, damage reports, the coordinate
calculator, shields, navigation, and resignation. The runner checks output for
actual game information, not only successful parsing.

Hardware verification uses `LOAD "trek.bas"` / `RUN` through COM4 at 115200 baud.
The tested session reached `COMMAND?`, performed SRS/LRS/DAM/COM, read coordinate
pairs, raised shields, navigated under enemy fire, resigned, and evaluated
`PRINT 2+2` after returning to the console.
Further checks passed for repeated `RUN`, accepting instructions and pressing a
key to continue, Ctrl-C during instructions sleep, active game INPUT and immediate
sleep, empty/clamped string functions, and `RENUM 100,100` with ON GOTO and inline
IF destinations. These checks verify restart/recovery, not a full-game leak test.
The final flashed build also rejected an overflowing numeric INPUT without
changing either destination variable, preserved distinct immediate scalar/array
values, recovered from a tight loop with Ctrl-C, and ran another SD-card mission.

## Phases

### Phase 1: Interpreter foundation and classic console

Status: complete.

- Vendor the pinned MY-BASIC core and MIT license.
- Add a CM55 interpreter task after retarget-io initialization.
- Route `PRINT` through stdout and `INPUT` through a bounded stdin callback.
- Execute a built-in smoke program, then remain in an interactive direct-mode
  command loop. Variables persist between commands, characters are echoed with
  backspace editing, and CR, LF, or CRLF terminates a line.
- Provide a 32 KiB, sorted, numbered program store. Entering a numbered line
  adds or replaces it; entering only its number deletes it.
- Provide classic console commands: `LIST`, `RUN`, `NEW`, `CLEAR`, `RENUM
  [start[,step]]`, and `HELP`. `RENUM` updates stored line numbers and numeric
  targets following `GOTO`, `GOSUB`, `THEN`, `ELSE`, `RESTORE`, and `RUN`.
- Discard UART type-ahead while a program executes, except Ctrl-C, which
  interrupts execution. Continue to accept normal input during BASIC `INPUT`.
- Acceptance: the complete firmware builds and serial output contains
  `Smoke result: 42` and `MY-BASIC Phase 1 smoke test passed`; entering
  `10 PRINT 6 * 7;`, `LIST`, and `RUN` stores, lists, and executes the program.

Verification: passed on hardware. A renumbered `GOTO` program printed only its
reachable lines, ordinary queued characters were discarded during an infinite
loop, and Ctrl-C recovered both the loop and an active `INPUT`.

### Phase 2: Trek source and compatibility harness

Status: complete. The embedded Trek source is now generated into
`proj_cm55/source/basic_trek_program.c`, and the runtime console accepts
`LOAD "TREK"` to load the built-in program into the classic BASIC store.

- Generate a C source asset from `basic/trek.bas` during the build so the BASIC
  file remains the single source of truth. This is now satisfied by the
  generated CM55 asset in `proj_cm55/source/basic_trek_program.c`.
- Add host-side compatibility tests that load the complete program and report
  parser errors with original BASIC line numbers.
- Extend the numeric-line translator for colon-separated statements, implicit
  variables, and one-based arrays.
- Add `LOAD "TREK"` for the built-in program. The built-in loader and
  filesystem-backed `LOAD`/`SAVE` commands are now implemented in
  `proj_cm55/source/basic_console.c`.
- Acceptance: the complete translated program parses without execution.

### Phase 3: Language compatibility

Status: complete. Host tests run the unchanged program through a scripted mission.

- Cover the constructs used by Trek: numeric and string arrays, `DEF FN`,
  `ON ... GOTO`, `GOSUB`/`RETURN`, `IF ... THEN`, `FOR`/`NEXT`, and semicolon
  print formatting.
- Provide or adapt `RND`, `RANDOMIZE TIMER`, `SLEEP`, `INT`, `SQR`, `TAB`,
  `LEFT$`, `RIGHT$`, `MID$`, and `STR$`.
- Add deterministic random seeding and scripted-input regression tests.
- Acceptance: Trek reaches its first command prompt using scripted input.

### Phase 4: Interactive UART game

Status: gameplay verified on hardware. SD-card loading, prompts, command dispatch,
coordinate input, scans, shields, navigation, combat, and resignation work.
Output serialization with unrelated CM55 logs remains a separate follow-up.

- Run the embedded Trek program from the CM55 BASIC task.
- Verify editing, echo, CR/LF handling, prompts without trailing newlines, and
  numeric/string input through KitProg UART.
- Serialize interpreter output with other CM55 logging to avoid interleaving.
- Acceptance: a user can start a mission, issue commands, and resign through a
  115200-baud serial terminal.

### Phase 5: Robustness and release

Status: not started.

- Measure interpreter heap and task stack high-water marks during a full game.
- Handle syntax/runtime errors, EOF, line overflow, and interpreter restart.
- Verify graphics responsiveness and tickless-idle UART behavior during play.
- Record final memory use, limitations, build steps, and third-party notices.
- Acceptance: repeated games run without leaks, faults, or corrupted UART I/O.
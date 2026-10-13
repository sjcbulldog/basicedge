# BASIC Language Reference

Last updated: 2026-10-11

This document describes the BASIC dialect implemented by the BasicEdge console
on the PSOC Edge E84 board. It is a reference for writing and running programs.
For design history, implementation notes, and test status, see
[basic.md](basic.md). For the graphics API, see [graphics.md](graphics.md).

## Overview

BasicEdge runs a classic, line-numbered BASIC similar to the microcomputer
dialects of the late 1970s. It is designed to run programs such as
`trek.bas` (Star Trek) and `amazing.bas` (maze generator) without changes.

The implementation has two layers:

1. **Classic translator** (`proj_cm55/source/basic_compat.c`). Converts the
   numbered program into runtime text for the interpreter. It handles line
   numbers, `GOTO`/`GOSUB`, inline `IF`, `FOR`/`NEXT`, `DIM`, `DEF FN`,
   `ON ... GOTO`, classic `INPUT`/`PRINT`, and classic string functions.
2. **MY-BASIC 1.2.2 interpreter**
   ([paladin-t/my_basic](https://github.com/paladin-t/my_basic), MIT license).
   Evaluates expressions and runs the translated program.

The stored program is never modified by translation. `LIST` and `SAVE` always
show the original source, and errors and trace output refer to the original
line numbers.

BASIC runs on the CM55 core. The console uses the KitProg UART at 115200 baud.
The SD card and WiFi belong to the CM33 core and are reached over IPC.

## Using the Console

The prompt is `BASIC>`. Each input line is handled in one of three ways:

| Input | Effect |
| --- | --- |
| `10 PRINT "HI"` | Stores line 10, replacing any existing line 10. Lines are kept sorted. |
| `10` | Deletes line 10. |
| A console command (`RUN`, `LIST`, ...) | Runs the command. |
| Anything else | Executes immediately as BASIC (immediate mode). |

Immediate-mode statements use the same translator as programs, and variables
persist between immediate commands until `RUN`, `CLEAR`, or `NEW`.

```text
BASIC> A=6*7
BASIC> PRINT A
42
```

Line editing supports Backspace. CR, LF, and CRLF all end a line. Keywords and
commands are not case-sensitive.

### Interrupting a Program

Press **Ctrl-C** to stop a running program, including one that is waiting in
`INPUT` or `SLEEP`. The console prints `Break` and returns to `BASIC>`.
Other keys typed while a program runs are discarded, so they cannot leak into a
later `INPUT`. The exception is `INKEY$` and `SLEEP`, which can see a pending key.

## Console Commands

### Program Commands

| Command | Description |
| --- | --- |
| `RUN` | Clears variables, translates the stored program, and runs it. |
| `MEM` | Shows memory available for BASIC programs (out of 32 KiB) and variables (heap). |
| `LIST` | Lists the whole program. |
| `LIST n` | Lists line `n`. |
| `LIST n-m` | Lists lines `n` through `m` inclusive. |
| `NEW` | Deletes the program and clears variables. Also turns `TRACE` off. |
| `CLEAR` | Clears variables but keeps the program. |
| `RENUM [start[,step]]` | Renumbers the program (default `10,10`). Targets after `GOTO`, `GOSUB`, `THEN`, `ELSE`, `RESTORE`, and `RUN`, including `ON ... GOTO` lists, are updated. |
| `TRACE ON` / `TRACE OFF` | Turns statement tracing on or off for the next `RUN`. See [Tracing](#tracing). |
| `HELP` | Lists the console commands. |
| `CLS` | Clears the terminal screen (ANSI escape sequence). |

### SD Card Commands

Filenames and paths are quoted strings. Paths use `/` (backslashes are rejected)
and are relative to the current directory unless they start with `/`.

| Command | Description |
| --- | --- |
| `LOAD "file"` | Loads a program from the SD card, replacing the current program. `LOAD "TREK"` loads the built-in Star Trek program. |
| `SAVE "file"` | Saves the current program to the SD card. |
| `DIR` | Lists the current directory. |
| `PWD` | Prints the current directory. |
| `CD ["path"]` | Changes directory. No argument returns to `/`. |
| `MKDIR "path"` | Creates a directory. |
| `DEL "file"` | Deletes a file. |
| `SDCARD STATUS` | Shows card presence, type, capacity, and filesystem. |
| `SDCARD FREE` | Shows free space. |
| `SDCARD FORMAT [YES]` | Formats the card. A card that already has a valid filesystem requires `YES`. |

### WiFi Commands

| Command | Description |
| --- | --- |
| `WIFI STATUS` | Shows whether WiFi is connected and to which SSID. |
| `WIFI SCAN` | Lists nearby networks (only while disconnected). |
| `WIFI CONNECT "SSID" "PASSWORD"` | Connects and, on success, saves the credentials (up to five networks). |
| `WIFI DISCONNECT` | Disconnects. |
| `WIFI STORED` | Lists SSIDs with saved credentials. |
| `WIFI CLEAR` | Erases all saved credentials. |
| `WIFI LOAD "URL"` | Downloads a program over HTTP or HTTPS and loads it. |
| `WIFI LIST` | Downloads and lists the online program catalog. |
| `WIFI BLOAD "name"` | Downloads and loads a program from the catalog. |

At startup the board reconnects to the most recently used saved network that is
in range. Downloads require HTTP status 200, do not follow redirects, and are
limited to 32 KiB. HTTPS certificates are **not** verified, so use only trusted
networks and servers.

The catalog is `https://www.mewserver.org/basic/progs.json`, in the same format
as [progs.json](../basic/progs.json):

```json
{
    "programs": [
        {
            "filename" : "trek.bas",
            "type" : "game",
            "description" : "A classic Star Trek text-based game.",
            "size" : 28406
        }
    ]
}
```

`WIFI BLOAD` matches the name ignoring case, and the `.bas` extension is
optional. For example, `WIFI BLOAD "TREK"` downloads
`https://www.mewserver.org/basic/trek.bas`. The program is loaded only if the
downloaded size equals the catalog `size`.

### Graphics Command

`GFX INIT | CLEAR | RESET | UPDATE | INFO | LIST | HELP` controls the display.
Programs use the `GFX_*` functions described in [graphics.md](graphics.md).

## Program Structure

- Every program line starts with a line number from 1 to 65535.
- A line can hold several statements separated by colons:
  `20 X=1:Y=2:PRINT X+Y`.
- A line can be up to about 270 characters long, and the program can have up to
  640 lines and 32 KiB of text.
- Execution starts at the lowest line number and ends at `END`, `STOP`, or the
  last line.
- `REM` makes the rest of the line a comment, including any colons. An
  apostrophe (`'`) also starts a comment that runs to the end of the line.

## Data Types and Variables

| Type | Example | Notes |
| --- | --- | --- |
| Number | `A`, `X1`, `COUNT` | Integer or double-precision real. Results are converted automatically. |
| String | `A$`, `NAME$` | Names end in `$`. Literals use double quotes. |
| Array | `A(3)`, `G(8,8)`, `N$(5)` | Numeric or string; must be declared with `DIM`. |

- Names can be longer than two characters and can contain digits and `_`.
- Undeclared numeric variables start at 0.
- A scalar and an array can share a name; `N` and `N(3)` are separate.
- Names beginning with `BC` are reserved for the translator. Do not use them.
- Hexadecimal integer literals use `&H` followed by one or more hexadecimal
  digits, for example `&HADD8E6` for an RGB color. The prefix and digits are
  case-insensitive; values from `&H0` through `&HFFFFFFFF` are accepted.
  They work in both numbered programs and immediate statements. Larger values
  and malformed literals are rejected. Translation preserves quoted strings,
  comments, and the source shown by `LIST` and `SAVE`.

### Arrays

```basic
10 DIM A(10), G(8,8), N$(5)
20 H=12:V=9
30 DIM W(H,V)
```

- Upper bounds are **inclusive**. Index 0 is also valid, so `DIM A(10)` has 11
  elements, `A(0)` to `A(10)`.
- Bounds can be constants or expressions. Fractional bounds round down. A
  negative bound or an array too large for the heap is a runtime error.
- Constant bounds can be at most 65534.
- `DIM` can appear anywhere, including after a colon or inside an `IF`. Arrays
  with constant bounds exist before the program starts. Arrays with computed
  bounds are allocated when their `DIM` runs.
- Fractional subscripts truncate toward zero: `A(2.7)` is `A(2)`.
- Up to 32 arrays per program.

## Operators

In order of decreasing precedence:

| Operator | Meaning |
| --- | --- |
| `( )` | Grouping |
| `^` | Power |
| `-` (unary), `NOT` | Negation, logical NOT |
| `*`, `/`, `MOD` | Multiply, divide (always real), remainder |
| `+`, `-` | Add, subtract. `+` also joins strings. |
| `=`, `<>`, `<`, `>`, `<=`, `>=` | Comparisons. True is 1, false is 0. Strings compare as text. |
| `AND`, `OR` | Logical AND and OR |

`=` is both assignment and equality, depending on context. Integer division
(`\`) is not supported; use `INT(A/B)`. Division by zero gives `inf`; it is not
an error.

## Statements

### Assignment

```basic
10 LET A = 5
20 B$ = "HELLO" + " WORLD"
30 G(1,2) = A * 2
```

`LET` is optional.

### PRINT

```basic
10 PRINT "SCORE: "; S
20 PRINT "NO NEWLINE";
30 PRINT
40 PRINT TAB(10); "X"
```

- `;` and `,` both print values side by side with no spacing. Commas do **not**
  move to print zones.
- A trailing `;` or `,` suppresses the newline.
- `PRINT` with no arguments prints a blank line.
- Numbers use the shortest `%g` form: `2.5`, `1000`, `0.3`. There is no leading
  space before positive numbers.
- `?` is not an abbreviation for `PRINT`.

### INPUT

```basic
10 INPUT "YOUR NAME"; N$
20 INPUT "X,Y"; X, Y
30 INPUT A
```

- The prompt is optional. It is followed by `;` or `,`, and the console prints
  `? ` after it.
- Up to eight variables, entered on one line separated by commas.
- Numeric fields must be valid numbers. If any field is invalid or the count is
  wrong, an error is reported and **no** variables are changed.
- Only simple variables are allowed. To read into an array, read into a scalar
  first: `INPUT T:A(1)=T`.
- An input line can be up to 255 characters.

### GOTO, GOSUB, RETURN

```basic
10 GOSUB 100
20 GOTO 10
100 PRINT "SUB":RETURN
```

Targets are line numbers. `RETURN` returns to the statement after the `GOSUB`,
including later statements on the same line.

### ON ... GOTO / ON ... GOSUB

```basic
10 ON K GOTO 100, 200, 300
```

The expression is rounded down. A value of 1 selects the first target, 2 the
second, and so on. Values out of range fall through to the next statement.

### IF ... THEN ... ELSE

```basic
10 IF X > 5 THEN 100
20 IF X = 1 THEN PRINT "ONE": Y = 2
30 IF A$ = "Y" THEN PRINT "YES" ELSE PRINT "NO"
40 IF A > 0 THEN IF B > 0 THEN PRINT "BOTH"
```

- `IF` is single-line. The `THEN` part covers **the rest of the line**,
  including statements after colons. If the condition is false, execution
  continues on the next line.
- `THEN n` jumps to line `n`. `THEN n: statements` jumps without running the
  statements after it.
- `ELSE` is optional. It pairs with the nearest unmatched `IF`.
- `IF` can be nested up to 16 levels on one line.
- Quirk: an `IF` that compares two string **literals** with `<` or `>` (for
  example `IF "A"<"B"`) fails to parse. Compare variables instead.

### FOR ... NEXT

```basic
10 FOR I = 1 TO 10
20   PRINT I;
30 NEXT I
40 FOR J = 10 TO 1 STEP -2: PRINT J: NEXT
```

- The limit and step are evaluated once, at the `FOR`. The default step is 1.
  Steps can be negative or fractional.
- The body is skipped if the start is already past the limit.
- After the loop ends, the variable holds the first value past the limit
  (11 in the first example).
- The variable after `NEXT` is optional. If given, it must match the innermost
  `FOR`.
- `GOTO` out of a loop is allowed.
- Loops can be nested up to 32 deep. Each `FOR` must have a matching `NEXT`
  later in the source.

### DEF FN

```basic
10 DEF FNR(R) = INT(RND(R)*7.98+1.01)
20 DEF FNA(X,Y) = X + Y
30 DEF FNS$(X) = STR$(X)
40 PRINT FNR(1); FNA(2,3); FNS$(4)
```

Each function is a single expression. Parameters are local to the function, and
other variables are global. Functions can be defined anywhere in the program and
used before their definition line.

### Other Statements

| Statement | Description |
| --- | --- |
| `REM text` | Comment. |
| `END`, `STOP` | Ends the program normally and returns to `BASIC>`. |
| `RANDOMIZE n` | Seeds the random number generator with `n` (0 to 4294967295). |
| `SLEEP s` | Pauses for `s` **seconds** (fractions allowed). Ends early when a key is pressed. That key is then available to `INKEY$`. |

## Functions

### Classic Functions

| Function | Result |
| --- | --- |
| `INT(x)` | Largest integer ≤ `x` (`INT(-1.2)` is `-2`). |
| `RND(x)` | Random real in `[0,1)`. `x>0`: next value. `x=0`: repeats the last value. `x<0`: reseeds with `-x`, then returns a value. `RND` with no argument is the same as `RND(1)`. |
| `ABS(x)`, `SGN(x)` | Absolute value; sign (-1, 0, 1). |
| `SQR(x)` | Square root. |
| `SIN(x)`, `COS(x)`, `TAN(x)` | Trigonometry, in radians. |
| `ASIN(x)`, `ACOS(x)` | Inverse sine and cosine. (`ATN` is **not** available.) |
| `EXP(x)`, `LOG(x)` | eˣ; natural logarithm. |
| `CEIL(x)`, `FIX(x)`, `ROUND(x)` | Round up; truncate toward zero; round to nearest. |
| `LEN(s$)` | Length of a string. |
| `VAL(s$)` | Number parsed from a string. |
| `ASC(s$)` | Character code of the first character. |
| `CHR$(n)` | One-character string with code `n`. |
| `STR$(x)` | Number formatted as a string (no leading space). |
| `LEFT$(s$,n)` | First `n` characters. `n` is clamped to the length; 0 gives `""`. |
| `RIGHT$(s$,n)` | Last `n` characters, clamped. |
| `MID$(s$,start[,n])` | `n` characters starting at 1-based `start`. Without `n`, the rest of the string. A start past the end gives `""`. |
| `TAB(n)` | Spaces up to 1-based column `n` of the current line, for use in `PRINT`. Nothing if the column is already past `n`. |
| `TIMER` | Seconds since boot, as a real. |
| `INKEY$` | The pending key as a one-character string, or `""` if none. Does not wait. |

String functions work on ASCII bytes. Negative counts or a start below 1 are
runtime errors.

### Graphics Functions

The `GFX_*` functions (`GFX_CREATE`, `GFX_TEXT`, `GFX_SET_POS`, `GFX_COLOR`, and
others) create and update objects on the board display. See
[graphics.md](graphics.md).

### Native MY-BASIC Features

Other MY-BASIC built-ins and constructs still work, for example `WHILE`/`WEND`.
They are outside the classic dialect and are not covered by the regression
tests, so prefer the classic forms above in portable programs.

## Tracing

`TRACE ON` makes the next `RUN` print every statement as it executes:

```text
BASIC> 10 REM HI: THERE
BASIC> 20 X=1:PRINT "A";:Y=2
BASIC> 30 IF X=1 THEN PRINT "B"
BASIC> TRACE ON
Trace is ON.
BASIC> RUN
TRACE 10: REM HI: THERE
TRACE 20: X=1
TRACE 20: PRINT "A";
A
TRACE 20: Y=2
TRACE 30: IF X=1 THEN
TRACE 30: PRINT "B"
B
```

- Each colon-separated statement is traced separately, and `REM` lines are
  included.
- An `IF` prints its header (`IF X=1 THEN`, or `IF X=1 THEN 50` for a jump),
  then each statement in the branch that runs.
- Statements in loops are traced every time they run.
- Trace lines always start on a new line, even after `PRINT ...;`.
- The setting applies when `RUN` translates the program, not partway through a
  run. `NEW` and loading a program turn tracing off.
- In immediate mode, the whole input line is traced.

## Errors

The translator rejects invalid programs before they run, with
`BASIC parser: <reason>`. Examples include `IF requires THEN.`,
`NEXT without FOR.`, `FOR without NEXT.`, and `Invalid classic DIM declaration.`

Runtime errors print the MY-BASIC error code, the original source line number,
a column, and a description, then return to `BASIC>`. For example:

```text
MY-BASIC error 20 at 30:15: Index out of bound
```

The line number (30) is the original program line. The column refers to the
translated runtime text, so treat it as approximate.

## Limits

| Item | Limit |
| --- | --- |
| Program text | 32 KiB |
| Program lines | 640 |
| Line numbers | 1–65535 |
| Console or `INPUT` line | 255 characters |
| Translated program | 96 KiB and 4096 rows (large programs with `TRACE ON` use more) |
| Arrays per program | 32 |
| Constant `DIM` bound | 65534 |
| `INPUT` variables per statement | 8 |
| Nested `IF` on one line | 16 |
| Nested `FOR` | 32 |
| Download size | 32 KiB |
| Catalog entries | 32 |

## Differences from Classic BASIC

Not supported:

- `DATA`, `READ` (`RESTORE` is accepted but has no data to reset).
- `PRINT USING`, print zones for `,`, and `?` for `PRINT`.
- Integer division `\`, `ATN`, `UCASE`, `GET`, `POKE`/`PEEK`, `USR`, and
  `CLEAR n`.
- Multi-line `IF ... END IF` blocks in the classic dialect.
- `INPUT` into array elements.

Differs from classic BASIC:

- `SLEEP` uses seconds and can be ended early by a key.
- Numbers print without a leading or trailing space.
- Division by zero gives `inf` instead of an error.

## Example

```basic
10 REM GUESS THE NUMBER
20 RANDOMIZE TIMER*1000
30 N=INT(RND(1)*100)+1:T=0
40 INPUT "YOUR GUESS"; G
50 T=T+1
60 IF G<N THEN PRINT "HIGHER":GOTO 40
70 IF G>N THEN PRINT "LOWER":GOTO 40
80 PRINT "CORRECT IN ";T;" TRIES"
90 END
```

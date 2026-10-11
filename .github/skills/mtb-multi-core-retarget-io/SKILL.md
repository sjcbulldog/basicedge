---
name: mtb-multi-core-retarget-io
description: Configures debug output across the cores of a multi-core ModusToolbox application - one core owns the debug UART through retarget-io, and stdio from every other active core is routed to it over IPC. Use when more than one core needs printf(), logging, or serial debug output, when adding a library or feature to a second core, or when printf() asserts, hard-faults, or produces no output on a core that does not own the UART.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.0.0"
---

# mtb-multi-core-retarget-io

Extends the `mtb-retarget-io` skill to applications that build more than one core image: one core
owns the debug UART through `retarget-io`, and every other active core reaches that UART over IPC.

Each core builds a separate image with its own C library, so only one core may own the debug UART.
On any other core `printf()` reaches the toolchain's default write hook, which **asserts / halts** —
it is not silent. Any library that prints internally triggers this the moment it is linked in.

## When to Use This Skill

- A multi-core application needs `printf()`, `retarget-io`, serial debug or logging on any core
- A library or feature is about to be added to a core other than the UART owner
- User reports: "printf crashes on CM33", "hard fault in printf", "assert in `_write`", "no serial output from the second core"
- The `mtb-library-installer` skill or the `mtb-retarget-io` skill hands off here because the target
  project is not the UART owner

### When Not to Use This Skill

- Single-core devices or single-image applications — stop, use the `mtb-retarget-io` skill
- Ownership is already recorded and the target core already has a backend
- The core needs a *separate* physical UART, not the shared debug UART — use the
  `mtb-device-configurator` skill

---

## Workflow

### Step 1 — Multi-core gate (first, mandatory)

- No `MTB_PROJECTS` in the application root `Makefile` → **stop**, hand off to the
  `mtb-retarget-io` skill.
- Only one core in `DEVICE_*_CORES` in `bsps/TARGET_*/bsp.mk` → **stop**.

A multi-project split by `VCORE_ATTRS` (SECURE / NON_SECURE) on a single-core device is not
multi-core here, but those are still separate images — continue only if the user confirms more than
one of them prints.

### Step 2 — Classify each project

**Active** = runs application code or contains any `printf()`. **Inactive** = only BSP init plus a
core-launch call (e.g. `Cy_SysEnableCM55`), or never started. Only active projects need a backend.
Show the classification and let the user correct it.

### Step 3 — Ask who owns the UART

Ask the user which core should own the debug UART. Offer the core that already has `retarget-io` as
the recommended option, and each remaining active project from Step 2 as the alternatives. Make the
consequence explicit: retarget-io goes on the owner only, and every other active core routes
`printf()` to it over IPC. Ask even when one core already has `retarget-io` — offer it, don't assume it.

If asked: pick the core producing the most output (its prints skip the ring entirely), a core that
stays awake (an owner in Deep Sleep stops draining the ring), or the boot core if early messages
matter — producers cannot print until the owner initialises the ring.

Then confirm the fallback for each remaining active core: **route over IPC** (default) or **silent**
(compiles and runs, output discarded, never faults). **STOP** for the answers before editing.

### Step 4 — Set up the owner core

1. If `deps/retarget-io.mtb` is missing in the owner project, invoke the **`mtb-retarget-io`
   skill** for that project and return here.
2. If `retarget-io` is present in any **non-owner** project, remove it from that project's `deps/`
   and delete its init call — two owners on one UART corrupt each other's output. Confirm with
   the user before removing.
3. Copy `assets/mtb_core_stdio.c` and `assets/mtb_core_stdio.h` into the owner project.
4. Owner project `Makefile`:
   ```makefile
   DEFINES+=MTB_CORE_STDIO_HAS_RETARGET_IO
   ```
5. Call `mtb_core_stdio_init(MTB_CORE_STDIO_ROLE_OWNER)` immediately after retarget-io init,
   and start the drain task. See
   [ipc-print-channel.md](./references/ipc-print-channel.md#owner-core).

### Step 5 — Set up every other active core

Copy the same two asset files in, **without** the `MTB_CORE_STDIO_HAS_RETARGET_IO` define and
**without** `retarget-io`. Call `mtb_core_stdio_init()` early in `main()` with
`MTB_CORE_STDIO_ROLE_PRODUCER` or `MTB_CORE_STDIO_ROLE_NONE` per Step 3.

Do this for every active core **regardless of which libraries it contains**. Never inspect libraries
to decide whether a backend is needed — the set that prints internally is open-ended, and a backend
costs nothing on a core that never prints. Never modify `mtb_shared/` sources to suppress prints.

See [ipc-print-channel.md](./references/ipc-print-channel.md) for shared-memory placement, cache
requirements, and the multi-producer variant.

### Step 6 — Record the decision

Add to the application's agent instructions file (for example `copilot-instructions.md` or
`AGENTS.md`), replacing any earlier block:

```markdown
## Debug Output Ownership
- Owner: proj_cm55 — retarget-io, drains the IPC print ring
- proj_cm33_ns: active, PRODUCER — printf() routed over IPC, retarget-io must NOT be added
- proj_cm33_s: inactive — no stdio backend required
```

Every later skill run reads this block instead of re-asking or re-introducing a second owner.

### Step 7 — Verify

Build every modified project. In each non-owner map file the stdio hook must resolve to
`mtb_core_stdio.o`, not a toolchain object. Confirm exactly one project calls
`cy_retarget_io_init()`. Then output the `⏸️ FLASH CHECK` prompt and have the user confirm a
non-owner `printf()` reaches the terminal.

---

## Troubleshooting

| Symptom | Cause / Fix |
| --- | --- |
| `printf()` asserts, halts or hard-faults on a core | No stdio backend on that core — run Step 5 for it |
| Output appears intermittently or garbled | Two cores called `cy_retarget_io_init()` — Step 4.2 |
| Producer prints never appear | Owner never ran `mtb_core_stdio_init()`, or never calls `mtb_core_stdio_service()` |
| Only early producer prints are missing | Owner initialised the ring after the producer started; check `mtb_core_stdio_dropped()` |
| Producer prints appear then stop | Ring full — raise `MTB_CORE_STDIO_RING_SIZE` or drain more often |
| Cross-core reads return garbage | `.cy_sharedmem` maps to different addresses per core, or the region is cacheable — see the `mtb-ipc-patterns` skill |
| Duplicate symbol `_write` / `fputc` / `__write` at link | `MTB_CORE_STDIO_HAS_RETARGET_IO` missing on the owner project |
| Map file shows the toolchain's `_write` | `mtb_core_stdio.c` not in `SOURCES` for that project |

## References

- [ipc-print-channel.md](./references/ipc-print-channel.md) — transport, linker and cache requirements, verification
- [multi-core-debug-output.md](../mtb-device-configurator/references/multi-core-debug-output.md) — UART hand-off and lightweight `PutString` alternatives
- `mtb-ipc-patterns` skill — shared memory, cache maintenance, IPC semaphores on PSOC Edge
- `mtb-retarget-io` skill — installing retarget-io on the owner core

# IPC Print Channel — Implementation Reference

Transport detail for routing `printf()` from a non-owner core to the core that owns the debug UART.
Used by the `mtb-multi-core-retarget-io` skill.

## Why the retarget sits below `printf()`

`printf()` formats into a buffer and hands the bytes to a single C library hook — `_write` on GCC,
`fputc` on ARM Compiler, `__write` on IAR. Everything that prints passes through it: application
code, middleware, `assert()` output, and libraries added months from now.

```
your printf() ─┐
middleware printf() ─┼─► _write() ─► shared ring ─► owner core ─► UART
assert() output ─┘      (override)
```

Overriding that hook is the only **library-agnostic** approach. An `ipc_printf()` API would require
every call site to change — including call sites inside libraries that must not be modified.

> ⛔ Never modify library sources in `mtb_shared/` to silence or redirect prints. The override lives
> entirely in application source in the core's own project.

## Roles

| Role | Meaning | retarget-io linked? |
| --- | --- | --- |
| `OWNER` | Owns the physical UART, drains the ring | Yes |
| `PRODUCER` | Active core, no UART, routes stdio over the ring | **No** |
| `NONE` | Active core that must produce no output | **No** |

`NONE` still needs `mtb_core_stdio.c` in the build — the override is what stops an unbacked
`printf()` from reaching the toolchain default hook and faulting; the role just makes it discard.

**Inactive cores need nothing.** A core that is never started, or contains only BSP init plus a
core-launch call, links no `printf()`. Do not add files to it.

---

## Integration

Copy `mtb_core_stdio.c` / `.h` from the skill's `assets/` into every active project — each
project's `source/` directory, or a shared folder added to `INCLUDES` and `SOURCES` in both Makefiles.

### Owner core

```makefile
DEFINES+=MTB_CORE_STDIO_HAS_RETARGET_IO
```

This compiles out the C library override so it cannot collide with retarget-io's own definition.

```c
#include "cy_retarget_io.h"
#include "mtb_core_stdio.h"

int main(void)
{
    cybsp_init();

    /* Owner initialises the UART first, then the ring. */
    init_retarget_io();                                  /* or cy_retarget_io_init(...) */
    mtb_core_stdio_init(MTB_CORE_STDIO_ROLE_OWNER);

    printf("[owner] debug UART ready\r\n");

    __enable_irq();
    /* ... launch the other core(s) ... */
}
```

Drain from a lowest-priority FreeRTOS task (≥ 512-word stack) or the bare-metal main loop:

```c
static void core_stdio_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        if (0u == mtb_core_stdio_service())
        {
            vTaskDelay(pdMS_TO_TICKS(10));   /* Nothing pending — yield */
        }
    }
}
```

### Producer core

No Makefile `DEFINES` entry, and **no** `retarget-io` in `deps/`.

```c
#include "mtb_core_stdio.h"

int main(void)
{
    cybsp_init();
    mtb_core_stdio_init(MTB_CORE_STDIO_ROLE_PRODUCER);

    printf("[producer] routed over IPC\r\n");   /* No special API needed */
}
```

---

## Shared memory placement

The ring must resolve to the **same physical address** in every core's linker script, and should be
**non-cacheable**. `CY_SECTION_SHAREDMEM` works only when both linker scripts map `.cy_sharedmem`
identically — verify this; if the sections differ per core, use a fixed physical address instead. If
the region is cacheable on a core, build that core with
`DEFINES+=MTB_CORE_STDIO_SHAREDMEM_CACHED=1`. See the `mtb-ipc-patterns` skill for the PSOC Edge
details.

## Ordering and loss behaviour

| Situation | Behaviour |
| --- | --- |
| Producer prints before the owner ran `mtb_core_stdio_init()` | Discarded, counted in `mtb_core_stdio_dropped()` |
| Ring full | Tail of the new write dropped and counted |
| Owner in Deep Sleep | Not drained until the owner wakes; output resumes, then drops once the ring fills |
| Owner and producer print concurrently | Lines can interleave mid-line; data is never corrupted |
| Producer core resets | Owner keeps draining; indices resync on the next owner init |

If the owner is not the boot core, early producer output is lost. The write path never blocks and
never returns an error, so a stalled or absent consumer cannot deadlock the producer.

## Multiple producer cores

The shipped implementation is single-producer. For a third core, add one ring per producer, or guard
the head index with `Cy_IPC_Drv_LockAcquire` — the head update is not atomic across cores.

## Verification

Inspect the producer project's map file for the stdio hook:

```
grep -n "_write" build/*/*.map        # GCC
```

It must resolve to `mtb_core_stdio.o`. If it resolves to a toolchain object (`lib_a-writer.o`,
`syscalls.o`), the override is not linked and `printf()` will fault — check `mtb_core_stdio.c` is in
`SOURCES` and that `MTB_CORE_STDIO_HAS_RETARGET_IO` is **not** defined for this project.

On hardware: print from the producer and confirm the line appears on the owner's terminal.

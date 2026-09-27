# Multi-Core Debug Output (PSOC Edge)

> **Scope:** This reference covers sharing the **debug UART** (KitProg3 / retarget-io) across
> multiple cores for `printf()`-style diagnostic output. For application UARTs connected to
> serial peripherals, standard single-owner patterns in
> [uart-runtime-patterns.md](./uart-runtime-patterns.md) apply — no special multi-core handling needed.

## The Problem

On PSOC Edge dual/tri-core projects (CM33_S → CM33_NS → CM55), multiple cores may need debug output on the **same physical UART** (typically SCB2 via KitProg3). If both cores independently call `cy_retarget_io_init()`, the second init corrupts the first core's UART state — producing garbled output, lost messages, or complete silence.

```
Core A boots → inits UART → calls cy_retarget_io_init() → printf("Boot OK")
Core A launches Core B → Cy_SysEnableCM55(...)
Core B boots → calls cy_retarget_io_init() → RE-INITIALIZES same UART hardware
Core A printf() → uses stale context → OUTPUT LOST
```

**Symptoms:** Boot messages appear intermittently (timing-dependent), or "no serial output" despite successful builds. Adding delays sometimes "fixes" it — a red flag for a race condition, not a real solution.

## Solution Options

Options and how to select which option:

* Option A: Use when all the application logic is in the non-primary core.  For example, on E84 devices, use when the CM55 is in use and the CM33 merely launches the CM55.
* Option B: Use when the types and amount of printing from one core is lower than the other.
* Option C: Use for full capability.  Use this whenever there are significant activities requiring logging or debug prints from both cores.  Avoid when the additional complexity presents unnecessary risks.

---

## Option A: UART Hand-Off (Simplest — boot confirmation only)

**One core uses UART briefly for boot confirmation, then disables it before launching the second core.** The second core takes exclusive ownership.

```c
/* === Launching core (e.g., CM33_NS) main.c === */
#include "cybsp.h"
#include "cy_retarget_io.h"

int main(void)
{
    cybsp_init();
    cy_retarget_io_init(CYBSP_DEBUG_UART_HW);

    printf("[CM33] Boot OK — handing UART to CM55\r\n");

    /* Flush and disable before launching the other core */
    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW)) {}
    Cy_SCB_UART_Disable(CYBSP_DEBUG_UART_HW, NULL);

    __enable_irq();
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    for (;;)
    {
        /* Launching core has NO debug output from this point forward */
        Cy_SysLib_Delay(1000);
    }
}
```

**When to use Option A:** The launching core is primarily a bootloader/secure-init with no ongoing diagnostics needs. After hand-off, the second core inits UART cleanly with zero contention.

**Limitations:**
- Launching core is completely blind after hand-off — if it crashes later, no trace
- Only useful when one core has no post-launch debug requirements

---

## Option B: Single-Owner + Lightweight PutString (Recommended for simple projects)

**Exactly one core owns `cy_retarget_io_init()` and `printf()`.** The other core uses raw PDL `PutString`/`PutArrayBlocking` calls that bypass retarget-io entirely. Either core can be the owner — choose based on which core has the most debug output.

**Pattern: One core owns retarget-io, the other uses raw PDL**

```c
/* === Non-owner core main.c (whichever core does NOT own retarget-io) === */
#include "cybsp.h"
#include "cy_scb_uart.h"

/* Do NOT call cy_retarget_io_init() here — the other core owns it */
static cy_stc_scb_uart_context_t uart_ctx;

static void lightweight_uart_print(const char *msg)
{
    Cy_SCB_UART_PutString(CYBSP_DEBUG_UART_HW, msg);
    /* Block until TX FIFO drains to avoid interleaving */
    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW)) {}
}

int main(void)
{
    cybsp_init();

    /* Minimal UART init — just enable TX, no retarget-io */
    Cy_SCB_UART_Init(CYBSP_DEBUG_UART_HW, &CYBSP_DEBUG_UART_config, &uart_ctx);
    Cy_SCB_UART_Enable(CYBSP_DEBUG_UART_HW);

    lightweight_uart_print("\r\n[non-owner] Boot OK\r\n");

    /* ... application logic ... */

    for (;;)
    {
        /* Use lightweight_uart_print() for all debug output — NOT printf() */
        lightweight_uart_print("[non-owner] heartbeat\r\n");
        Cy_SysLib_Delay(2000);
    }
}
```

** When to use Option B:** When one core only needs a small amount of debugging, i.e. for capturing faults or unexpected errors, but doesn't need full logging or frequent serial debug prints.

> **Ownership decision:** If CM55 runs FreeRTOS with extensive application logging, CM55 should own retarget-io. If CM33 is the primary application core, CM33 should own it. The rule is simple: **only one core calls `cy_retarget_io_init()`**.

**Why this works:** `Cy_SCB_UART_PutString()` is stateless — it pushes bytes into the TX FIFO regardless of who initialized the SCB. As long as the SCB is enabled and the baud rate is configured, output appears. No context pointer is dereferenced for blocking puts.

**Limitations:**
- No `printf()` format strings on the non-owner core (use `snprintf` → `PutString` if needed)
- No RX capability on the lightweight side
- Interleaving possible if both cores transmit simultaneously (garbled lines, not lost data)

### Preventing Interleaving

The simplest approach is a cooperative `IsTxComplete` gate — it reduces interleaving but has a small race window between the check and the first byte entering the FIFO:

```c
static void lightweight_uart_print(const char *msg)
{
    /* Wait for any in-flight TX from either core */
    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW)) {}
    Cy_SCB_UART_PutString(CYBSP_DEBUG_UART_HW, msg);
    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW)) {}
}
```

For true mutual exclusion, use a hardware IPC semaphore. PSOC Edge provides dedicated `IPC_STRUCT` lock registers designed for inter-core synchronization — no shared memory or OS needed:

```c
#include "cy_ipc_drv.h"

/* Choose an unused IPC channel (check BSP — channels 0-7 often reserved for system) */
#define UART_LOCK_IPC_STRUCT  IPC_STRUCT8

static void lightweight_uart_print(const char *msg)
{
    /* Acquire hardware lock — spins until the other core releases */
    while (CY_IPC_DRV_SUCCESS != Cy_IPC_Drv_LockAcquire(UART_LOCK_IPC_STRUCT)) {}

    Cy_SCB_UART_PutString(CYBSP_DEBUG_UART_HW, msg);
    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW)) {}

    /* Release lock — other core can now transmit */
    Cy_IPC_Drv_LockRelease(UART_LOCK_IPC_STRUCT, CY_IPC_NO_NOTIFICATION);
}
```

> **IPC channel selection:** Channels 0–7 are typically reserved by the system (BTSS, Flash, etc.). Use channel 8+ for application locks. Check your BSP's `cy_ipc_config.h` for reserved allocations.

---

## Option C: IPC Print Channel (Recommended for production / high-throughput)

**One core owns the UART entirely.** Other cores send print requests via IPC (inter-processor communication). This eliminates all contention and guarantees ordered output. Either core can be the owner — the other becomes the producer.

**Architecture:**

```
Non-owner core (producer):
  snprintf(msg_buf, ...) → writes to IPC mailbox / shared memory ring buffer

Owner core (consumer + UART owner):
  FreeRTOS task polls IPC channel → printf("%s", received_msg)
```

**Pattern: IPC mailbox with shared memory ring buffer**

```c
/* === shared_print.h (included by both cores) === */
#include <stdint.h>
#include <stdbool.h>

#define PRINT_BUF_SIZE  256
#define PRINT_RING_SLOTS 8

typedef struct {
    char     data[PRINT_BUF_SIZE];
    uint16_t len;
    bool     ready;  /* Set by producer, cleared by consumer */
} print_slot_t;

typedef struct {
    print_slot_t slots[PRINT_RING_SLOTS];
    volatile uint8_t write_idx;  /* Producer writes */
    volatile uint8_t read_idx;   /* Consumer reads */
} shared_print_ring_t;

/* Place in shared memory region accessible to both cores */
extern shared_print_ring_t shared_print_ring
    __attribute__((section(".cy_sharedmem")));
```

```c
/* === Non-owner core: producer side === */
#include "shared_print.h"
#include <stdio.h>
#include <string.h>

void ipc_printf(const char *fmt, ...)
{
    uint8_t idx = shared_print_ring.write_idx;
    print_slot_t *slot = &shared_print_ring.slots[idx % PRINT_RING_SLOTS];

    /* Drop if ring is full (consumer hasn't drained) */
    if (slot->ready) return;

    va_list args;
    va_start(args, fmt);
    slot->len = (uint16_t)vsnprintf(slot->data, PRINT_BUF_SIZE, fmt, args);
    va_end(args);

    __DMB();  /* Ensure data is written before flag */
    slot->ready = true;
    shared_print_ring.write_idx = (idx + 1) % PRINT_RING_SLOTS;

    /* Optionally: trigger IPC interrupt to wake consumer immediately */
    /* Cy_IPC_Drv_SendMsgWord(IPC_STRUCT7, IPC_NOTIFY_PRINT, 0); */
}
```

```c
/* === Owner core: consumer side (FreeRTOS task) === */
#include "shared_print.h"
#include <stdio.h>

void print_consumer_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        uint8_t idx = shared_print_ring.read_idx;
        print_slot_t *slot = &shared_print_ring.slots[idx % PRINT_RING_SLOTS];

        if (slot->ready)
        {
            /* This core owns printf/retarget-io — safe to call */
            printf("%.*s", slot->len, slot->data);

            slot->ready = false;
            __DMB();
            shared_print_ring.read_idx = (idx + 1) % PRINT_RING_SLOTS;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));  /* Idle poll — or use IPC notify */
        }
    }
}
```

**When to use Option C:** Use when both cores need complete and predictable UART use, for logging, normal serial debug prints, etc.

**Linker configuration:** The shared memory region must be placed in a non-cached, non-secure RAM section accessible to both cores. Check the BSP linker scripts for `.cy_sharedmem` or equivalent sections.

**Advantages over Option B:**
- Guaranteed message ordering (FIFO)
- No interleaving — consumer serializes all output
- Full `printf()` formatting on the producer side
- Extensible to multiple producers (add core ID prefix)

**Disadvantages:**
- More complex setup (shared memory, linker config, consumer task)
- Slight latency (10ms poll, or IPC interrupt overhead)
- Ring buffer overflow drops messages if consumer is blocked

---

## Decision Guide

| Criteria | Option A (Hand-Off) | Option B (PutString) | Option C (IPC) |
|----------|---------------------|----------------------|----------------|
| Setup complexity | Trivial — disable + re-init | Low — swap `printf` for `PutString` | Medium — shared memory + consumer task |
| Output from non-owner after boot | ❌ None | ✅ Raw strings | ✅ Full printf-style |
| Output ordering | N/A (single owner) | Best-effort (interleaving possible) | Guaranteed FIFO |
| Format strings on non-owner | N/A | Manual `snprintf` → `PutString` | Full `printf`-style via `ipc_printf()` |
| Throughput | N/A | Limited by blocking TX waits | Higher — buffered, async |
| Good for | Bootloader/launcher cores | Prototyping, Phase 1 bringup, simple projects | Production, multi-task logging, high-frequency prints |

---

## Anti-Patterns (NEVER do these)

| ❌ Anti-Pattern | Why It Fails |
|---|---|
| Both cores call `cy_retarget_io_init()` | Second init corrupts first core's UART context |
| `Cy_SysLib_Delay(N)` as synchronization | Race condition — timing varies with code size, compiler, optimization |
| Sharing a single `cy_stc_scb_uart_context_t` across cores | Context is not thread-safe; concurrent access corrupts internal state |
| Using `printf()` on non-owner core after owner inits retarget-io | `stdout` is connected to the owner's retarget-io instance — non-owner writes go nowhere |

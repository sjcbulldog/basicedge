# Pattern 4: Bidirectional State + Command

## Quick Reference

| Item | Value |
|------|-------|
| **Use case** | Any dual-core app where one core owns state and the other sends commands |
| **state_t offset** | `+STATE_OFFSET` from shared region start — writer core publishes, reader core polls |
| **cmd_t offset** | `+CMD_OFFSET` from shared region start — opposite direction |
| **Address base** | Determined by Memory Configurator shared region (device-specific) |
| **DCache behavior** | Non-cacheable if MPU configured for shared region (BSP default on most devices) |

> **Device-specific values:** The shared memory base address, offsets, and MPU region depend on the target device. Check the linker/scatter files in `bsps/TARGET_*/COMPONENT_CM*/TOOLCHAIN_*/` or Memory Configurator for actual addresses.

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | `CY_SECTION_SHAREDMEM` maps to different linker sections per core | Data invisible to other core — each core sees its own copy |
| G2 | Fixed address must match Memory Configurator region start exactly | Wrong address = HardFault or stale data from wrong memory |
| G3 | IPC init (`memset`) must run BEFORE any writes | Wipes data written before init completes |
| G4 | Heavyweight ops in IPC poll task | Stack overflow — route through FreeRTOS event queue |
| G5 | Forgetting initial DCache invalidate after boot | Stale speculative prefetch lines may return garbage |

## Why Fixed Physical Addresses?

`CY_SECTION_SHAREDMEM` does **NOT** work for cross-core visibility on multi-core devices:

```c
/* ❌ WRONG — each core has its own .cy_sharedmem section */
CY_SECTION_SHAREDMEM static ipc_state_t shared_state;

/* ✅ CORRECT — fixed physical address, both cores see same bytes */
#define IPC_STATE_ADDR  (IPC_REGION_BASE + IPC_STATE_OFFSET)
static volatile ipc_state_t *const ipc_state = (ipc_state_t *)IPC_STATE_ADDR;
```

**Choosing offsets:**
- Place structs near the END of the shared region to avoid collision with other shared data (ring buffers, etc.)
- Use 64-byte separation between structs to prevent cache line sharing
- Verify against Memory Configurator: region size must accommodate all offsets

---

## Shared Header (Both Cores Include Identically)

```c
/* ipc_shared.h */
#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Device-specific addresses — obtain from Memory Configurator shared region.
 * Example values shown; replace with actual values for your device/BSP.
 */
#define IPC_REGION_BASE   0x261C0000u  /* From Memory Configurator: shared region start */
#define IPC_STATE_OFFSET  0x3F000u     /* Near end of region to avoid collisions */
#define IPC_CMD_OFFSET    0x3F040u     /* 64 bytes after state (cache line separation) */

#define IPC_STATE_ADDR    (IPC_REGION_BASE + IPC_STATE_OFFSET)
#define IPC_CMD_ADDR      (IPC_REGION_BASE + IPC_CMD_OFFSET)

#define IPC_MAGIC_VALID   0xCAFEBABEu

/* State struct: writer core → reader core */
typedef struct __attribute__((aligned(32))) {
    volatile uint32_t magic;       /* Written LAST — signals data valid */
    volatile uint32_t sequence;    /* Incremented on each update */
    /* Application-specific fields below */
    int32_t           payload;     /* Replace with actual state fields */
    uint8_t           _reserved[20];
} ipc_state_t;

/* Command struct: opposite direction */
typedef struct __attribute__((aligned(32))) {
    volatile uint32_t magic;       /* Written LAST — signals data valid */
    volatile uint32_t sequence;    /* Incremented on each command */
    uint8_t           cmd_type;    /* Application-defined command ID */
    uint8_t           _pad[3];
    int32_t           cmd_param;   /* Command parameter */
    uint8_t           _reserved[16];
} ipc_cmd_t;

/* Access macros — cast to volatile pointer at fixed address */
#define IPC_STATE  ((volatile ipc_state_t *)IPC_STATE_ADDR)
#define IPC_CMD    ((volatile ipc_cmd_t *)IPC_CMD_ADDR)

#endif /* IPC_SHARED_H */
```

---

## Writer Side (Publish State)

```c
/* ipc_writer.c — core that owns the state */
#include "ipc_shared.h"
#include <string.h>

static uint32_t state_seq = 0;

void ipc_init_writer(void)
{
    /* Clear speculative prefetch lines cached before MPU setup */
    SCB_InvalidateDCache_by_Addr((void *)IPC_STATE_ADDR, sizeof(ipc_state_t));
    SCB_InvalidateDCache_by_Addr((void *)IPC_CMD_ADDR, sizeof(ipc_cmd_t));

    /* Zero-init both structs (writer core owns init) */
    memset((void *)IPC_STATE, 0, sizeof(ipc_state_t));
    memset((void *)IPC_CMD, 0, sizeof(ipc_cmd_t));
    SCB_CleanDCache_by_Addr((void *)IPC_STATE_ADDR, sizeof(ipc_state_t));
    SCB_CleanDCache_by_Addr((void *)IPC_CMD_ADDR, sizeof(ipc_cmd_t));
}

void ipc_publish_state(int32_t payload)
{
    /* 1. Invalidate magic — signals "update in progress" */
    IPC_STATE->magic = 0;
    SCB_CleanDCache_by_Addr((void *)&IPC_STATE->magic, sizeof(uint32_t));

    /* 2. Write data fields */
    IPC_STATE->payload = payload;

    /* 3. Increment sequence */
    IPC_STATE->sequence = ++state_seq;

    /* 4. Write magic LAST — signals data valid */
    IPC_STATE->magic = IPC_MAGIC_VALID;

    /* 5. Flush entire struct to shared SRAM */
    SCB_CleanDCache_by_Addr((void *)IPC_STATE_ADDR, sizeof(ipc_state_t));
}
```

---

## Reader Side (Poll State + Send Commands)

```c
/* ipc_reader.c — core that consumes state and sends commands */
#include "ipc_shared.h"
#include <string.h>

static uint32_t last_state_seq = 0;
static uint32_t cmd_seq = 0;

void ipc_init_reader(void)
{
    /* Invalidate cache to see writer's zero-init */
    SCB_InvalidateDCache_by_Addr((void *)IPC_STATE_ADDR, sizeof(ipc_state_t));
    SCB_InvalidateDCache_by_Addr((void *)IPC_CMD_ADDR, sizeof(ipc_cmd_t));
}

bool ipc_poll_state(ipc_state_t *out_state)
{
    SCB_InvalidateDCache_by_Addr((void *)IPC_STATE_ADDR, sizeof(ipc_state_t));

    if (IPC_STATE->magic != IPC_MAGIC_VALID)
        return false;
    if (IPC_STATE->sequence == last_state_seq)
        return false;

    memcpy(out_state, (void *)IPC_STATE, sizeof(ipc_state_t));
    last_state_seq = out_state->sequence;
    return true;
}

void ipc_send_command(uint8_t cmd_type, int32_t param)
{
    /* 1. Invalidate magic */
    IPC_CMD->magic = 0;
    SCB_CleanDCache_by_Addr((void *)&IPC_CMD->magic, sizeof(uint32_t));

    /* 2. Write command */
    IPC_CMD->cmd_type = cmd_type;
    IPC_CMD->cmd_param = param;

    /* 3. Sequence */
    IPC_CMD->sequence = ++cmd_seq;

    /* 4. Magic LAST */
    IPC_CMD->magic = IPC_MAGIC_VALID;

    /* 5. Flush */
    SCB_CleanDCache_by_Addr((void *)IPC_CMD_ADDR, sizeof(ipc_cmd_t));
}
```

---

## Non-Cacheable MPU Configuration

The BSP typically configures an MPU region to mark the shared memory area as non-cacheable. With this configuration:

- **No runtime DCache Clean/Invalidate needed** — writes go directly to SRAM
- **Exception:** Initial invalidation after boot is still required to clear speculative prefetch lines cached before MPU setup

To verify MPU configuration in Device Configurator:
1. Open System personality
2. Check **Memory Protection Unit (MPU)** section
3. Confirm the shared region is marked as non-cacheable (TEX=0, C=0, B=0)

If your BSP uses different MPU settings, you may need the DCache operations shown in the code above.

---

## Init Ordering

```
┌─────────────────────────────────────────────────────────────┐
│ Writer Core Boot                                            │
│  1. cybsp_init()                                            │
│  2. ipc_init_writer()      ← Zero-init shared structs       │
│  3. IPC boot handshake     ← Signal reader core to proceed  │
│  4. Start app task         ← Begin publishing state         │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Reader Core Boot                                            │
│  1. cybsp_init()                                            │
│  2. Wait for writer ready  ← IPC boot handshake             │
│  3. ipc_init_reader()      ← Invalidate cache               │
│  4. Start app task         ← Begin polling state            │
└─────────────────────────────────────────────────────────────┘
```

> **Critical:** Writer core MUST complete `ipc_init_writer()` before reader core calls `ipc_init_reader()`. Use the IPC boot handshake pattern from the `mtb-dual-core-setup` skill to enforce this ordering.

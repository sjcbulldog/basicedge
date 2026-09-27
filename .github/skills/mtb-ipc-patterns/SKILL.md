---
name: mtb-ipc-patterns
description: Inter-processor communication patterns for PSOC Edge E84 dual-core projects. Use when implementing shared memory, message queues, ring buffers, bidirectional state/command, or any data exchange between CM33 and CM55 cores.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# IPC Communication Patterns — PSOC Edge Dual-Core

Inter-processor communication patterns for PSOC Edge E84 (CM33 + CM55).

> **Prerequisites:** Read `CONTEXT.md`. For initial IPC setup and boot sync, use the /dual-core-setup skill first.

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | **`ipc_manager_init()` calls `memset(0)` on shared memory** — must run BEFORE any IPC writes | Data silently wiped; intermittent "IPC not working" bugs |
| G2 | **Heavyweight operations in IPC poll task** — flash writes, factory reset, KVS erase overflow small stack | HardFault / stack corruption |
| G3 | **`CY_SECTION_SHAREDMEM` maps differently per core** — linker sections may not share the same physical address | Cross-core reads return garbage; use fixed physical addresses (Pattern 4) |

### G1 Detail — Init Ordering

```c
/* WRONG — ipc_manager_init() wipes WiFi status written above */
void app_main_task(void *arg) {
    start_wifi();           /* Writes to shared memory */
    ipc_manager_init();     /* memset(0) — WIPES everything! */
}

/* CORRECT — init shared memory first, then use it */
void app_main_task(void *arg) {
    ipc_manager_init();     /* FIRST — clears shared memory */
    start_wifi();           /* Now safe to write */
}
```

### G2 Detail — Route Heavyweight Ops Through Event Queue

```c
/* DON'T — stack overflow in small IPC poll task */
void ipc_poll_task(void *arg) {
    if (cmd == CMD_FACTORY_RESET)
        perform_factory_reset();  /* Too heavy for 512-byte stack */
}

/* DO — post to main task with larger stack */
void ipc_poll_task(void *arg) {
    if (cmd == CMD_FACTORY_RESET)
        xQueueSend(app_event_queue, &(app_event_t){.type = EVT_FACTORY_RESET}, 0);
}
```

---

## Pattern 1: Semaphore-Guarded Shared Memory

Best for: Periodic data updates where CM55 produces and CM33 consumes (e.g., sensor readings, DSP results).

```c
/* Shared between both cores — place in shared header */
#define SHARED_DATA_MAGIC  0xDEADBEEF

typedef struct {
    volatile uint32_t magic;      /* Written last — signals data is valid */
    volatile uint32_t sequence;   /* Incrementing counter for freshness check */
    float             values[4];  /* Application data */
} shared_data_t;

CY_SECTION_SHAREDMEM __attribute__((aligned(32)))
static shared_data_t shared_data;
```

> **⚠️ Warning:** `CY_SECTION_SHAREDMEM` works ONLY when both cores' linker scripts place `.cy_sharedmem` at the SAME physical address. On PSOC Edge multi-project workspaces, each core may have different linker section mappings. If cross-core visibility fails, use Pattern 4 (fixed physical addresses) instead.

**CM55 (producer):**
```c
void update_shared_data(float *new_values, uint32_t count)
{
    /* Invalidate cache before write to ensure coherency */
    SCB_CleanDCache_by_Addr((uint32_t*)&shared_data, sizeof(shared_data));

    shared_data.magic = 0;  /* Invalidate while updating */
    memcpy((void*)shared_data.values, new_values, count * sizeof(float));
    shared_data.sequence++;
    shared_data.magic = SHARED_DATA_MAGIC;  /* Mark valid — write last */

    /* Flush to shared SRAM so CM33 sees the update */
    SCB_CleanDCache_by_Addr((uint32_t*)&shared_data, sizeof(shared_data));
}
```

**CM33 (consumer):**
```c
bool read_shared_data(float *out_values, uint32_t count)
{
    /* Invalidate cache to get fresh data from shared SRAM */
    SCB_InvalidateDCache_by_Addr((uint32_t*)&shared_data, sizeof(shared_data));

    if (shared_data.magic != SHARED_DATA_MAGIC)
        return false;  /* Data not yet valid or being updated */

    static uint32_t last_seq = 0;
    if (shared_data.sequence == last_seq)
        return false;  /* No new data */

    memcpy(out_values, (void*)shared_data.values, count * sizeof(float));
    last_seq = shared_data.sequence;
    return true;
}
```

**Key points:**
- Magic sentinel written **last** acts as a lightweight lock
- DCache management is mandatory — CM55 has DCache, CM33 may or may not
- Align shared struct to 32 bytes to avoid cache line sharing with unrelated data

---

## Pattern 2: IPC Message Queue (Built-in)

Best for: Event-driven communication (state changes, commands, alerts). Uses ModusToolbox IPC library.

```c
/* Message types — define in shared header */
typedef enum {
    IPC_MSG_BOOT_READY    = 0x0001,
    IPC_MSG_STATE_CHANGE  = 0x0002,
    IPC_MSG_COMMAND       = 0x0003,
    IPC_MSG_HEARTBEAT     = 0x0004,
} ipc_msg_type_t;

typedef struct {
    uint32_t type;       /* ipc_msg_type_t */
    uint32_t param1;
    uint32_t param2;
    uint32_t param3;
} ipc_msg_t;  /* Must be exactly 16 bytes */
```

**Sending (CM55 → CM33):**
```c
void send_state_change(uint32_t old_state, uint32_t new_state)
{
    ipc_msg_t msg = {
        .type   = IPC_MSG_STATE_CHANGE,
        .param1 = old_state,
        .param2 = new_state,
        .param3 = xTaskGetTickCount()
    };

    cy_rslt_t result = mtb_ipc_send(&msg, sizeof(msg), IPC_TIMEOUT_MS);
    if (result == CY_RSLT_IPC_QUEUE_FULL)
    {
        /* Queue is full — skip this message, don't block the DSP pipeline */
        printf("[CM55] IPC queue full, skipping state update\n");
    }
}
```

**Receiving (CM33 side, in a FreeRTOS task):**
```c
void ipc_receiver_task(void *arg)
{
    ipc_msg_t rx_msg;
    for (;;)
    {
        cy_rslt_t result = mtb_ipc_receive(
            ipc_handle, &rx_msg, sizeof(rx_msg), portMAX_DELAY);

        if (result == CY_RSLT_SUCCESS)
        {
            switch (rx_msg.type)
            {
                case IPC_MSG_STATE_CHANGE:
                    handle_state_change(rx_msg.param1, rx_msg.param2);
                    break;
                case IPC_MSG_HEARTBEAT:
                    update_heartbeat_timestamp(rx_msg.param3);
                    break;
                default:
                    break;
            }
        }
    }
}
```

**Constraints:**
- Message size: exactly **16 bytes**
- Queue depth: **8 entries**
- If queue is full, `mtb_ipc_send()` returns `CY_RSLT_IPC_QUEUE_FULL`
- **Never** block CM55 DSP pipeline on IPC send — skip or buffer locally

> **Queue depth:** The built-in IPC message queue has a fixed 8-entry depth. If the sender generates commands faster than the receiver processes them, `mtb_ipc_send()` returns `CY_RSLT_IPC_QUEUE_FULL` and the command is lost. For applications where the final value matters more than every intermediate value (e.g., thermostat setpoint adjustments), this is acceptable — only the last value needs to arrive. For applications requiring guaranteed delivery (motor control, audio streaming), use Pattern 3 (Ring Buffer) with a larger capacity.

---

## Pattern 3: Ring Buffer over Shared Memory

Best for: Higher-throughput data transfer (audio samples, radar frames) where the built-in 8-entry queue is insufficient.

```c
#define RING_BUFFER_SIZE  16  /* Must be power of 2 */
#define RING_BUFFER_MASK  (RING_BUFFER_SIZE - 1)

typedef struct {
    float frame_data[64];  /* One radar frame or audio buffer */
    uint32_t frame_id;
    uint32_t timestamp;
} ring_entry_t;

typedef struct {
    volatile uint32_t write_idx;  /* Written by producer (CM55) only */
    volatile uint32_t read_idx;   /* Written by consumer (CM33) only */
    ring_entry_t entries[RING_BUFFER_SIZE];
} ring_buffer_t;

CY_SECTION_SHAREDMEM __attribute__((aligned(32)))
static ring_buffer_t ring_buf;
```

**CM55 (producer):**
```c
bool ring_write(const ring_entry_t *entry)
{
    uint32_t next_write = (ring_buf.write_idx + 1) & RING_BUFFER_MASK;

    SCB_InvalidateDCache_by_Addr((uint32_t*)&ring_buf.read_idx, sizeof(uint32_t));
    if (next_write == ring_buf.read_idx)
        return false;  /* Buffer full */

    memcpy((void*)&ring_buf.entries[ring_buf.write_idx], entry, sizeof(ring_entry_t));
    SCB_CleanDCache_by_Addr((uint32_t*)&ring_buf.entries[ring_buf.write_idx], sizeof(ring_entry_t));

    ring_buf.write_idx = next_write;
    SCB_CleanDCache_by_Addr((uint32_t*)&ring_buf.write_idx, sizeof(uint32_t));
    return true;
}
```

**CM33 (consumer):**
```c
bool ring_read(ring_entry_t *entry)
{
    SCB_InvalidateDCache_by_Addr((uint32_t*)&ring_buf.write_idx, sizeof(uint32_t));
    if (ring_buf.read_idx == ring_buf.write_idx)
        return false;  /* Buffer empty */

    SCB_InvalidateDCache_by_Addr(
        (uint32_t*)&ring_buf.entries[ring_buf.read_idx], sizeof(ring_entry_t));
    memcpy(entry, (void*)&ring_buf.entries[ring_buf.read_idx], sizeof(ring_entry_t));

    ring_buf.read_idx = (ring_buf.read_idx + 1) & RING_BUFFER_MASK;
    SCB_CleanDCache_by_Addr((uint32_t*)&ring_buf.read_idx, sizeof(uint32_t));
    return true;
}
```

**Key points:**
- Single-producer single-consumer (SPSC) — no lock needed
- Power-of-2 size enables mask-based wrapping (no modulo)
- Each index owned by one core only — no write contention
- DCache flush/invalidate on every read/write is mandatory

---

## Pattern 4: Bidirectional State + Command

Best for: Any dual-core application needing two-way communication (one core owns state, the other sends commands).

> **⛔ CRITICAL:** `CY_SECTION_SHAREDMEM` does NOT work for cross-core visibility. It maps to different per-core `.cy_sharedmem` linker sections. Use fixed physical addresses instead.

**Quick setup:**
- `state_t` at `0x261C0000 + 0x3F000` — writer core publishes state, reader core polls
- `cmd_t` at `0x261C0000 + 0x3F040` — reader core sends commands back

```c
/* Fixed physical address — both cores see same bytes */
#define IPC_STATE_ADDR  (0x261C0000u + 0x3F000u)
static volatile ipc_state_t *const ipc_state = (ipc_state_t *)IPC_STATE_ADDR;
```

**Write protocol:** invalidate magic → write data → increment seq → write magic → DCache clean

**Read protocol:** DCache invalidate → check magic → check sequence → consume

📖 **Full implementation:** [pattern-bidirectional.md](./references/pattern-bidirectional.md) — includes shared header, writer/reader code, and gotchas table.

---

## Choosing the Right Pattern

| Use case | Pattern | Throughput | Complexity |
|----------|---------|-----------|------------|
| State changes, commands | IPC Message Queue | Low (8 msg buffer) | Low |
| Periodic sensor readings | Semaphore-Guarded Shared Mem | Medium | Medium |
| Streaming data (radar, audio) | Ring Buffer | High | Higher |
| **Bidirectional state + commands** | **Bidirectional State + Command** | **Medium** | **Medium** |
| One-time boot synchronization | IPC Boot Handshake (see /dual-core-setup skill) | N/A | Low |

---

## IPC Command Routing: Pre-Framework vs. Post-Framework

IPC poll tasks start processing commands as soon as boot completes, but application frameworks (Matter, WiFi manager, MQTT) may not be initialized yet. Commands that depend on these frameworks will crash or silently corrupt state.

**Two-tier routing pattern:**
- **Immediate commands:** Local state queries, LED control, sensor reads — execute directly in the IPC handler callback
- **Deferred commands:** Matter attribute writes, WiFi state changes, MQTT publishes — post to the application's FreeRTOS event queue; the app task processes them after framework initialization is complete

```c
void ipc_handler(uint32_t cmd, const void *data) {
    switch (cmd) {
        case CMD_GET_SENSOR:    // Immediate — no framework dependency
            handle_sensor_query(data);
            break;
        case CMD_SET_SETPOINT:  // Deferred — needs application stack
            app_event_t evt = { .type = EVT_IPC_SETPOINT, .data = *(int16_t *)data };
            xQueueSend(app_event_queue, &evt, 0);
            break;
    }
}
```

---

## Producer Timeout / Stale Data Detection

When one core publishes data periodically (e.g., CM55 publishes sensor readings every 2 seconds), the consumer should detect when the producer stops updating:

```c
// Producer side (CM55): increment sequence counter on every update
shared->sensor_seq++;
shared->temperature = new_value;
SCB_CleanDCache_by_Addr(shared, sizeof(*shared));

// Consumer side (CM33): detect stale data
static uint32_t last_seq = 0;
static TickType_t last_update = 0;
if (shared->sensor_seq != last_seq) {
    last_seq = shared->sensor_seq;
    last_update = xTaskGetTickCount();
    use_temperature(shared->temperature);
} else if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(30000)) {
    mark_sensor_stale();
}
```

# M33_NS and M55 Message Channels

## Status

Implemented for CM33_NS and CM55. The complete workspace builds, and the bidirectional log/ready path plus blue/green LED behavior have been verified on hardware.

Debugger startup may launch the M55 startup sequence more than once. Repeated startup text while debugging is not by itself evidence of a reset loop.

## Goals

- Provide asynchronous M33 non-secure to M55 and M55 to M33 non-secure message channels.
- Keep all protocol state, message headers, payloads, queue entries, and allocator metadata in the shared `m33_m55_shared` memory region.
- Support payloads from 0 through 4096 bytes.
- Keep a message allocated until its receiving core destroys it.
- Signal M55 initialization completion to M33, and allow M33 to send bounded log text to M55.
- Keep all public opcode values in one common header.

## Existing Memory Constraint

The current BSP defines `m33_m55_shared` at `0x261C0000` with a size of `0x40000` bytes (256 KiB). Both CM33 and CM55 generated memory headers use this physical range.

The CM33 non-secure linker script places `.cy_shared_socmem` in this region. The CM55 linker now places the same section at the same region start and reserves the remaining tail as `.reserved_socmem`.

The protocol will use an explicitly shared linker section mapped to the same physical start and size in both images. It will not rely on the per-core `.cy_sharedmem` section, which the current linker scripts map to different allocatable regions.

## Architecture

The transport uses four MTB IPC queues on application channel 1. The BSP keeps channel 0 and its registered IRQ handlers for SRF; this protocol adds queue numbers 0-3 to the existing per-core MTB IPC instances. Queue items are 32-bit slot handles only. Message headers, payloads, queue control/data, allocator metadata, and slot pools reside in `.cy_shared_socmem` inside `m33_m55_shared`. FreeRTOS queue objects remain local to each core and are not shared between the two kernels.

Four single-producer/single-consumer rings reside in the shared protocol section:

| Ring | Producer | Consumer | Purpose |
|---|---|---|---|
| M33-to-M55 messages | M33_NS | M55 | Message handles awaiting M55 processing |
| M55-to-M33 messages | M55 | M33_NS | Message handles awaiting M33 processing |
| M33 pool returns | M55 destroy operation | M33_NS | Handles returned to the M33 allocator |
| M55 pool returns | M33_NS destroy operation | M55 | Handles returned to the M55 allocator |

Each core owns the allocation metadata and free list for its own message pool. A receiver does not allocate from or mutate the sender's free list. When the receiver destroys a message, it returns that message's handle through the corresponding return queue; the originating allocator drains that queue before its next allocation. This avoids a cross-core allocator lock while preserving receiver-controlled message lifetime. The queues each have one producer and one consumer; MTB IPC provides queue synchronization and task wakeup.

Each core has a high-priority message-receive task. It reads its MTB IPC queue nonblocking every 10 ms, so delivery does not depend solely on an IPC wake event. The task hands each message to its opcode handler and destroys it after processing.

## Message Format and Opcodes

The common header file, proposed as `message_protocol.h`, owns every public opcode definition and the shared message-format constants. The public message begins with this four-byte header:

| Field | Size | Meaning |
|---|---:|---|
| `opcode` | 16 bits | Operation identifier from the common opcode header |
| `payload_length` | 16 bits | Number of valid payload bytes; range 0-4096 |
| `payload` | 0-4096 bytes | Opaque operation data |

The payload is not required to be NUL-terminated. Handlers must use `payload_length`, validate it before access, and never treat payload data as a `printf` format string. Log payloads are opaque bytes from M33 stdout or stderr writes.

Initial opcode assignments:

| Value | Name | Direction | Payload / behavior |
|---:|---|---|---|
| `0x0001` | `MESSAGE_OPCODE_M55_INIT_COMPLETE` | M55 to M33_NS | Empty payload. M33_NS turns on the green LED when received. |
| `0x0002` | `MESSAGE_OPCODE_M33_LOG` | M33_NS to M55 | Up to 4096 bytes from an M33 stdout/stderr write. M55 writes the bytes to standard output. |

Opcode zero is reserved as invalid. New opcodes are added only to the common header. Compile-time checks will enforce a four-byte header and the 4096-byte maximum.

## Pool and Queue Sizing Proposal

Use fixed-size message slots. A slot stores the four-byte public header and up to 4096 payload bytes, rounded up to a 32-byte boundary. The compiled slot stride is 4128 bytes.

Reserve 16 slots for M33_NS-originated messages and 16 slots for M55-originated messages. This permits up to 16 outstanding messages in either direction, including queued and currently processed messages. A 16-entry message ring and 16-entry return ring per direction can represent that full outstanding set. Queue entries contain a slot handle, not a payload copy.

The two pools consume 132,096 bytes before queue and control metadata. The complete shared object occupies 133,056 bytes of the 256 KiB region; the M55 linker reserves the remaining 129,088 bytes. The CM33_NS and CM55 linker maps place the object at `0x261C0000` with the same size.

## Message Lifecycle

1. The sending core drains handles returned to its pool and allocates one free slot.
2. It initializes the opcode, payload length, and payload. It rejects payload lengths greater than 4096.
3. It publishes the slot handle to the direction's MTB IPC queue. Queue puts wait up to 100 ms for transient queue or lock contention; receiver tasks also poll every 10 ms.
4. The receiving task dequeues the handle, validates the handle, message length, and opcode, then processes the message.
5. After processing is complete and no code will reference the payload again, the receiver destroys the message by returning its handle through the sender's pool-return ring.
6. The sender's allocator drains the return ring and makes the slot reusable.

A receiver must not retain a payload pointer after destroy. A sender must not modify or reuse a slot between enqueue and return. If a send ring is full, send fails without transferring ownership; the sender retains and may retry or destroy the message locally. If a pool is exhausted, allocation fails without blocking an IPC handler.

## Startup and Application Flow

1. The existing secure CM33 boot chain starts CM33_NS. CM33_NS initializes the BSP and initializes the shared protocol control block and queues exactly once, before enabling M55.
2. CM33_NS creates its FreeRTOS tasks, enables M55, and then starts its scheduler, preserving the requirement that M55 starts before FreeRTOS.
3. The first scheduled CM33 task turns on the blue LED. This indicates that the CM33 scheduler and at least one application task are running.
4. M55 initializes the BSP and graphics application, invalidates any stale cache lines from its boot window, and waits for the protocol control block to report a valid version before using the shared rings. M55 must not clear or reinitialize shared protocol memory after CM33_NS has started it.
5. After graphics initialization is complete and the display is ready, M55 sends `MESSAGE_OPCODE_M55_INIT_COMPLETE` with an empty payload.
6. The M33_NS receive task handles that opcode and turns on the green LED. Blue indicates CM33 task startup; green indicates M55 graphics readiness.
7. When M33_NS sends `MESSAGE_OPCODE_M33_LOG`, the M55 receive task validates the bounded payload and writes it to standard output using the existing M55 retarget-io ownership.

The project keeps a single owner for the physical console: M55. M33_NS overrides newlib `_write` for file descriptors 1 and 2, divides larger writes into 4096-byte log messages, and reports partial writes when the message queue cannot accept more data. M55 uses `fwrite` to preserve the received bytes and caller-supplied line endings. M33_NS does not initialize retarget-io or access the M55 UART.

## Ordering and Cache Coherency

- The M33_NS initializer is the sole initializer of shared protocol memory and runs before M55 is enabled. An initialization magic value and protocol version are published last.
- CM55 checks the magic and version before accessing queue state; it never `memset`s the shared protocol section.
- Shared structures, slots, and queue data are aligned to at least 32 bytes. Message publication and consumption use hardware memory barriers, and MTB IPC serializes shared queue access.
- The effective CM55 MPU configuration marks the shared region non-cacheable (`MAIR` attribute `0x44`). CM55 invalidates stale D-cache lines for the full shared range immediately after `cybsp_init()` and before reading protocol state; subsequent message transfers use barriers and MTB IPC queue synchronization without per-message cache maintenance.
- The linker map for both cores must show the exact same physical protocol-section address and no overlap with `.reserved_socmem`, graphics buffers, or other shared data.

## Error Handling

- Reject unknown opcodes, oversized payload lengths, invalid handles, and corrupted control/version fields without reading beyond a slot.
- Return a valid dequeued slot after handling or rejection so a malformed message cannot permanently consume pool capacity.
- Queue sends and destroys use bounded 100 ms waits; allocation failures, invalid handles, and unknown opcodes do not result in out-of-bounds access. No message processing or `printf` occurs in an ISR.
- M55 log handling writes only the bounded payload bytes and does not interpret them as a format string.
- If scheduler startup fails, retain the existing fatal-error behavior rather than entering a false-ready state.

## Verification Plan

- Build CM33 secure, CM33 non-secure, and CM55 together; inspect both linker maps for shared-section address, size, and non-overlap.
- Verify header size, payload offset, slot stride, maximum payload, and queue bounds with compile-time assertions.
- Exercise empty and full queues, wraparound, zero-length payload, exactly 4096-byte payload, oversize rejection, pool exhaustion, receiver destroy, and slot reuse in both directions.
- On hardware, the blue LED after the first CM33 task, the green LED after M55 graphics initialization, and the M33 startup log on the M55 console were confirmed. Debugger startup may produce duplicate M55 startup output.
- Confirm a message remains unavailable while owned by the receiver and becomes reusable only after receiver destruction and return-handle processing.

## Implemented Choices

- Each direction has 16 message slots and a 16-entry message queue; two additional 16-entry queues return destroyed handles to their owning allocator.
- M55 initialization complete is sent after LVGL/display initialization and the light-blue screen setup. The M33 blue LED remains on; green indicates M55 readiness.
- M33 stdout/stderr writes are chunked at the 4096-byte payload limit and printed by M55 without adding line endings.
- MTB IPC channel 0 remains reserved for SRF; application handle queues use channel 1 and the existing BSP-registered queue interrupts.
- Message queue puts and destroys use a 100 ms timeout; receiver tasks poll every 10 ms to tolerate missed IPC wakeups during tickless idle.
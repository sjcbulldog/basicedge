/*******************************************************************************
 * File: mtb_core_stdio.h
 *
 * Cross-core stdio routing for multi-core ModusToolbox applications.
 *
 * In a multi-core application every core builds its own image with its own C
 * library. Only ONE core may own the physical debug UART (retarget-io). On any
 * other active core, printf() would otherwise resolve to the toolchain's
 * default write hook, which asserts / halts instead of producing output.
 *
 * This module retargets the C library write hook on non-owner cores into a
 * shared-memory ring buffer that the owner core drains onto the UART. No
 * library or call site needs to change: printf(), puts(), assert output and
 * any third-party middleware print all route transparently.
 *
 * Usage:
 *   Owner core     - link retarget-io, call mtb_core_stdio_init(OWNER),
 *                    then call mtb_core_stdio_service() periodically.
 *   Producer core  - do NOT link retarget-io, call
 *                    mtb_core_stdio_init(PRODUCER). printf() just works.
 *   Silent core    - call mtb_core_stdio_init(NONE). printf() is discarded
 *                    without faulting.
 ******************************************************************************/

#ifndef MTB_CORE_STDIO_H
#define MTB_CORE_STDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ring capacity in bytes. MUST be a power of two. */
#ifndef MTB_CORE_STDIO_RING_SIZE
#define MTB_CORE_STDIO_RING_SIZE (1024u)
#endif

/**
 * Set to 1 only if the shared memory section is mapped cacheable on this core.
 * Placing the ring in a non-cacheable shared region is strongly preferred.
 */
#ifndef MTB_CORE_STDIO_SHAREDMEM_CACHED
#define MTB_CORE_STDIO_SHAREDMEM_CACHED (0)
#endif

typedef enum
{
    MTB_CORE_STDIO_ROLE_NONE = 0, /**< Discard all stdio on this core. */
    MTB_CORE_STDIO_ROLE_OWNER,    /**< Owns the UART; drains the ring. */
    MTB_CORE_STDIO_ROLE_PRODUCER  /**< Routes stdio into the ring. */
} mtb_core_stdio_role_t;

/**
 * Initialise stdio routing for this core.
 *
 * The OWNER must run this before any producer prints; bytes produced earlier
 * are discarded and counted by mtb_core_stdio_dropped().
 *
 * On the owner core, call this AFTER cy_retarget_io_init().
 */
void mtb_core_stdio_init(mtb_core_stdio_role_t role);

/**
 * Drain buffered bytes to stdout. Owner core only; no-op on other roles.
 * Call from a low-priority task or the main loop.
 *
 * @return number of bytes emitted this call.
 */
uint32_t mtb_core_stdio_service(void);

/** Bytes discarded because the ring was full or the owner was not ready. */
uint32_t mtb_core_stdio_dropped(void);

/**
 * Low-level entry point used by the retargeted C library write hook.
 * Exposed so a core can forward output explicitly if its toolchain hook
 * cannot be overridden.
 *
 * @return @p len always, so printf() never reports a write error.
 */
int mtb_core_stdio_write(const void* buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* MTB_CORE_STDIO_H */

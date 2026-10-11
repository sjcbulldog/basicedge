/*******************************************************************************
 * File: mtb_core_stdio.c
 *
 * Implementation of cross-core stdio routing. See mtb_core_stdio.h.
 *
 * Build this file into EVERY active core of the application. The role passed
 * to mtb_core_stdio_init() selects the behaviour at runtime; the C library
 * write hook below is only ever reached on cores that do not link retarget-io,
 * because retarget-io provides its own definition of the same symbol.
 ******************************************************************************/

#include "mtb_core_stdio.h"

#include <stdio.h>
#include <string.h>

#include "cy_device_headers.h"
#include "cy_syslib.h"

#define MTB_CORE_STDIO_RING_MAGIC (0x5052494Eu) /* 'PRIN' */
#define MTB_CORE_STDIO_RING_MASK  (MTB_CORE_STDIO_RING_SIZE - 1u)

#if (MTB_CORE_STDIO_RING_SIZE & MTB_CORE_STDIO_RING_MASK) != 0u
#error "MTB_CORE_STDIO_RING_SIZE must be a power of two"
#endif

typedef struct
{
    volatile uint32_t magic;   /* Set by the owner once the ring is usable */
    volatile uint32_t head;    /* Producer writes, owner reads */
    volatile uint32_t tail;    /* Owner writes, producer reads */
    volatile uint32_t dropped; /* Bytes lost to a full ring */
    volatile uint8_t  data[MTB_CORE_STDIO_RING_SIZE];
} mtb_core_stdio_ring_t;

/* Must resolve to the same physical address in every core's linker script. */
CY_SECTION_SHAREDMEM __attribute__((aligned(32)))
static mtb_core_stdio_ring_t mtb_core_stdio_ring;

static mtb_core_stdio_role_t mtb_core_stdio_role = MTB_CORE_STDIO_ROLE_NONE;

#if (MTB_CORE_STDIO_SHAREDMEM_CACHED != 0) && defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
#define MTB_CORE_STDIO_CACHE_CLEAN()      SCB_CleanDCache_by_Addr((uint32_t*)&mtb_core_stdio_ring, \
                                                                 (int32_t)sizeof(mtb_core_stdio_ring))
#define MTB_CORE_STDIO_CACHE_INVALIDATE() SCB_InvalidateDCache_by_Addr((uint32_t*)&mtb_core_stdio_ring, \
                                                                      (int32_t)sizeof(mtb_core_stdio_ring))
#else
#define MTB_CORE_STDIO_CACHE_CLEAN()      do { } while (0)
#define MTB_CORE_STDIO_CACHE_INVALIDATE() do { } while (0)
#endif


void mtb_core_stdio_init(mtb_core_stdio_role_t role)
{
    mtb_core_stdio_role = role;

    if (MTB_CORE_STDIO_ROLE_OWNER == role)
    {
        mtb_core_stdio_ring.magic   = 0u;
        mtb_core_stdio_ring.head    = 0u;
        mtb_core_stdio_ring.tail    = 0u;
        mtb_core_stdio_ring.dropped = 0u;
        __DMB();
        mtb_core_stdio_ring.magic = MTB_CORE_STDIO_RING_MAGIC; /* Written last */
        MTB_CORE_STDIO_CACHE_CLEAN();
    }
}


int mtb_core_stdio_write(const void* buf, int len)
{
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t       head;
    uint32_t       tail;
    uint32_t       space;
    uint32_t       i = 0u;

    if ((len <= 0) || (NULL == buf) || (MTB_CORE_STDIO_ROLE_PRODUCER != mtb_core_stdio_role))
    {
        return (len > 0) ? len : 0; /* Silent discard - never fault the caller */
    }

    MTB_CORE_STDIO_CACHE_INVALIDATE();

    if (MTB_CORE_STDIO_RING_MAGIC != mtb_core_stdio_ring.magic)
    {
        return len; /* Owner has not initialised the ring yet */
    }

    head  = mtb_core_stdio_ring.head & MTB_CORE_STDIO_RING_MASK;
    tail  = mtb_core_stdio_ring.tail & MTB_CORE_STDIO_RING_MASK;
    space = MTB_CORE_STDIO_RING_SIZE - 1u - ((head - tail) & MTB_CORE_STDIO_RING_MASK);

    while ((i < (uint32_t)len) && (i < space))
    {
        mtb_core_stdio_ring.data[(head + i) & MTB_CORE_STDIO_RING_MASK] = src[i];
        i++;
    }

    if ((uint32_t)len > i)
    {
        mtb_core_stdio_ring.dropped += (uint32_t)len - i;
    }

    __DMB(); /* Payload must be visible before the index that publishes it */
    mtb_core_stdio_ring.head = (head + i) & MTB_CORE_STDIO_RING_MASK;
    MTB_CORE_STDIO_CACHE_CLEAN();

    return len;
}


uint32_t mtb_core_stdio_service(void)
{
    uint8_t  chunk[64];
    uint32_t head;
    uint32_t tail;
    uint32_t count = 0u;

    if (MTB_CORE_STDIO_ROLE_OWNER != mtb_core_stdio_role)
    {
        return 0u;
    }

    MTB_CORE_STDIO_CACHE_INVALIDATE();

    head = mtb_core_stdio_ring.head & MTB_CORE_STDIO_RING_MASK;
    tail = mtb_core_stdio_ring.tail & MTB_CORE_STDIO_RING_MASK;

    while ((tail != head) && (count < sizeof(chunk)))
    {
        chunk[count] = mtb_core_stdio_ring.data[tail];
        tail         = (tail + 1u) & MTB_CORE_STDIO_RING_MASK;
        count++;
    }

    if (0u != count)
    {
        (void)fwrite(chunk, 1u, count, stdout);
        (void)fflush(stdout);

        __DMB();
        mtb_core_stdio_ring.tail = tail;
        MTB_CORE_STDIO_CACHE_CLEAN();
    }

    return count;
}


uint32_t mtb_core_stdio_dropped(void)
{
    MTB_CORE_STDIO_CACHE_INVALIDATE();
    return mtb_core_stdio_ring.dropped;
}


/*******************************************************************************
 * C library retargeting
 *
 * Compiled only when retarget-io is absent, which is exactly the non-owner
 * cores. On the owner core retarget-io defines these symbols and this block
 * must stay out of the link to avoid a duplicate definition.
 ******************************************************************************/
#if !defined(MTB_CORE_STDIO_HAS_RETARGET_IO)

#if defined(__ICCARM__)

#include <yfuns.h>

size_t __write(int handle, const unsigned char* buffer, size_t size)
{
    (void)handle;
    return (size_t)mtb_core_stdio_write(buffer, (int)size);
}

#elif defined(__ARMCC_VERSION)

int fputc(int ch, FILE* f)
{
    char c = (char)ch;
    (void)f;
    (void)mtb_core_stdio_write(&c, 1);
    return ch;
}

#elif defined(__GNUC__)

int _write(int fd, const char* ptr, int len)
{
    (void)fd;
    return mtb_core_stdio_write(ptr, len);
}

#else
#error "Unsupported toolchain - add the stdio write hook for it here"
#endif

#endif /* !MTB_CORE_STDIO_HAS_RETARGET_IO */

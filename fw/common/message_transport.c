#include "message_transport.h"

#include <stddef.h>
#include <string.h>

#include "cybsp.h"
#include "task.h"
#include "message_transport_internal.h"

#define MESSAGE_SHARED_MAGIC          (0x4D534731UL)
#define MESSAGE_SHARED_VERSION        (1UL)
#define MESSAGE_IPC_CHANNEL           (MTB_IPC_CHAN_1)
#define MESSAGE_IPC_QUEUE_SEMA_BASE   (40UL)
#define MESSAGE_QUEUE_OPERATION_TIMEOUT_US (100000UL)
#define MESSAGE_HANDLE_INDEX_MASK     (0xFFUL)
#define MESSAGE_HANDLE_GENERATION_MAX (0x00FFFFFFUL)

#define MESSAGE_BUILD_ASSERT(condition, name) \
    typedef char message_build_assert_##name[(condition) ? 1 : -1]

MESSAGE_BUILD_ASSERT((sizeof(message_slot_t) % 32U) == 0U, slot_stride_alignment);
MESSAGE_BUILD_ASSERT(MESSAGE_POOL_SLOTS <= 256U, handle_index_capacity);

__attribute__((section(".cy_shared_socmem"), aligned(32), used))
message_shared_state_t message_shared_state;

static mtb_ipc_queue_t local_queue_handles[MESSAGE_QUEUE_COUNT];
static bool local_transport_ready;

#if CY_SYSTEM_CPU_M33
extern mtb_ipc_t cybsp_cm33_ipc_instance;
#define MESSAGE_LOCAL_POOL 0U
#define MESSAGE_LOCAL_IPC_INSTANCE (&cybsp_cm33_ipc_instance)
#else
#define MESSAGE_LOCAL_POOL 1U
#define MESSAGE_LOCAL_IPC_INSTANCE (&cybsp_cm55_ipc_instance)
#endif

static uint32_t message_handle_make(uint32_t slot, uint32_t generation)
{
    return ((generation & MESSAGE_HANDLE_GENERATION_MAX) << 8U) | slot;
}

static uint32_t message_handle_slot(message_handle_t handle)
{
    return handle & MESSAGE_HANDLE_INDEX_MASK;
}

static uint32_t message_handle_generation(message_handle_t handle)
{
    return handle >> 8U;
}

static uint32_t message_local_send_queue(void)
{
    return (MESSAGE_LOCAL_POOL == 0U) ? MESSAGE_QUEUE_M33_TO_M55 : MESSAGE_QUEUE_M55_TO_M33;
}

static uint32_t message_local_receive_queue(void)
{
    return (MESSAGE_LOCAL_POOL == 0U) ? MESSAGE_QUEUE_M55_TO_M33 : MESSAGE_QUEUE_M33_TO_M55;
}

static uint32_t message_local_return_queue(void)
{
    return (MESSAGE_LOCAL_POOL == 0U) ? MESSAGE_QUEUE_M33_RETURNS : MESSAGE_QUEUE_M55_RETURNS;
}

static uint32_t message_remote_pool_return_queue(void)
{
    return (MESSAGE_LOCAL_POOL == 0U) ? MESSAGE_QUEUE_M55_RETURNS : MESSAGE_QUEUE_M33_RETURNS;
}

#if CY_SYSTEM_CPU_M33
static bool message_initialize_queues(void)
{
    for (uint32_t queue_index = 0U; queue_index < MESSAGE_QUEUE_COUNT; queue_index++)
    {
        mtb_ipc_queue_config_t config =
        {
            .channel_num = MESSAGE_IPC_CHANNEL,
            .queue_num = queue_index,
            .max_num_items = MESSAGE_QUEUE_DEPTH,
            .item_size = sizeof(message_handle_t),
            .queue_pool = message_shared_state.queue_pools[queue_index].handles,
            .semaphore_num = MESSAGE_IPC_QUEUE_SEMA_BASE + queue_index
        };

        if (CY_RSLT_SUCCESS != mtb_ipc_queue_init(MESSAGE_LOCAL_IPC_INSTANCE,
                                                 &local_queue_handles[queue_index],
                                                 &message_shared_state.queues[queue_index].data,
                                                 &config))
        {
            return false;
        }
    }

    return true;
}
#endif

bool message_transport_initialize(void)
{
#if CY_SYSTEM_CPU_M33
    memset(&message_shared_state, 0, sizeof(message_shared_state));

    for (uint32_t pool_index = 0U; pool_index < MESSAGE_POOL_COUNT; pool_index++)
    {
        message_shared_state.free_count[pool_index] = MESSAGE_POOL_SLOTS;
        for (uint32_t slot_index = 0U; slot_index < MESSAGE_POOL_SLOTS; slot_index++)
        {
            message_shared_state.free_slots[pool_index][slot_index] = slot_index;
            message_shared_state.metadata[pool_index][slot_index].state = MESSAGE_SLOT_FREE;
        }
    }

    if (!message_initialize_queues())
    {
        memset(&message_shared_state, 0, sizeof(message_shared_state));
        return false;
    }

    __DMB();
    message_shared_state.version = MESSAGE_SHARED_VERSION;
    message_shared_state.magic = MESSAGE_SHARED_MAGIC;
    local_transport_ready = true;
    return true;
#else
    return false;
#endif
}

bool message_transport_attach(void)
{
#if CY_SYSTEM_CPU_M55
    __DMB();
    if ((message_shared_state.magic != MESSAGE_SHARED_MAGIC) ||
        (message_shared_state.version != MESSAGE_SHARED_VERSION))
    {
        return false;
    }

    for (uint32_t queue_index = 0U; queue_index < MESSAGE_QUEUE_COUNT; queue_index++)
    {
        if (CY_RSLT_SUCCESS != mtb_ipc_queue_get_handle(MESSAGE_LOCAL_IPC_INSTANCE,
                                                        &local_queue_handles[queue_index],
                                                        MESSAGE_IPC_CHANNEL,
                                                        queue_index))
        {
            return false;
        }
    }

    local_transport_ready = true;
    return true;
#else
    return local_transport_ready;
#endif
}

static void message_drain_local_returns(void)
{
    message_handle_t handle;
    uint32_t return_queue = message_local_return_queue();

    while (CY_RSLT_SUCCESS == mtb_ipc_queue_get(&local_queue_handles[return_queue],
                                               &handle, 0U))
    {
        uint32_t slot_index = message_handle_slot(handle);
        uint32_t generation = message_handle_generation(handle);

        if ((slot_index < MESSAGE_POOL_SLOTS) &&
            (message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index].generation == generation) &&
            (message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index].state == MESSAGE_SLOT_SENT))
        {
            taskENTER_CRITICAL();
            message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index].state = MESSAGE_SLOT_FREE;
            message_shared_state.free_slots[MESSAGE_LOCAL_POOL]
                                            [message_shared_state.free_count[MESSAGE_LOCAL_POOL]++] = slot_index;
            taskEXIT_CRITICAL();
        }
    }
}

bool message_transport_allocate(uint16_t opcode, uint16_t payload_length,
                                message_t **message)
{
    if (!local_transport_ready || (message == NULL) ||
        (payload_length > MESSAGE_MAX_PAYLOAD_SIZE) || (opcode == 0U))
    {
        return false;
    }

    message_drain_local_returns();

    taskENTER_CRITICAL();
    uint32_t free_count = message_shared_state.free_count[MESSAGE_LOCAL_POOL];
    if (free_count == 0U)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    uint32_t slot_index = message_shared_state.free_slots[MESSAGE_LOCAL_POOL][--free_count];
    message_shared_state.free_count[MESSAGE_LOCAL_POOL] = free_count;
    message_slot_metadata_t *metadata = &message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index];
    uint32_t generation = (metadata->generation + 1U) & MESSAGE_HANDLE_GENERATION_MAX;
    if (generation == 0U)
    {
        generation = 1U;
    }
    metadata->generation = generation;
    metadata->state = MESSAGE_SLOT_LOCAL;
    taskEXIT_CRITICAL();

    message_t *allocated = &message_shared_state.slots[MESSAGE_LOCAL_POOL][slot_index].message;
    allocated->opcode = opcode;
    allocated->payload_length = payload_length;
    *message = allocated;
    return true;
}

static bool message_find_local_slot(message_t *message, uint32_t *slot_index)
{
    uintptr_t pool_start = (uintptr_t)&message_shared_state.slots[MESSAGE_LOCAL_POOL][0];
    uintptr_t message_address = (uintptr_t)message;
    uintptr_t message_offset;

    if ((message == NULL) || (message_address < pool_start))
    {
        return false;
    }

    message_offset = message_address - pool_start;
    if ((message_offset % sizeof(message_slot_t)) != offsetof(message_slot_t, message))
    {
        return false;
    }

    *slot_index = (uint32_t)(message_offset / sizeof(message_slot_t));
    return *slot_index < MESSAGE_POOL_SLOTS;
}

bool message_transport_send(message_t *message)
{
    uint32_t slot_index;
    if (!local_transport_ready || !message_find_local_slot(message, &slot_index) ||
        (message->payload_length > MESSAGE_MAX_PAYLOAD_SIZE))
    {
        return false;
    }

    taskENTER_CRITICAL();
    message_slot_metadata_t *metadata = &message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index];
    if (metadata->state != MESSAGE_SLOT_LOCAL)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    metadata->state = MESSAGE_SLOT_SENT;
    uint32_t handle = message_handle_make(slot_index, metadata->generation);
    taskEXIT_CRITICAL();

    __DMB();
    uint32_t queue_index = message_local_send_queue();
    if (CY_RSLT_SUCCESS != mtb_ipc_queue_put(&local_queue_handles[queue_index], &handle,
                                             MESSAGE_QUEUE_OPERATION_TIMEOUT_US))
    {
        taskENTER_CRITICAL();
        if ((metadata->generation == message_handle_generation(handle)) &&
            (metadata->state == MESSAGE_SLOT_SENT))
        {
            metadata->state = MESSAGE_SLOT_LOCAL;
        }
        taskEXIT_CRITICAL();
        return false;
    }

    return true;
}

bool message_transport_receive(uint64_t timeout_us, message_handle_t *handle,
                               message_t **message)
{
    if (!local_transport_ready || (handle == NULL) || (message == NULL))
    {
        return false;
    }

    uint32_t queue_index = message_local_receive_queue();
    if (CY_RSLT_SUCCESS != mtb_ipc_queue_get(&local_queue_handles[queue_index], handle, timeout_us))
    {
        return false;
    }

    __DMB();
    uint32_t owner_pool = (MESSAGE_LOCAL_POOL == 0U) ? 1U : 0U;
    uint32_t slot_index = message_handle_slot(*handle);
    uint32_t generation = message_handle_generation(*handle);

    if ((slot_index >= MESSAGE_POOL_SLOTS) ||
        (message_shared_state.metadata[owner_pool][slot_index].generation != generation) ||
        (message_shared_state.metadata[owner_pool][slot_index].state != MESSAGE_SLOT_SENT))
    {
        return false;
    }

    *message = &message_shared_state.slots[owner_pool][slot_index].message;
    return true;
}

bool message_transport_destroy(message_handle_t handle)
{
    uint32_t owner_pool = (MESSAGE_LOCAL_POOL == 0U) ? 1U : 0U;
    uint32_t slot_index = message_handle_slot(handle);
    uint32_t generation = message_handle_generation(handle);

    if (!local_transport_ready || (slot_index >= MESSAGE_POOL_SLOTS) ||
        (message_shared_state.metadata[owner_pool][slot_index].generation != generation) ||
        (message_shared_state.metadata[owner_pool][slot_index].state != MESSAGE_SLOT_SENT))
    {
        return false;
    }

    __DMB();
    uint32_t queue_index = message_remote_pool_return_queue();
    return CY_RSLT_SUCCESS == mtb_ipc_queue_put(&local_queue_handles[queue_index], &handle,
                                                MESSAGE_QUEUE_OPERATION_TIMEOUT_US);
}

bool message_transport_release_local(message_t *message)
{
    uint32_t slot_index;
    if (!local_transport_ready || !message_find_local_slot(message, &slot_index))
    {
        return false;
    }

    taskENTER_CRITICAL();
    message_slot_metadata_t *metadata = &message_shared_state.metadata[MESSAGE_LOCAL_POOL][slot_index];
    if ((metadata->state != MESSAGE_SLOT_LOCAL) ||
        (message_shared_state.free_count[MESSAGE_LOCAL_POOL] >= MESSAGE_POOL_SLOTS))
    {
        taskEXIT_CRITICAL();
        return false;
    }

    metadata->state = MESSAGE_SLOT_FREE;
    message_shared_state.free_slots[MESSAGE_LOCAL_POOL]
                                    [message_shared_state.free_count[MESSAGE_LOCAL_POOL]++] = slot_index;
    taskEXIT_CRITICAL();
    return true;
}
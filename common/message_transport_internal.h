#ifndef MESSAGE_TRANSPORT_INTERNAL_H
#define MESSAGE_TRANSPORT_INTERNAL_H

#include "message_protocol.h"
#include "mtb_ipc.h"
#include "mtb_ipc_impl.h"

#define MESSAGE_POOL_COUNT 2U
#define MESSAGE_POOL_SLOTS 16U
#define MESSAGE_QUEUE_COUNT 4U
#define MESSAGE_QUEUE_DEPTH 16U

typedef enum
{
    MESSAGE_QUEUE_M33_TO_M55 = 0U,
    MESSAGE_QUEUE_M55_TO_M33 = 1U,
    MESSAGE_QUEUE_M33_RETURNS = 2U,
    MESSAGE_QUEUE_M55_RETURNS = 3U
} message_queue_index_t;

typedef enum
{
    MESSAGE_SLOT_FREE = 0U,
    MESSAGE_SLOT_LOCAL = 1U,
    MESSAGE_SLOT_SENT = 2U
} message_slot_state_t;

typedef struct __attribute__((aligned(32)))
{
    message_t message;
} message_slot_t;

typedef struct __attribute__((aligned(32)))
{
    mtb_ipc_queue_data_t data;
} message_queue_data_t;

typedef struct __attribute__((aligned(32)))
{
    uint32_t handles[MESSAGE_QUEUE_DEPTH];
} message_queue_pool_t;

typedef struct
{
    volatile uint32_t generation;
    volatile uint32_t state;
} message_slot_metadata_t;

typedef struct __attribute__((aligned(32)))
{
    volatile uint32_t magic;
    volatile uint32_t version;
    message_queue_data_t queues[MESSAGE_QUEUE_COUNT];
    message_queue_pool_t queue_pools[MESSAGE_QUEUE_COUNT];
    uint32_t free_count[MESSAGE_POOL_COUNT];
    uint32_t free_slots[MESSAGE_POOL_COUNT][MESSAGE_POOL_SLOTS];
    message_slot_metadata_t metadata[MESSAGE_POOL_COUNT][MESSAGE_POOL_SLOTS];
    message_slot_t slots[MESSAGE_POOL_COUNT][MESSAGE_POOL_SLOTS];
} message_shared_state_t;

extern message_shared_state_t message_shared_state;

#endif
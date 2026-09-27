#ifndef MESSAGE_TRANSPORT_H
#define MESSAGE_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "message_protocol.h"

bool message_transport_initialize(void);
bool message_transport_attach(void);
bool message_transport_allocate(uint16_t opcode, uint16_t payload_length,
                                message_t **message);
bool message_transport_send(message_t *message);
bool message_transport_receive(uint64_t timeout_us, message_handle_t *handle,
                               message_t **message);
bool message_transport_destroy(message_handle_t handle);
bool message_transport_release_local(message_t *message);

#endif
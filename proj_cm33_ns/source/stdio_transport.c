#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "message_log.h"
#include "message_transport.h"

bool message_log_write(message_log_subsystem_t subsystem,
                       const char *text,
                       size_t length)
{
    if (((uint32_t)subsystem >= MESSAGE_LOG_SUBSYSTEM_COUNT) ||
        ((length > 0U) && (text == NULL)))
    {
        return false;
    }

    size_t offset = 0U;
    while (offset < length)
    {
        uint16_t text_length = (uint16_t)(((length - offset) >
                                           (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_log_header_t)))
                                              ? (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_log_header_t))
                                              : (length - offset));
        message_t *message;
        uint16_t payload_length = (uint16_t)(sizeof(message_log_header_t) + text_length);
        if (!message_transport_allocate(MESSAGE_OPCODE_M33_LOG,
                                        payload_length,
                                        &message))
        {
            return false;
        }

        message_log_header_t header =
        {
            .subsystem = (uint8_t)subsystem,
            .reserved = 0U,
            .text_length = text_length
        };
        memcpy(message->payload, &header, sizeof(header));
        memcpy(&message->payload[sizeof(header)], &text[offset], text_length);
        if (!message_transport_send(message))
        {
            (void)message_transport_release_local(message);
            return false;
        }
        offset += text_length;
    }
    return true;
}

int message_log_printf(message_log_subsystem_t subsystem,
                       const char *format,
                       ...)
{
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if (written < 0)
    {
        return written;
    }
    size_t text_length = ((size_t)written < sizeof(text))
                             ? (size_t)written
                             : sizeof(text) - 1U;
    return message_log_write(subsystem, text, text_length) ? written : -1;
}

int _write(int file, char *buffer, int length)
{
    if ((file != 1) && (file != 2))
    {
        errno = EBADF;
        return -1;
    }

    if ((length < 0) || ((length > 0) && (buffer == NULL)))
    {
        errno = EINVAL;
        return -1;
    }

    int bytes_written = 0;
    while (bytes_written < length)
    {
        uint32_t remaining = (uint32_t)(length - bytes_written);
        uint16_t chunk_length = (uint16_t)((remaining > (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_log_header_t)))
                                               ? (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_log_header_t))
                                               : remaining);
        if (!message_log_write(MESSAGE_LOG_SUBSYSTEM_GENERAL,
                               &buffer[bytes_written],
                               chunk_length))
        {
            errno = EAGAIN;
            return (bytes_written > 0) ? bytes_written : -1;
        }

        bytes_written += chunk_length;
    }

    return bytes_written;
}
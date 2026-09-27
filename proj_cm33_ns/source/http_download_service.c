/******************************************************************************
 * File: http_download_service.c
 * Description: CM33-owned HTTP/HTTPS BASIC source download service.
 ******************************************************************************/

#include "http_download_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cy_secure_sockets.h"
#include "cy_secure_sockets_error.h"
#include "cy_wcm.h"
#include "message_log.h"
#include "message_transport.h"

#define HTTP_DOWNLOAD_STATUS_OK (0)
#define HTTP_DOWNLOAD_STATUS_ERROR (-1)
#define HTTP_DOWNLOAD_MAX_HEADER_SIZE (4096U)
#define HTTP_DOWNLOAD_MAX_CHUNK_OVERHEAD (4096U)
#define HTTP_DOWNLOAD_RESPONSE_CAPACITY (MESSAGE_HTTP_MAX_BODY_SIZE + \
                                         HTTP_DOWNLOAD_MAX_HEADER_SIZE + \
                                         HTTP_DOWNLOAD_MAX_CHUNK_OVERHEAD)
#define HTTP_DOWNLOAD_SOCKET_TIMEOUT_MS (10000U)
#define HTTP_DOWNLOAD_RECV_CHUNK_SIZE (1460U)
#define HTTP_DOWNLOAD_MAX_HOST_LENGTH (127U)

typedef struct
{
    char host[HTTP_DOWNLOAD_MAX_HOST_LENGTH + 1U];
    char path[MESSAGE_HTTP_MAX_URL_LENGTH + 2U];
    uint16_t port;
    bool use_tls;
} http_download_url_t;

static uint8_t http_download_response[HTTP_DOWNLOAD_RESPONSE_CAPACITY + 1U];
static size_t http_download_length;

static void http_progress(const char *text)
{
    size_t length = strlen(text);
    (void)message_log_write(MESSAGE_LOG_SUBSYSTEM_WIFI, text, length);
}

static void http_progress_bytes(size_t received)
{
    char text[64];
    (void)snprintf(text, sizeof(text),
                   "WIFI LOAD: received %lu response bytes.\r\n",
                   (unsigned long)received);
    http_progress(text);
}

static bool http_ascii_prefix_equal(const char *text,
                                    const char *expected,
                                    size_t length)
{
    for (size_t index = 0U; index < length; ++index)
    {
        if (toupper((unsigned char)text[index]) !=
            toupper((unsigned char)expected[index]))
        {
            return false;
        }
    }
    return true;
}

static bool http_ascii_contains(const char *text,
                                size_t text_length,
                                const char *expected)
{
    size_t expected_length = strlen(expected);
    if (expected_length > text_length)
    {
        return false;
    }
    for (size_t offset = 0U; offset <= text_length - expected_length; ++offset)
    {
        if (http_ascii_prefix_equal(&text[offset], expected, expected_length))
        {
            return true;
        }
    }
    return false;
}

static char *http_find_crlf(char *text, char *limit)
{
    for (char *cursor = text; (cursor + 1) < limit; ++cursor)
    {
        if ((cursor[0] == '\r') && (cursor[1] == '\n'))
        {
            return cursor;
        }
    }
    return NULL;
}

static bool http_parse_url(const char *url, http_download_url_t *parsed)
{
    const char *authority;
    const char *authority_end;
    const char *host_end;
    const char *port_start = NULL;
    const char *path_start;
    const char *path_end;
    size_t authority_length;
    size_t host_length;
    size_t url_length = strlen(url);

    if ((url_length >= 7U) && http_ascii_prefix_equal(url, "http://", 7U))
    {
        parsed->use_tls = false;
        parsed->port = 80U;
        authority = &url[7];
    }
    else if ((url_length >= 8U) && http_ascii_prefix_equal(url, "https://", 8U))
    {
        parsed->use_tls = true;
        parsed->port = 443U;
        authority = &url[8];
    }
    else
    {
        return false;
    }

    authority_end = authority + strcspn(authority, "/?#");
    authority_length = (size_t)(authority_end - authority);
    if ((authority_length == 0U) || (memchr(authority, '@', authority_length) != NULL))
    {
        return false;
    }

    host_end = authority_end;
    const char *colon = memchr(authority, ':', authority_length);
    if (colon != NULL)
    {
        host_end = colon;
        port_start = colon + 1;
        size_t port_length = (size_t)(authority_end - port_start);
        char port_text[6];
        if ((port_length == 0U) || (port_length >= sizeof(port_text)))
        {
            return false;
        }
        memcpy(port_text, port_start, port_length);
        port_text[port_length] = '\0';
        for (size_t index = 0U; index < port_length; ++index)
        {
            if (!isdigit((unsigned char)port_text[index]))
            {
                return false;
            }
        }
        unsigned long port = strtoul(port_text, NULL, 10);
        if ((port == 0UL) || (port > 65535UL))
        {
            return false;
        }
        parsed->port = (uint16_t)port;
    }

    host_length = (size_t)(host_end - authority);
    if ((host_length == 0U) || (host_length > HTTP_DOWNLOAD_MAX_HOST_LENGTH))
    {
        return false;
    }
    memcpy(parsed->host, authority, host_length);
    parsed->host[host_length] = '\0';

    path_start = authority_end;
    path_end = path_start + strcspn(path_start, "#");
    size_t path_length = (size_t)(path_end - path_start);
    if ((path_length == 0U) || (*path_start == '#'))
    {
        parsed->path[0] = '/';
        parsed->path[1] = '\0';
    }
    else if (*path_start == '?')
    {
        if ((path_length + 1U) >= sizeof(parsed->path))
        {
            return false;
        }
        parsed->path[0] = '/';
        memcpy(&parsed->path[1], path_start, path_length);
        parsed->path[path_length + 1U] = '\0';
    }
    else
    {
        if (path_length >= sizeof(parsed->path))
        {
            return false;
        }
        memcpy(parsed->path, path_start, path_length);
        parsed->path[path_length] = '\0';
    }
    return true;
}

static bool http_decode_chunked_body(char *body,
                                     size_t body_length,
                                     size_t *decoded_length)
{
    size_t input_offset = 0U;
    size_t output_offset = 0U;

    for (;;)
    {
        char *line_end = http_find_crlf(&body[input_offset], &body[body_length]);
        if (line_end == NULL)
        {
            return false;
        }
        size_t line_length = (size_t)(line_end - &body[input_offset]);
        size_t digits = 0U;
        size_t chunk_length = 0U;
        while ((digits < line_length) && (body[input_offset + digits] != ';'))
        {
            unsigned char character = (unsigned char)body[input_offset + digits];
            unsigned int digit;
            if ((character >= '0') && (character <= '9'))
            {
                digit = character - '0';
            }
            else if ((toupper(character) >= 'A') && (toupper(character) <= 'F'))
            {
                digit = (unsigned int)(toupper(character) - 'A') + 10U;
            }
            else
            {
                return false;
            }
            if (chunk_length > (MESSAGE_HTTP_MAX_BODY_SIZE - digit) / 16U)
            {
                return false;
            }
            chunk_length = (chunk_length * 16U) + digit;
            ++digits;
        }
        if ((digits == 0U) || (line_end + 2 > &body[body_length]))
        {
            return false;
        }
        input_offset = (size_t)(line_end - body) + 2U;
        if (chunk_length == 0U)
        {
            *decoded_length = output_offset;
            return true;
        }
        if ((chunk_length > (body_length - input_offset)) ||
            ((body_length - input_offset - chunk_length) < 2U) ||
            ((body[input_offset + chunk_length] != '\r') ||
             (body[input_offset + chunk_length + 1U] != '\n')) ||
            ((output_offset + chunk_length) > MESSAGE_HTTP_MAX_BODY_SIZE))
        {
            return false;
        }
        memmove(&body[output_offset], &body[input_offset], chunk_length);
        output_offset += chunk_length;
        input_offset += chunk_length + 2U;
    }
}

static bool http_parse_response(size_t response_size, size_t *body_length)
{
    char *response = (char *)http_download_response;
    char *response_end = &response[response_size];
    char *header_end = NULL;
    char *status_line_end;
    char *cursor;
    size_t header_length;
    bool has_content_length = false;
    bool is_chunked = false;
    size_t content_length = 0U;

    response[response_size] = '\0';
    for (char *scan = response; (scan + 3) < response_end; ++scan)
    {
        if ((scan[0] == '\r') && (scan[1] == '\n') &&
            (scan[2] == '\r') && (scan[3] == '\n'))
        {
            header_end = scan;
            break;
        }
    }
    if ((header_end == NULL) || ((size_t)(header_end - response) > HTTP_DOWNLOAD_MAX_HEADER_SIZE))
    {
        return false;
    }
    header_length = (size_t)(header_end - response);
    status_line_end = http_find_crlf(response, header_end + 2);
    if (status_line_end == NULL)
    {
        return false;
    }
    *status_line_end = '\0';
    char *status_code = strchr(response, ' ');
    if ((status_code == NULL) || (strtol(status_code + 1, NULL, 10) != 200L))
    {
        return false;
    }
    cursor = status_line_end + 2;
    while (cursor < header_end)
    {
        char *line_end = http_find_crlf(cursor, header_end + 2);
        if ((line_end == NULL) || (line_end > header_end))
        {
            return false;
        }
        size_t line_length = (size_t)(line_end - cursor);
        char *colon = memchr(cursor, ':', line_length);
        if (colon != NULL)
        {
            size_t name_length = (size_t)(colon - cursor);
            char *value = colon + 1;
            while ((*value == ' ') || (*value == '\t'))
            {
                ++value;
            }
            size_t value_length = (size_t)(line_end - value);
            if ((name_length == 14U) &&
                http_ascii_prefix_equal(cursor, "Content-Length", name_length))
            {
                char *value_end = line_end;
                while ((value_end > value) && isspace((unsigned char)value_end[-1]))
                {
                    --value_end;
                }
                char saved = *value_end;
                *value_end = '\0';
                char *number_end;
                unsigned long parsed_length = strtoul(value, &number_end, 10);
                bool valid_length = (*number_end == '\0');
                *value_end = saved;
                if (!valid_length || (parsed_length > MESSAGE_HTTP_MAX_BODY_SIZE))
                {
                    return false;
                }
                content_length = (size_t)parsed_length;
                has_content_length = true;
            }
            else if ((name_length == 17U) &&
                     http_ascii_prefix_equal(cursor, "Transfer-Encoding", name_length) &&
                     http_ascii_contains(value, value_length, "chunked"))
            {
                is_chunked = true;
            }
        }
        cursor = line_end + 2;
    }

    size_t body_offset = header_length + 4U;
    size_t available_body = response_size - body_offset;
    memmove(response, &response[body_offset], available_body);
    if (is_chunked)
    {
        return http_decode_chunked_body(response, available_body, body_length) &&
               (memchr(response, '\0', *body_length) == NULL);
    }
    if (has_content_length)
    {
        if (available_body < content_length)
        {
            return false;
        }
        available_body = content_length;
    }
    if ((available_body > MESSAGE_HTTP_MAX_BODY_SIZE) ||
        (memchr(response, '\0', available_body) != NULL))
    {
        return false;
    }
    *body_length = available_body;
    return true;
}

static bool http_download_get(const char *url)
{
    http_download_url_t parsed_url;
    cy_socket_ip_address_t ip_address;
    cy_socket_sockaddr_t address;
    cy_socket_t socket_handle = NULL;
    cy_rslt_t result;
    bool socket_created = false;
    bool socket_connected = false;
    bool success = false;
    size_t request_length;
    size_t response_size = 0U;
    char request[1200];
    char host_header[HTTP_DOWNLOAD_MAX_HOST_LENGTH + 8U];

    http_download_length = 0U;
    if (!cy_wcm_is_connected_to_ap())
    {
        http_progress("WIFI LOAD: WiFi is not connected.\r\n");
        return false;
    }
    if (!http_parse_url(url, &parsed_url))
    {
        http_progress("WIFI LOAD: URL is invalid or unsupported.\r\n");
        return false;
    }
    http_progress("WIFI LOAD: resolving server name.\r\n");
    if (CY_RSLT_SUCCESS != cy_socket_gethostbyname(parsed_url.host,
                                                    CY_SOCKET_IP_VER_V4,
                                                    &ip_address))
    {
        http_progress("WIFI LOAD: server name resolution failed.\r\n");
        return false;
    }
    http_progress("WIFI LOAD: creating network connection.\r\n");
    if (CY_RSLT_SUCCESS != cy_socket_create(CY_SOCKET_DOMAIN_AF_INET,
                                            CY_SOCKET_TYPE_STREAM,
                                            parsed_url.use_tls
                                                ? CY_SOCKET_IPPROTO_TLS
                                                : CY_SOCKET_IPPROTO_TCP,
                                            &socket_handle))
    {
        http_progress("WIFI LOAD: could not create network socket.\r\n");
        return false;
    }
    socket_created = true;

    uint32_t timeout = HTTP_DOWNLOAD_SOCKET_TIMEOUT_MS;
    if ((CY_RSLT_SUCCESS != cy_socket_setsockopt(socket_handle,
                                                 CY_SOCKET_SOL_SOCKET,
                                                 CY_SOCKET_SO_RCVTIMEO,
                                                 &timeout,
                                                 sizeof(timeout))) ||
        (CY_RSLT_SUCCESS != cy_socket_setsockopt(socket_handle,
                                                 CY_SOCKET_SOL_SOCKET,
                                                 CY_SOCKET_SO_SNDTIMEO,
                                                 &timeout,
                                                 sizeof(timeout))))
    {
        goto cleanup;
    }
    if (parsed_url.use_tls)
    {
        cy_socket_tls_auth_mode_t auth_mode = CY_SOCKET_TLS_VERIFY_NONE;
        if ((CY_RSLT_SUCCESS != cy_socket_setsockopt(socket_handle,
                                                      CY_SOCKET_SOL_TLS,
                                                      CY_SOCKET_SO_TLS_AUTH_MODE,
                                                      &auth_mode,
                                                      sizeof(auth_mode))) ||
            (CY_RSLT_SUCCESS != cy_socket_setsockopt(socket_handle,
                                                      CY_SOCKET_SOL_TLS,
                                                      CY_SOCKET_SO_SERVER_NAME_INDICATION,
                                                      parsed_url.host,
                                                      (uint32_t)strlen(parsed_url.host) + 1U)))
        {
            goto cleanup;
        }
    }

    address.ip_address = ip_address;
    address.port = parsed_url.port;
    if (CY_RSLT_SUCCESS != cy_socket_connect(socket_handle,
                                             &address,
                                             sizeof(address)))
    {
        http_progress("WIFI LOAD: server connection failed.\r\n");
        goto cleanup;
    }
    socket_connected = true;

    if (parsed_url.port == (parsed_url.use_tls ? 443U : 80U))
    {
        (void)snprintf(host_header, sizeof(host_header), "%s", parsed_url.host);
    }
    else
    {
        (void)snprintf(host_header, sizeof(host_header), "%s:%u",
                       parsed_url.host, (unsigned int)parsed_url.port);
    }
    int written = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: BASIC-WIFI/1.0\r\n"
                           "Accept: */*\r\n"
                           "Accept-Encoding: identity\r\n"
                           "Connection: close\r\n\r\n",
                           parsed_url.path, host_header);
    if ((written <= 0) || ((size_t)written >= sizeof(request)))
    {
        goto cleanup;
    }
    request_length = (size_t)written;
    for (size_t sent_total = 0U; sent_total < request_length; )
    {
        uint32_t bytes_sent = 0U;
        result = cy_socket_send(socket_handle,
                                &request[sent_total],
                                (uint32_t)(request_length - sent_total),
                                CY_SOCKET_FLAGS_NONE,
                                &bytes_sent);
        if ((result != CY_RSLT_SUCCESS) || (bytes_sent == 0U))
        {
            http_progress("WIFI LOAD: request transmission failed.\r\n");
            goto cleanup;
        }
        sent_total += bytes_sent;
    }

    http_progress("WIFI LOAD: receiving file response.\r\n");
    size_t next_progress = 8192U;
    for (;;)
    {
        uint32_t bytes_received = 0U;
        size_t remaining = HTTP_DOWNLOAD_RESPONSE_CAPACITY - response_size;
        if (remaining == 0U)
        {
            http_progress("WIFI LOAD: response exceeds the size limit.\r\n");
            goto cleanup;
        }
        uint32_t receive_length = (uint32_t)((remaining > HTTP_DOWNLOAD_RECV_CHUNK_SIZE)
                                                 ? HTTP_DOWNLOAD_RECV_CHUNK_SIZE
                                                 : remaining);
        result = cy_socket_recv(socket_handle,
                                &http_download_response[response_size],
                                receive_length,
                                CY_SOCKET_FLAGS_NONE,
                                &bytes_received);
        response_size += bytes_received;
        if (response_size >= next_progress)
        {
            http_progress_bytes(response_size);
            next_progress += 8192U;
        }
        if (result == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED)
        {
            break;
        }
        if ((result != CY_RSLT_SUCCESS) || (bytes_received == 0U))
        {
            http_progress("WIFI LOAD: response receive failed.\r\n");
            goto cleanup;
        }
    }

    success = (response_size > 0U) &&
              http_parse_response(response_size, &http_download_length);
    if (success)
    {
        char text[64];
        (void)snprintf(text, sizeof(text),
                       "WIFI LOAD: download validated (%lu bytes).\r\n",
                       (unsigned long)http_download_length);
        http_progress(text);
    }
    else
    {
        http_progress("WIFI LOAD: server response was invalid or unsupported.\r\n");
    }

cleanup:
    if (socket_connected)
    {
        (void)cy_socket_disconnect(socket_handle, HTTP_DOWNLOAD_SOCKET_TIMEOUT_MS);
    }
    if (socket_created)
    {
        (void)cy_socket_delete(socket_handle);
    }
    if (!success)
    {
        http_download_length = 0U;
    }
    return success;
}

static bool http_send_response(int32_t status,
                               uint32_t total_length,
                               const uint8_t *data,
                               uint16_t data_length)
{
    message_t *message;
    message_http_response_t response =
    {
        .status = status,
        .total_length = total_length,
        .data_length = data_length,
        .reserved = 0U
    };
    size_t payload_length = sizeof(response) + data_length;

    if ((payload_length > MESSAGE_MAX_PAYLOAD_SIZE) ||
        !message_transport_allocate(MESSAGE_OPCODE_M33_HTTP_RESPONSE,
                                     (uint16_t)payload_length,
                                     &message))
    {
        return false;
    }
    memcpy(message->payload, &response, sizeof(response));
    if (data_length > 0U)
    {
        memcpy(&message->payload[sizeof(response)], data, data_length);
    }
    if (!message_transport_send(message))
    {
        (void)message_transport_release_local(message);
        return false;
    }
    return true;
}

bool http_download_service_handle_message(message_handle_t handle,
                                          message_t *message)
{
    if ((message == NULL) || (message->opcode != MESSAGE_OPCODE_M55_HTTP_REQUEST))
    {
        return false;
    }
    if (message->payload_length < sizeof(message_http_request_t))
    {
        (void)http_send_response(HTTP_DOWNLOAD_STATUS_ERROR, 0U, NULL, 0U);
        (void)message_transport_destroy(handle);
        return true;
    }

    message_http_request_t request;
    memcpy(&request, message->payload, sizeof(request));
    if ((sizeof(request) + request.url_length > message->payload_length) ||
        (request.url_length > MESSAGE_HTTP_MAX_URL_LENGTH))
    {
        (void)http_send_response(HTTP_DOWNLOAD_STATUS_ERROR, 0U, NULL, 0U);
        (void)message_transport_destroy(handle);
        return true;
    }

    if (request.operation == MESSAGE_HTTP_BEGIN)
    {
        char url[MESSAGE_HTTP_MAX_URL_LENGTH + 1U];
        if ((request.url_length == 0U) || (request.data_length != 0U) ||
            (memchr(&message->payload[sizeof(request)], '\0', request.url_length) != NULL))
        {
            http_download_length = 0U;
            (void)http_send_response(HTTP_DOWNLOAD_STATUS_ERROR, 0U, NULL, 0U);
        }
        else
        {
            memcpy(url, &message->payload[sizeof(request)], request.url_length);
            url[request.url_length] = '\0';
            bool success = http_download_get(url);
            (void)http_send_response(success ? HTTP_DOWNLOAD_STATUS_OK
                                             : HTTP_DOWNLOAD_STATUS_ERROR,
                                     (uint32_t)http_download_length,
                                     NULL,
                                     0U);
        }
    }
    else if ((request.operation == MESSAGE_HTTP_READ) &&
             (request.url_length == 0U) &&
             (request.data_length > 0U) &&
             (request.data_length <= (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_http_response_t))) &&
             (request.offset <= http_download_length))
    {
        size_t remaining = http_download_length - request.offset;
        uint16_t data_length = (uint16_t)((remaining > request.data_length)
                                              ? request.data_length
                                              : remaining);
        (void)http_send_response(HTTP_DOWNLOAD_STATUS_OK,
                                 (uint32_t)http_download_length,
                                 &http_download_response[request.offset],
                                 data_length);
    }
    else
    {
        (void)http_send_response(HTTP_DOWNLOAD_STATUS_ERROR, 0U, NULL, 0U);
    }

    (void)message_transport_destroy(handle);
    return true;
}
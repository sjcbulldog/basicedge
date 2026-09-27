/******************************************************************************
 * File: file_service.c
 * Description: IPC-backed SD-card file service shared by CM33 and CM55.
 ******************************************************************************/

#include "file_service.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cybsp.h"
#include "event_groups.h"
#include "message_transport.h"
#include "task.h"

#define FILE_SERVICE_RESPONSE_READY (1U << 0U)
#define FILE_SERVICE_STATUS_OK (0)
#define FILE_SERVICE_STATUS_ERROR (-1)

#if CY_SYSTEM_CPU_M55
static EventGroupHandle_t file_service_events;
static int32_t file_service_status;
static uint32_t file_service_total_length;
static uint16_t file_service_data_length;
#endif
static uint8_t file_service_data[MESSAGE_MAX_PAYLOAD_SIZE];

#if CY_SYSTEM_CPU_M33
#include "FS.h"
#include "sd_card_init.h"

static FS_FILE *file_service_file;
/* emFile represents the user-visible root directory "/" as an empty path. */
static char file_service_current_directory[FILE_SERVICE_PATH_MAX] = "";
static char file_service_directory_data[FILE_SERVICE_DIRECTORY_CAPACITY];

static bool file_service_normalize_directory(const char *path,
                                             char *normalized,
                                             size_t capacity)
{
    size_t output_length = 0U;
    const char *cursor = path;

    while ('\0' != *cursor)
    {
        const char *end = strchr(cursor, '/');
        size_t component_length = (NULL != end)
                                      ? (size_t)(end - cursor)
                                      : strlen(cursor);

        if ((0U != component_length) && (0 != strncmp(cursor, ".", component_length)))
        {
            if ((2U == component_length) && (0 == strncmp(cursor, "..", 2U)))
            {
                while ((output_length > 0U) &&
                       ('/' != normalized[output_length - 1U]))
                {
                    --output_length;
                }
                if (output_length > 0U)
                {
                    --output_length;
                }
            }
            else
            {
                size_t prefix = (output_length > 0U) ? 1U : 0U;
                if ((output_length + prefix + component_length) >= capacity)
                {
                    return false;
                }
                if (prefix > 0U)
                {
                    normalized[output_length++] = '/';
                }
                memcpy(&normalized[output_length], cursor, component_length);
                output_length += component_length;
            }
        }

        if (NULL == end)
        {
            break;
        }
        cursor = end + 1;
    }

    normalized[output_length] = '\0';
    return true;
}

static bool file_service_resolve_path(const char *path,
                                      char *resolved,
                                      size_t capacity)
{
    int written;

    if ((path == NULL) || (strchr(path, '\\') != NULL))
    {
        return false;
    }

    if ('/' == path[0])
    {
        written = snprintf(resolved, capacity, "%s", path + 1);
    }
    else if ('\0' == path[0])
    {
        written = snprintf(resolved, capacity, "%s", file_service_current_directory);
    }
    else if ('\0' == file_service_current_directory[0])
    {
        written = snprintf(resolved, capacity, "%s", path);
    }
    else
    {
        written = snprintf(resolved, capacity, "%s/%s",
                           file_service_current_directory,
                           path);
    }

    if (written >= 0)
    {
        char delimiter = FS_CONF_GetDirectoryDelimiter();
        for (size_t index = 0U; index < (size_t)written; ++index)
        {
            if ('/' == resolved[index])
            {
                resolved[index] = delimiter;
            }
        }
    }

    return (written >= 0) && ((size_t)written < capacity);
}

static bool file_service_build_directory(const char *path, size_t *length)
{
    FS_FIND_DATA find_data;
    char file_name[FILE_SERVICE_PATH_MAX];
    size_t output_length = 0U;
    int result = FS_FindFirstFile(&find_data,
                                  path,
                                  file_name,
                                  (int)sizeof(file_name));

    if (1 == result)
    {
        *length = 0U;
        file_service_directory_data[0] = '\0';
        return true;
    }
    if (0 != result)
    {
        return false;
    }

    do
    {
        int written;
        if ((find_data.Attributes & FS_ATTR_DIRECTORY) != 0U)
        {
            written = snprintf(&file_service_directory_data[output_length],
                               sizeof(file_service_directory_data) - output_length,
                               "%-32s <DIR>\r\n",
                               find_data.sFileName);
        }
        else
        {
            written = snprintf(&file_service_directory_data[output_length],
                               sizeof(file_service_directory_data) - output_length,
                               "%-32s %lu\r\n",
                               find_data.sFileName,
                               (unsigned long)find_data.FileSize);
        }
        if ((written < 0) ||
            ((size_t)written >= (sizeof(file_service_directory_data) - output_length)))
        {
            FS_FindClose(&find_data);
            return false;
        }
        output_length += (size_t)written;
        result = FS_FindNextFile(&find_data);
    } while (result > 0);

    FS_FindClose(&find_data);
    if (result < 0)
    {
        return false;
    }

    *length = output_length;
    return true;
}

static bool file_service_send_response(int32_t status,
                                       uint32_t total_length,
                                       const void *data,
                                       uint16_t data_length)
{
    message_t *response;
    size_t payload_length = sizeof(message_file_response_t) + data_length;

    if (!message_transport_allocate(MESSAGE_OPCODE_M33_FILE_RESPONSE,
                                     (uint16_t)payload_length,
                                     &response))
    {
        return false;
    }

    message_file_response_t header =
    {
        .status = status,
        .total_length = total_length,
        .data_length = data_length,
        .reserved = 0U,
    };
    memcpy(response->payload, &header, sizeof(header));
    if ((data_length > 0U) && (data != NULL))
    {
        memcpy(&response->payload[sizeof(header)], data, data_length);
    }

    if (!message_transport_send(response))
    {
        (void)message_transport_release_local(response);
        return false;
    }

    return true;
}

static bool file_service_handle_request(const message_file_request_t *request,
                                        const char *path,
                                        const uint8_t *data)
{
    char resolved_path[FILE_SERVICE_PATH_MAX];

    if ((request->path_length >= FILE_SERVICE_PATH_MAX) ||
        (request->data_length > FILE_SERVICE_CHUNK_MAX) ||
        !sd_card_filesystem_ready())
    {
        return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
    }

    switch ((message_file_operation_t)request->operation)
    {
        case MESSAGE_FILE_CHANGE_DIRECTORY:
        {
            FS_FILE_INFO info;
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)) ||
                (0 != FS_GetFileInfo(resolved_path, &info)) ||
                ((info.Attributes & FS_ATTR_DIRECTORY) == 0U))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            {
                char delimiter = FS_CONF_GetDirectoryDelimiter();
                for (size_t index = 0U; resolved_path[index] != '\0'; ++index)
                {
                    if (delimiter == resolved_path[index])
                    {
                        resolved_path[index] = '/';
                    }
                }
            }
            if (!file_service_normalize_directory(resolved_path,
                                                  file_service_current_directory,
                                                  sizeof(file_service_current_directory)))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            return file_service_send_response(FILE_SERVICE_STATUS_OK, 0U, NULL, 0U);
        }

        case MESSAGE_FILE_DIRECTORY:
        {
            size_t directory_length;
            size_t offset = request->offset;
            uint16_t data_length;
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)) ||
                !file_service_build_directory(resolved_path, &directory_length) ||
                (offset > directory_length))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            data_length = (uint16_t)(((directory_length - offset > request->data_length)
                                          ? request->data_length
                                          : directory_length - offset));
            return file_service_send_response(FILE_SERVICE_STATUS_OK,
                                               (uint32_t)directory_length,
                                               &file_service_directory_data[offset],
                                               data_length);
        }

        case MESSAGE_FILE_DELETE:
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            return file_service_send_response((0 == FS_Remove(resolved_path))
                                                   ? FILE_SERVICE_STATUS_OK
                                                   : FILE_SERVICE_STATUS_ERROR,
                                               0U,
                                               NULL,
                                               0U);

        case MESSAGE_FILE_MAKE_DIRECTORY:
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            return file_service_send_response((0 == FS_MkDir(resolved_path))
                                                   ? FILE_SERVICE_STATUS_OK
                                                   : FILE_SERVICE_STATUS_ERROR,
                                               0U,
                                               NULL,
                                               0U);

        case MESSAGE_FILE_GET_DIRECTORY:
            return file_service_send_response(FILE_SERVICE_STATUS_OK,
                                               0U,
                                               ('\0' == file_service_current_directory[0])
                                                   ? "/"
                                                   : file_service_current_directory,
                                               (uint16_t)strlen(file_service_current_directory) +
                                                   (('\0' == file_service_current_directory[0]) ? 1U : 0U));

        case MESSAGE_FILE_FORMAT:
        {
            FS_DISK_INFO disk_info;
            bool valid_filesystem = (0 == FS_GetVolumeInfo("", &disk_info));
            if (valid_filesystem && (0U == request->data_length))
            {
                return file_service_send_response(FILE_SERVICE_FORMAT_CONFIRM,
                                                   0U,
                                                   NULL,
                                                   0U);
            }
            (void)FS_FormatLLIfRequired("");
            int format_status = FS_Format("", NULL);
            if (0 == format_status)
            {
                file_service_current_directory[0] = '\0';
            }
            return file_service_send_response((0 == format_status)
                                                   ? FILE_SERVICE_FORMAT_OK
                                                   : FILE_SERVICE_FORMAT_ERROR,
                                               0U,
                                               NULL,
                                               0U);
        }

        case MESSAGE_FILE_SAVE_BEGIN:
            if (file_service_file != NULL)
            {
                (void)FS_FClose(file_service_file);
                file_service_file = NULL;
            }
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            file_service_file = FS_FOpen(resolved_path, "w");
            return file_service_send_response((file_service_file != NULL)
                                                   ? FILE_SERVICE_STATUS_OK
                                                   : FILE_SERVICE_STATUS_ERROR,
                                               0U,
                                               NULL,
                                               0U);

        case MESSAGE_FILE_SAVE_CHUNK:
            if ((file_service_file == NULL) ||
                (FS_FWrite(data, 1U, request->data_length, file_service_file) != request->data_length))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            return file_service_send_response(FILE_SERVICE_STATUS_OK, 0U, NULL, 0U);

        case MESSAGE_FILE_SAVE_END:
            if (file_service_file == NULL)
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            (void)FS_FClose(file_service_file);
            file_service_file = NULL;
            return file_service_send_response(FILE_SERVICE_STATUS_OK, 0U, NULL, 0U);

        case MESSAGE_FILE_LOAD_BEGIN:
            if (file_service_file != NULL)
            {
                (void)FS_FClose(file_service_file);
                file_service_file = NULL;
            }
            if (!file_service_resolve_path(path, resolved_path, sizeof(resolved_path)))
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            file_service_file = FS_FOpen(resolved_path, "r");
            if (file_service_file == NULL)
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            return file_service_send_response(FILE_SERVICE_STATUS_OK,
                                               (uint32_t)FS_GetFileSize(file_service_file),
                                               NULL,
                                               0U);

        case MESSAGE_FILE_LOAD_CHUNK:
        {
            uint16_t bytes_read;
            if (file_service_file == NULL)
            {
                return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
            }
            (void)FS_SetFilePos(file_service_file, (I32)request->offset, FS_FILE_BEGIN);
            bytes_read = (uint16_t)FS_FRead(file_service_data, 1U,
                                            request->data_length, file_service_file);
            return file_service_send_response(FILE_SERVICE_STATUS_OK, 0U,
                                               file_service_data, bytes_read);
        }

        case MESSAGE_FILE_LOAD_END:
            if (file_service_file != NULL)
            {
                (void)FS_FClose(file_service_file);
                file_service_file = NULL;
            }
            return file_service_send_response(FILE_SERVICE_STATUS_OK, 0U, NULL, 0U);

        default:
            return file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
    }
}
#endif

bool file_service_handle_message(message_handle_t handle, message_t *message)
{
    if ((message == NULL) || (message->opcode == 0U))
    {
        return false;
    }

#if CY_SYSTEM_CPU_M33
    if ((message->opcode == MESSAGE_OPCODE_M55_FILE_REQUEST) &&
        (message->payload_length >= sizeof(message_file_request_t)))
    {
        message_file_request_t request;
        memcpy(&request, message->payload, sizeof(request));
        char path[FILE_SERVICE_PATH_MAX];
        if ((request.path_length >= sizeof(path)) ||
            (sizeof(request) + request.path_length + request.data_length > message->payload_length))
        {
            (void)file_service_send_response(FILE_SERVICE_STATUS_ERROR, 0U, NULL, 0U);
        }
        else
        {
            memcpy(path, &message->payload[sizeof(request)], request.path_length);
            path[request.path_length] = '\0';
            (void)file_service_handle_request(&request,
                                               path,
                                               &message->payload[sizeof(request) + request.path_length]);
        }
        return message_transport_destroy(handle);
    }
#else
    if (message->opcode == MESSAGE_OPCODE_M33_FILE_RESPONSE)
    {
        message_file_response_t response;
        if (message->payload_length >= sizeof(response))
        {
            memcpy(&response, message->payload, sizeof(response));
            if (response.data_length <= (message->payload_length - sizeof(response)))
            {
                file_service_status = response.status;
                file_service_total_length = response.total_length;
                file_service_data_length = response.data_length;
                memcpy(file_service_data,
                       &message->payload[sizeof(response)],
                       response.data_length);
                (void)xEventGroupSetBits(file_service_events, FILE_SERVICE_RESPONSE_READY);
            }
        }
        return message_transport_destroy(handle);
    }
#endif

    return false;
}

#if CY_SYSTEM_CPU_M55
static bool file_service_request(message_file_operation_t operation,
                                 const char *path,
                                 uint32_t offset,
                                 const void *data,
                                 uint16_t data_length)
{
    message_t *request_message;
    size_t path_length = (path != NULL) ? strlen(path) : 0U;
    size_t payload_length = sizeof(message_file_request_t) + path_length + data_length;
    message_file_request_t request =
    {
        .operation = (uint8_t)operation,
        .reserved = 0U,
        .path_length = (uint16_t)path_length,
        .offset = offset,
        .data_length = data_length,
        .reserved2 = 0U,
    };

    if ((file_service_events == NULL) || (path_length >= FILE_SERVICE_PATH_MAX) ||
        (payload_length > MESSAGE_MAX_PAYLOAD_SIZE) ||
        !message_transport_allocate(MESSAGE_OPCODE_M55_FILE_REQUEST,
                                     (uint16_t)payload_length,
                                     &request_message))
    {
        return false;
    }

    memcpy(request_message->payload, &request, sizeof(request));
    memcpy(&request_message->payload[sizeof(request)], path, path_length);
    if ((data_length > 0U) && (data != NULL))
    {
        memcpy(&request_message->payload[sizeof(request) + path_length], data, data_length);
    }
    if (!message_transport_send(request_message))
    {
        (void)message_transport_release_local(request_message);
        return false;
    }

    (void)xEventGroupWaitBits(file_service_events,
                              FILE_SERVICE_RESPONSE_READY,
                              pdTRUE,
                              pdFALSE,
                              portMAX_DELAY);
    return FILE_SERVICE_STATUS_OK == file_service_status;
}

bool file_service_save(const char *path, const char *data, size_t length)
{
    if ((file_service_events == NULL) || !file_service_request(MESSAGE_FILE_SAVE_BEGIN,
                                                                 path, 0U, NULL, 0U))
    {
        return false;
    }
    for (size_t offset = 0U; offset < length; )
    {
        uint16_t chunk = (uint16_t)((length - offset > FILE_SERVICE_CHUNK_MAX)
                                        ? FILE_SERVICE_CHUNK_MAX
                                        : (length - offset));
        if (!file_service_request(MESSAGE_FILE_SAVE_CHUNK, path, (uint32_t)offset,
                                   &data[offset], chunk))
        {
            return false;
        }
        offset += chunk;
    }
    return file_service_request(MESSAGE_FILE_SAVE_END, path, 0U, NULL, 0U);
}

bool file_service_load(const char *path, char *data, size_t capacity, size_t *length)
{
    if ((file_service_events == NULL) ||
        !file_service_request(MESSAGE_FILE_LOAD_BEGIN, path, 0U, NULL, 0U) ||
        (file_service_total_length >= capacity))
    {
        return false;
    }

    *length = file_service_total_length;
    for (size_t offset = 0U; offset < *length; )
    {
        uint16_t chunk = (uint16_t)((*length - offset > FILE_SERVICE_CHUNK_MAX)
                                        ? FILE_SERVICE_CHUNK_MAX
                                        : (*length - offset));
        if (!file_service_request(MESSAGE_FILE_LOAD_CHUNK, path, (uint32_t)offset,
                                   NULL, chunk))
        {
            return false;
        }
        if (file_service_data_length != chunk)
        {
            return false;
        }
        memcpy(&data[offset], file_service_data, chunk);
        offset += chunk;
    }
    data[*length] = '\0';
    return file_service_request(MESSAGE_FILE_LOAD_END, path, 0U, NULL, 0U);
}

bool file_service_change_directory(const char *path)
{
    return file_service_request(MESSAGE_FILE_CHANGE_DIRECTORY,
                                ((path != NULL) && ('\0' != *path)) ? path : "/",
                                0U,
                                NULL,
                                0U);
}

bool file_service_directory(char *data, size_t capacity, size_t *length)
{
    size_t total_length;

    if ((data == NULL) || (length == NULL) ||
        !file_service_request(MESSAGE_FILE_DIRECTORY, "", 0U, NULL,
                              (uint16_t)FILE_SERVICE_CHUNK_MAX))
    {
        return false;
    }

    total_length = file_service_total_length;
    if ((total_length >= capacity) || (file_service_data_length > total_length))
    {
        return false;
    }

    memcpy(data, file_service_data, file_service_data_length);
    size_t offset = file_service_data_length;
    while (offset < total_length)
    {
        uint16_t chunk = (uint16_t)(((total_length - offset > FILE_SERVICE_CHUNK_MAX)
                                         ? FILE_SERVICE_CHUNK_MAX
                                         : total_length - offset));
        if (!file_service_request(MESSAGE_FILE_DIRECTORY, "", (uint32_t)offset,
                                   NULL, chunk) ||
            (file_service_data_length != chunk))
        {
            return false;
        }
        memcpy(&data[offset], file_service_data, chunk);
        offset += chunk;
    }
    data[total_length] = '\0';
    *length = total_length;
    return true;
}

bool file_service_delete(const char *path)
{
    return file_service_request(MESSAGE_FILE_DELETE,
                                (path != NULL) ? path : "",
                                0U,
                                NULL,
                                0U);
}

bool file_service_make_directory(const char *path)
{
    return file_service_request(MESSAGE_FILE_MAKE_DIRECTORY,
                                (path != NULL) ? path : "",
                                0U,
                                NULL,
                                0U);
}

bool file_service_get_directory(char *path, size_t capacity)
{
    if ((path == NULL) || (capacity == 0U) ||
        !file_service_request(MESSAGE_FILE_GET_DIRECTORY, "", 0U, NULL, 0U) ||
        (file_service_data_length >= capacity))
    {
        return false;
    }
    memcpy(path, file_service_data, file_service_data_length);
    path[file_service_data_length] = '\0';
    return true;
}

int file_service_format(bool confirmed)
{
    return file_service_request(MESSAGE_FILE_FORMAT,
                                "",
                                0U,
                                NULL,
                                confirmed ? 1U : 0U)
               ? FILE_SERVICE_FORMAT_OK
               : ((file_service_status == FILE_SERVICE_FORMAT_CONFIRM)
                      ? FILE_SERVICE_FORMAT_CONFIRM
                      : FILE_SERVICE_FORMAT_ERROR);
}
#else
bool file_service_save(const char *path, const char *data, size_t length)
{
    (void)path;
    (void)data;
    (void)length;
    return false;
}

bool file_service_load(const char *path, char *data, size_t capacity, size_t *length)
{
    (void)path;
    (void)data;
    (void)capacity;
    (void)length;
    return false;
}

bool file_service_change_directory(const char *path)
{
    (void)path;
    return false;
}

bool file_service_directory(char *data, size_t capacity, size_t *length)
{
    (void)data;
    (void)capacity;
    (void)length;
    return false;
}

bool file_service_delete(const char *path)
{
    (void)path;
    return false;
}

bool file_service_make_directory(const char *path)
{
    (void)path;
    return false;
}

bool file_service_get_directory(char *path, size_t capacity)
{
    (void)path;
    (void)capacity;
    return false;
}

int file_service_format(bool confirmed)
{
    (void)confirmed;
    return FILE_SERVICE_FORMAT_ERROR;
}
#endif

void file_service_initialize(void)
{
#if CY_SYSTEM_CPU_M55
    file_service_events = xEventGroupCreate();
#endif
}
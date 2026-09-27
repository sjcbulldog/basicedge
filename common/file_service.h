/******************************************************************************
 * File: file_service.h
 * Description: IPC-backed SD-card file service shared by CM33 and CM55.
 ******************************************************************************/

#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "message_protocol.h"

#define FILE_SERVICE_PATH_MAX (256U)
#define FILE_SERVICE_CHUNK_MAX (MESSAGE_MAX_PAYLOAD_SIZE - sizeof(message_file_request_t) - FILE_SERVICE_PATH_MAX)
#define FILE_SERVICE_DIRECTORY_CAPACITY (8192U)

typedef enum
{
	FILE_SERVICE_FORMAT_ERROR = -1,
	FILE_SERVICE_FORMAT_OK = 0,
	FILE_SERVICE_FORMAT_CONFIRM = 1
} file_service_format_result_t;

bool file_service_handle_message(message_handle_t handle, message_t *message);
void file_service_initialize(void);
bool file_service_save(const char *path, const char *data, size_t length);
bool file_service_load(const char *path, char *data, size_t capacity, size_t *length);
bool file_service_change_directory(const char *path);
bool file_service_directory(char *data, size_t capacity, size_t *length);
bool file_service_delete(const char *path);
bool file_service_make_directory(const char *path);
bool file_service_get_directory(char *path, size_t capacity);
int file_service_format(bool confirmed);

#endif
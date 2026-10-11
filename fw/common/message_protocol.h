#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define MESSAGE_MAX_PAYLOAD_SIZE 4096U
#define MESSAGE_HTTP_MAX_BODY_SIZE (32U * 1024U)
#define MESSAGE_HTTP_MAX_URL_LENGTH (512U)

typedef enum
{
    MESSAGE_OPCODE_M55_INIT_COMPLETE = 0x0001U,
    MESSAGE_OPCODE_M33_LOG = 0x0002U,
    MESSAGE_OPCODE_M33_SD_PROBE_COMPLETE = 0x0003U,
    MESSAGE_OPCODE_M55_FILE_REQUEST = 0x0100U,
    MESSAGE_OPCODE_M33_FILE_RESPONSE = 0x0101U,
    MESSAGE_OPCODE_M55_WIFI_REQUEST = 0x0200U,
    MESSAGE_OPCODE_M33_WIFI_RESPONSE = 0x0201U,
    MESSAGE_OPCODE_M33_WIFI_READY = 0x0202U,
    MESSAGE_OPCODE_M55_HTTP_REQUEST = 0x0300U,
    MESSAGE_OPCODE_M33_HTTP_RESPONSE = 0x0301U
} message_opcode_t;

typedef enum
{
    MESSAGE_LOG_SUBSYSTEM_GENERAL = 0U,
    MESSAGE_LOG_SUBSYSTEM_SD_CARD = 1U,
    MESSAGE_LOG_SUBSYSTEM_WIFI = 2U,
    MESSAGE_LOG_SUBSYSTEM_COUNT = 3U
} message_log_subsystem_t;

typedef struct
{
    uint8_t subsystem;
    uint8_t reserved;
    uint16_t text_length;
} message_log_header_t;

typedef enum
{
    MESSAGE_HTTP_BEGIN = 1U,
    MESSAGE_HTTP_READ = 2U
} message_http_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t reserved;
    uint16_t url_length;
    uint32_t offset;
    uint16_t data_length;
    uint16_t reserved2;
} message_http_request_t;

typedef struct
{
    int32_t status;
    uint32_t total_length;
    uint16_t data_length;
    uint16_t reserved;
} message_http_response_t;

typedef enum
{
    MESSAGE_WIFI_SCAN = 1U,
    MESSAGE_WIFI_CONNECT = 2U,
    MESSAGE_WIFI_DISCONNECT = 3U,
    MESSAGE_WIFI_STATUS = 4U,
    MESSAGE_WIFI_STORED = 5U,
    MESSAGE_WIFI_CLEAR = 6U
} message_wifi_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t ssid_length;
    uint8_t password_length;
    uint8_t reserved;
} message_wifi_request_t;

typedef struct
{
    int32_t status;
    uint16_t record_count;
    uint16_t reserved;
} message_wifi_response_t;

typedef struct
{
    char ssid[34];
    int16_t signal_strength;
    uint8_t channel;
    uint8_t security;
} message_wifi_scan_record_t;

typedef enum
{
    MESSAGE_FILE_SAVE_BEGIN = 1U,
    MESSAGE_FILE_SAVE_CHUNK = 2U,
    MESSAGE_FILE_SAVE_END = 3U,
    MESSAGE_FILE_LOAD_BEGIN = 4U,
    MESSAGE_FILE_LOAD_CHUNK = 5U,
    MESSAGE_FILE_LOAD_END = 6U,
    MESSAGE_FILE_CHANGE_DIRECTORY = 7U,
    MESSAGE_FILE_DIRECTORY = 8U,
    MESSAGE_FILE_DELETE = 9U,
    MESSAGE_FILE_MAKE_DIRECTORY = 10U,
    MESSAGE_FILE_GET_DIRECTORY = 11U,
    MESSAGE_FILE_FORMAT = 12U,
    MESSAGE_FILE_SD_STATUS = 13U,
    MESSAGE_FILE_SD_FREE = 14U
} message_file_operation_t;

typedef struct
{
    uint8_t inserted;
    uint8_t initialized;
    uint16_t reserved;
    uint32_t total_mib;
    uint32_t free_kib;
    char card_type[20];
    char card_capacity[16];
    char filesystem[8];
} message_sd_card_status_t;

typedef struct
{
    uint8_t operation;
    uint8_t reserved;
    uint16_t path_length;
    uint32_t offset;
    uint16_t data_length;
    uint16_t reserved2;
} message_file_request_t;

typedef struct
{
    int32_t status;
    uint32_t total_length;
    uint16_t data_length;
    uint16_t reserved;
} message_file_response_t;

typedef struct
{
    uint16_t opcode;
    uint16_t payload_length;
    uint8_t payload[MESSAGE_MAX_PAYLOAD_SIZE];
} message_t;

typedef uint32_t message_handle_t;

_Static_assert(offsetof(message_t, payload) == 4U, "message header must be four bytes");
_Static_assert(sizeof(message_t) == (MESSAGE_MAX_PAYLOAD_SIZE + 4U),
               "message must contain a four-byte header and 4096-byte payload");

#endif
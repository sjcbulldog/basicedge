/******************************************************************************
 * File: basic_console.c
 * Description: Early-PC-style BASIC program storage and console commands.
 ******************************************************************************/

#include "basic_console.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "my_basic.h"
#include "file_service.h"
#include "wifi_service.h"

extern const char basic_trek_program[];
extern const size_t basic_trek_program_length;

#define BASIC_PROGRAM_CAPACITY (32U * 1024U)
#define BASIC_RUNTIME_CAPACITY (BASIC_PROGRAM_CAPACITY + BASIC_MAX_LINES)
#define BASIC_MAX_LINES        (640U)
#define BASIC_STORED_LINE_SIZE (272U)
#define BASIC_MIN_LINE_NUMBER  (1UL)
#define BASIC_MAX_LINE_NUMBER  (65535UL)
#define BASIC_RENUM_START      (10UL)
#define BASIC_RENUM_STEP       (10UL)

typedef struct
{
    uint32_t old_number;
    uint32_t new_number;
} basic_line_map_t;

static char basic_program[BASIC_PROGRAM_CAPACITY];
static char translated_program[BASIC_RUNTIME_CAPACITY];
static char directory_listing[FILE_SERVICE_DIRECTORY_CAPACITY];
static size_t basic_program_length;

static const char *skip_spaces(const char *text)
{
    while (isspace((unsigned char)*text))
    {
        ++text;
    }

    return text;
}

static bool command_match(const char *input,
                          const char *command,
                          const char **arguments)
{
    input = skip_spaces(input);
    while ((*input != '\0') && (*command != '\0') &&
           (toupper((unsigned char)*input) == (unsigned char)*command))
    {
        ++input;
        ++command;
    }

    if ((*command != '\0') || ((*input != '\0') && !isspace((unsigned char)*input)))
    {
        return false;
    }

    if (NULL != arguments)
    {
        *arguments = skip_spaces(input);
    }

    return true;
}

static bool parse_line_number(const char *text,
                              uint32_t *line_number,
                              const char **remainder)
{
    char *end;
    unsigned long number;

    text = skip_spaces(text);
    if (!isdigit((unsigned char)*text))
    {
        return false;
    }

    number = strtoul(text, &end, 10);
    if ((number < BASIC_MIN_LINE_NUMBER) || (number > BASIC_MAX_LINE_NUMBER))
    {
        return false;
    }

    *line_number = (uint32_t)number;
    *remainder = skip_spaces(end);
    return true;
}

static size_t find_line(uint32_t line_number, bool *found)
{
    size_t offset = 0U;

    *found = false;
    while (offset < basic_program_length)
    {
        uint32_t stored_number;
        const char *remainder;
        (void)parse_line_number(&basic_program[offset],
                                &stored_number,
                                &remainder);
        if (stored_number >= line_number)
        {
            *found = (stored_number == line_number);
            break;
        }

        const char *line_end = strchr(&basic_program[offset], '\n');
        if (NULL == line_end)
        {
            break;
        }
        offset = (size_t)(line_end - basic_program) + 1U;
    }

    return offset;
}

static void store_program_line(const char *input)
{
    uint32_t line_number;
    const char *body;
    bool found;
    size_t offset;
    size_t old_length = 0U;

    if (!parse_line_number(input, &line_number, &body))
    {
        printf("Invalid line number.\r\n");
        return;
    }

    offset = find_line(line_number, &found);
    if (found)
    {
        const char *line_end = strchr(&basic_program[offset], '\n');
        old_length = (NULL != line_end)
                         ? (size_t)(line_end - &basic_program[offset]) + 1U
                         : basic_program_length - offset;
    }

    if ('\0' == *body)
    {
        if (found)
        {
            memmove(&basic_program[offset],
                    &basic_program[offset + old_length],
                    basic_program_length - offset - old_length + 1U);
            basic_program_length -= old_length;
        }
        return;
    }

    char stored_line[BASIC_STORED_LINE_SIZE];
    int written = snprintf(stored_line,
                           sizeof(stored_line),
                           "%lu %s\n",
                           (unsigned long)line_number,
                           body);
    if ((written < 0) || ((size_t)written >= sizeof(stored_line)))
    {
        printf("Program line is too long.\r\n");
        return;
    }

    size_t new_length = (size_t)written;
    if ((basic_program_length - old_length + new_length) >= sizeof(basic_program))
    {
        printf("Out of BASIC program memory.\r\n");
        return;
    }

    memmove(&basic_program[offset + new_length],
            &basic_program[offset + old_length],
            basic_program_length - offset - old_length + 1U);
    memcpy(&basic_program[offset], stored_line, new_length);
    basic_program_length = basic_program_length - old_length + new_length;
}

static bool append_text(char *destination,
                        size_t capacity,
                        size_t *length,
                        const char *text,
                        size_t text_length)
{
    if ((*length + text_length) >= capacity)
    {
        return false;
    }

    memcpy(&destination[*length], text, text_length);
    *length += text_length;
    destination[*length] = '\0';
    return true;
}

static bool matches_case_insensitive_name(const char *text,
                                         size_t text_length,
                                         const char *name)
{
    size_t length = strlen(name);

    if (text_length != length)
    {
        return false;
    }

    for (size_t index = 0U; index < length; ++index)
    {
        unsigned char actual = (unsigned char)text[index];
        unsigned char expected = (unsigned char)name[index];
        if (toupper(actual) != toupper(expected))
        {
            return false;
        }
    }

    return true;
}

static bool parse_filename(struct mb_interpreter_t *interpreter,
                           const char *arguments,
                           char *filename,
                           size_t filename_capacity)
{
    const char *cursor = skip_spaces(arguments);
    const char *end;
    size_t length;

    if ('"' == *cursor)
    {
        ++cursor;
        end = strchr(cursor, '"');
        if (NULL == end)
        {
            return false;
        }

        length = (size_t)(end - cursor);
        if ((0U == length) || (length >= filename_capacity) ||
            (*skip_spaces(end + 1) != '\0'))
        {
            return false;
        }

        memcpy(filename, cursor, length);
        filename[length] = '\0';
    }
    else
    {
        mb_value_t value;
        char variable_name[32];

        end = cursor;
        while (isalnum((unsigned char)*end) || ('_' == *end) || ('$' == *end))
        {
            ++end;
        }
        length = (size_t)(end - cursor);
        if ((0U == length) || (length >= sizeof(variable_name)) ||
            (*skip_spaces(end) != '\0'))
        {
            return false;
        }

        memcpy(variable_name, cursor, length);
        variable_name[length] = '\0';
        (void)mb_get_value_by_name(interpreter, NULL, variable_name, &value);
        if ((MB_DT_STRING != value.type) || (NULL == value.value.string) ||
            (strlen(value.value.string) >= filename_capacity))
        {
            return false;
        }
        (void)snprintf(filename, filename_capacity, "%s", value.value.string);
        length = strlen(filename);
    }

    for (size_t index = 0U; index < length; ++index)
    {
        if ('\\' == cursor[index])
        {
            return false;
        }
    }

    return true;
}

static bool parse_wifi_string(const char **cursor,
                              char *value,
                              size_t capacity)
{
    const char *start;
    const char *end;
    size_t length;

    *cursor = skip_spaces(*cursor);
    if (**cursor != '"')
    {
        return false;
    }
    start = ++(*cursor);
    end = strchr(start, '"');
    if (end == NULL)
    {
        return false;
    }
    length = (size_t)(end - start);
    if ((length == 0U) || (length >= capacity))
    {
        return false;
    }
    memcpy(value, start, length);
    value[length] = '\0';
    *cursor = end + 1;
    return true;
}

static bool parse_wifi_connect_arguments(const char *arguments,
                                         char *ssid,
                                         size_t ssid_capacity,
                                         char *password,
                                         size_t password_capacity)
{
    const char *cursor = arguments;
    if (!parse_wifi_string(&cursor, ssid, ssid_capacity) ||
        !parse_wifi_string(&cursor, password, password_capacity))
    {
        return false;
    }
    return *skip_spaces(cursor) == '\0';
}

static void list_program(const char *arguments)
{
    uint32_t first = BASIC_MIN_LINE_NUMBER;
    uint32_t last = BASIC_MAX_LINE_NUMBER;
    const char *cursor = skip_spaces(arguments);
    char *end;

    if ('\0' != *cursor)
    {
        unsigned long parsed = strtoul(cursor, &end, 10);
        if ((end == cursor) || (parsed > BASIC_MAX_LINE_NUMBER))
        {
            printf("Usage: LIST [line|first-last]\r\n");
            return;
        }
        first = (uint32_t)parsed;
        last = first;
        end = (char *)skip_spaces(end);
        if ('-' == *end)
        {
            const char *range_start = skip_spaces(end + 1);
            parsed = strtoul(range_start, &end, 10);
            if ((end == range_start) ||
                (parsed > BASIC_MAX_LINE_NUMBER))
            {
                printf("Usage: LIST [line|first-last]\r\n");
                return;
            }
            last = (uint32_t)parsed;
            if (first > last)
            {
                printf("Usage: LIST [line|first-last]\r\n");
                return;
            }
        }
        if (*skip_spaces(end) != '\0')
        {
            printf("Usage: LIST [line|first-last]\r\n");
            return;
        }
    }

    size_t offset = 0U;
    bool listed = false;
    while (offset < basic_program_length)
    {
        uint32_t line_number;
        const char *body;
        if (!parse_line_number(&basic_program[offset], &line_number, &body))
        {
            printf("Malformed BASIC program text.\r\n");
            return;
        }
        if ((line_number >= first) && (line_number <= last))
        {
            const char *line_end = strchr(&basic_program[offset], '\n');
            size_t line_length = (NULL != line_end)
                                     ? (size_t)(line_end - &basic_program[offset]) + 1U
                                     : basic_program_length - offset;
            (void)fwrite(&basic_program[offset], 1U, line_length, stdout);
            listed = true;
        }
        const char *line_end = strchr(&basic_program[offset], '\n');
        offset = (NULL != line_end)
                     ? (size_t)(line_end - basic_program) + 1U
                     : basic_program_length;
    }
    if (!listed && (0U == basic_program_length))
    {
        printf("No BASIC program is stored.\r\n");
    }
}

static uint32_t mapped_line_number(const basic_line_map_t *map,
                                   size_t count,
                                   uint32_t old_number)
{
    for (size_t index = 0U; index < count; ++index)
    {
        if (map[index].old_number == old_number)
        {
            return map[index].new_number;
        }
    }

    return old_number;
}

static bool is_reference_keyword(const char *token, size_t length)
{
    static const char *const keywords[] =
    {
        "GOTO", "GOSUB", "THEN", "ELSE", "RESTORE", "RUN"
    };

    for (size_t index = 0U; index < (sizeof(keywords) / sizeof(keywords[0])); ++index)
    {
        if ((strlen(keywords[index]) == length) &&
            (0 == strncmp(token, keywords[index], length)))
        {
            return true;
        }
    }

    return false;
}

static bool append_translated_body(char *destination,
                                   size_t capacity,
                                   const char *body,
                                   const basic_line_map_t *map,
                                   size_t map_count,
                                   bool label_references,
                                   size_t *output_length)
{
    bool in_string = false;
    bool expect_reference = false;

    while ((*body != '\0') && (*body != '\n'))
    {
        if ('"' == *body)
        {
            in_string = !in_string;
            if (!append_text(destination, capacity, output_length, body++, 1U))
            {
                return false;
            }
            continue;
        }

        if (!in_string && isalpha((unsigned char)*body))
        {
            const char *token = body;
            char uppercase[16];
            size_t token_length = 0U;
            while (isalnum((unsigned char)*body) || (*body == '$'))
            {
                if (token_length < (sizeof(uppercase) - 1U))
                {
                    uppercase[token_length] = (char)toupper((unsigned char)*body);
                }
                ++token_length;
                ++body;
            }
            uppercase[(token_length < sizeof(uppercase))
                          ? token_length
                          : (sizeof(uppercase) - 1U)] = '\0';

            if (!append_text(destination, capacity, output_length, token, token_length))
            {
                return false;
            }
            if ((3U == token_length) && (0 == strcmp(uppercase, "REM")))
            {
                size_t remainder_length = strcspn(body, "\n");
                return append_text(destination,
                                   capacity,
                                   output_length,
                                   body,
                                   remainder_length) &&
                       append_text(destination,
                                   capacity,
                                   output_length,
                                   "\n",
                                   1U);
            }
            expect_reference = is_reference_keyword(uppercase, token_length);
            continue;
        }

        if (!in_string && expect_reference && isdigit((unsigned char)*body))
        {
            char *end;
            uint32_t old_number = (uint32_t)strtoul(body, &end, 10);
            char number[16];
            int written = snprintf(number,
                                   sizeof(number),
                                   label_references ? "L%lu" : "%lu",
                                   (unsigned long)mapped_line_number(map,
                                                                    map_count,
                                                                    old_number));
            if ((written < 0) ||
                !append_text(destination,
                             capacity,
                             output_length,
                             number,
                             (size_t)written))
            {
                return false;
            }
            body = end;
            continue;
        }

        if (!isspace((unsigned char)*body) && (*body != ','))
        {
            expect_reference = false;
        }
        if (!append_text(destination, capacity, output_length, body++, 1U))
        {
            return false;
        }
    }

    return append_text(destination, capacity, output_length, "\n", 1U);
}

static void renumber_program(const char *arguments)
{
    basic_line_map_t map[BASIC_MAX_LINES];
    uint32_t start = BASIC_RENUM_START;
    uint32_t step = BASIC_RENUM_STEP;
    size_t map_count = 0U;
    size_t offset = 0U;

    if ('\0' != *arguments)
    {
        char *end;
        start = (uint32_t)strtoul(arguments, &end, 10);
        end = (char *)skip_spaces(end);
        if (',' == *end)
        {
            step = (uint32_t)strtoul(skip_spaces(end + 1), &end, 10);
        }
        if ((start < BASIC_MIN_LINE_NUMBER) ||
            (step < BASIC_MIN_LINE_NUMBER) || (*skip_spaces(end) != '\0'))
        {
            printf("Usage: RENUM [start[,step]]\r\n");
            return;
        }
    }

    while ((offset < basic_program_length) && (map_count < BASIC_MAX_LINES))
    {
        const char *body;
        if (!parse_line_number(&basic_program[offset],
                               &map[map_count].old_number,
                               &body))
        {
            printf("Cannot renumber malformed program text.\r\n");
            return;
        }
        uint32_t new_number = start + ((uint32_t)map_count * step);
        if (new_number > BASIC_MAX_LINE_NUMBER)
        {
            printf("RENUM exceeds maximum line number.\r\n");
            return;
        }
        map[map_count++].new_number = new_number;

        const char *line_end = strchr(&basic_program[offset], '\n');
        offset = (NULL != line_end)
                     ? (size_t)(line_end - basic_program) + 1U
                     : basic_program_length;
    }

    if (offset < basic_program_length)
    {
        printf("Too many BASIC program lines.\r\n");
        return;
    }

    size_t output_length = 0U;
    offset = 0U;
    for (size_t index = 0U; index < map_count; ++index)
    {
        const char *body;
        uint32_t ignored_number;
        char number[16];
        int written;

        (void)parse_line_number(&basic_program[offset], &ignored_number, &body);
        written = snprintf(number,
                           sizeof(number),
                           "%lu ",
                           (unsigned long)map[index].new_number);
        if ((written < 0) ||
            !append_text(translated_program,
                         sizeof(translated_program),
                         &output_length,
                         number,
                         (size_t)written) ||
            !append_translated_body(translated_program,
                                    sizeof(translated_program),
                                    body,
                                    map,
                                    map_count,
                                    false,
                                    &output_length))
        {
            printf("Renumbered program exceeds available memory.\r\n");
            return;
        }

        const char *line_end = strchr(&basic_program[offset], '\n');
        offset = (NULL != line_end)
                     ? (size_t)(line_end - basic_program) + 1U
                     : basic_program_length;
    }

    memcpy(basic_program, translated_program, output_length + 1U);
    basic_program_length = output_length;
    printf("Program renumbered.\r\n");
}

static bool load_builtin_program(const char *argument,
                                const char *program_name)
{
    const char *cursor = skip_spaces(argument);
    size_t length;

    basic_console_initialize();

    if ('"' == *cursor)
    {
        const char *name = ++cursor;
        const char *end = name;
        while ((*end != '\0') && ('"' != *end))
        {
            ++end;
        }
        if ('"' != *end)
        {
            return false;
        }

        size_t name_length = (size_t)(end - name);
        if (matches_case_insensitive_name(name, name_length, program_name))
        {
            length = basic_trek_program_length;
            if (length >= sizeof(basic_program))
            {
                printf("Trek program is too large to load.\r\n");
                return true;
            }

            memcpy(basic_program, basic_trek_program, length);
            basic_program_length = length;
            basic_program[basic_program_length] = '\0';
            return true;
        }

        printf("Unknown program: %.*s\r\n", (int)name_length, name);
        return true;
    }

    if ('\0' == *cursor)
    {
        length = basic_trek_program_length;
        if (length >= sizeof(basic_program))
        {
            printf("Trek program is too large to load.\r\n");
            return true;
        }

        memcpy(basic_program, basic_trek_program, length);
        basic_program_length = length;
        basic_program[basic_program_length] = '\0';
        return true;
    }

    return false;
}

static bool build_runtime_program(void)
{
    size_t input_offset = 0U;
    size_t output_length = 0U;

    translated_program[0] = '\0';
    while (input_offset < basic_program_length)
    {
        const char *body;
        uint32_t line_number;
        char label[16];
        int written;

        if (!parse_line_number(&basic_program[input_offset], &line_number, &body))
        {
            return false;
        }

        written = snprintf(label,
                           sizeof(label),
                           "L%lu: ",
                           (unsigned long)line_number);
        if ((written < 0) ||
            !append_text(translated_program,
                         sizeof(translated_program),
                         &output_length,
                         label,
                         (size_t)written) ||
            !append_translated_body(translated_program,
                                    sizeof(translated_program),
                                    body,
                                    NULL,
                                    0U,
                                    true,
                                    &output_length))
        {
            return false;
        }

        const char *line_end = strchr(&basic_program[input_offset], '\n');
        input_offset = (NULL != line_end)
                           ? (size_t)(line_end - basic_program) + 1U
                           : basic_program_length;
    }

    return true;
}

void basic_console_initialize(void)
{
    basic_program[0] = '\0';
    basic_program_length = 0U;
}

bool basic_console_process(struct mb_interpreter_t **interpreter,
                           const char *input,
                           int *status)
{
    const char *arguments;
    uint32_t line_number;

    *status = MB_FUNC_OK;

    if (parse_line_number(input, &line_number, &arguments))
    {
        store_program_line(input);
        return true;
    }
    if (command_match(input, "LIST", &arguments))
    {
        list_program(arguments);
        return true;
    }
    if (command_match(input, "DIR", &arguments))
    {
        size_t listing_length;
        if ('\0' != *arguments)
        {
            printf("Usage: DIR\r\n");
            return true;
        }
        if (file_service_directory(directory_listing,
                                   sizeof(directory_listing),
                                   &listing_length))
        {
            (void)fwrite(directory_listing, 1U, listing_length, stdout);
        }
        else
        {
            printf("Unable to read current directory.\r\n");
        }
        return true;
    }
    if (command_match(input, "PWD", &arguments))
    {
        char directory[FILE_SERVICE_PATH_MAX];
        if ('\0' != *arguments)
        {
            printf("Usage: PWD\r\n");
        }
        else if (file_service_get_directory(directory, sizeof(directory)))
        {
            printf("%s\r\n", directory);
        }
        else
        {
            printf("Unable to read current directory.\r\n");
        }
        return true;
    }
    if (command_match(input, "FORMAT", &arguments))
    {
        int format_result;
        if ('\0' == *arguments)
        {
            format_result = file_service_format(false);
            if (FILE_SERVICE_FORMAT_CONFIRM == format_result)
            {
                printf("A valid filesystem exists. Type FORMAT YES to erase it.\r\n");
            }
            else if (FILE_SERVICE_FORMAT_OK == format_result)
            {
                printf("SD card formatted.\r\n");
            }
            else
            {
                printf("Unable to format SD card.\r\n");
            }
        }
        else if (matches_case_insensitive_name(arguments, strlen(arguments), "YES"))
        {
            printf("Formatting, please wait .... ");
            (void)fflush(stdout);
            format_result = file_service_format(true);
            if (FILE_SERVICE_FORMAT_OK == format_result)
            {
                printf("complete\r\n");
            }
            else
            {
                printf("\r\nUnable to format SD card.\r\n");
            }
        }
        else
        {
            printf("Usage: FORMAT [YES]\r\n");
        }
        return true;
    }
    if (command_match(input, "CD", &arguments))
    {
        char directory[FILE_SERVICE_PATH_MAX] = "";
        if (('\0' != *arguments) &&
            !parse_filename(*interpreter, arguments, directory, sizeof(directory)))
        {
            printf("Usage: CD [\"path\"]\r\n");
            return true;
        }
        if (file_service_change_directory(directory))
        {
            printf("Directory changed.\r\n");
        }
        else
        {
            printf("Directory not found.\r\n");
        }
        return true;
    }
    if (command_match(input, "DEL", &arguments))
    {
        char filename[FILE_SERVICE_PATH_MAX];
        if (!parse_filename(*interpreter, arguments, filename, sizeof(filename)))
        {
            printf("Usage: DEL \"filename\"\r\n");
            return true;
        }
        if (file_service_delete(filename))
        {
            printf("Deleted file: %s\r\n", filename);
        }
        else
        {
            printf("Unable to delete \"%s\".\r\n", filename);
        }
        return true;
    }
    if (command_match(input, "MKDIR", &arguments))
    {
        char directory[FILE_SERVICE_PATH_MAX];
        if (!parse_filename(*interpreter, arguments, directory, sizeof(directory)))
        {
            printf("Usage: MKDIR \"path\"\r\n");
            return true;
        }
        if (file_service_make_directory(directory))
        {
            printf("Created directory: %s\r\n", directory);
        }
        else
        {
            printf("Unable to create directory \"%s\".\r\n", directory);
        }
        return true;
    }
    if (command_match(input, "RUN", &arguments))
    {
        if (0U == basic_program_length)
        {
            printf("No BASIC program is stored.\r\n");
            return true;
        }
        if (!build_runtime_program())
        {
            printf("Program is too large to run.\r\n");
            *status = MB_FUNC_ERR;
            return true;
        }
        *status = mb_reset(interpreter, false, true);
        if (MB_FUNC_OK == *status)
        {
            *status = mb_load_string(*interpreter, translated_program, true);
        }
        if (MB_FUNC_OK == *status)
        {
            *status = mb_run(*interpreter, true);
        }
        return true;
    }
    if (command_match(input, "WIFI", &arguments))
    {
        const char *wifi_arguments;
        if (command_match(arguments, "SCAN", &wifi_arguments) &&
            (*wifi_arguments == '\0'))
        {
            message_wifi_scan_record_t records[WIFI_SERVICE_MAX_SCAN_RECORDS];
            size_t record_count;
            if (!wifi_service_scan(records,
                                   WIFI_SERVICE_MAX_SCAN_RECORDS,
                                   &record_count))
            {
                printf("WIFI scan unavailable while connected or not ready.\r\n");
                return true;
            }
            printf("%-32s  %8s  %3s\r\n", "SSID", "RSSI dBm", "CH");
            for (size_t index = 0U; index < record_count; ++index)
            {
                printf("%-32.32s  %8d  %3u\r\n",
                       records[index].ssid,
                       records[index].signal_strength,
                       (unsigned int)records[index].channel);
            }
            printf("%u network(s) found.\r\n", (unsigned int)record_count);
            return true;
        }
        if (command_match(arguments, "DISCONNECT", &wifi_arguments) &&
            (*wifi_arguments == '\0'))
        {
            printf(wifi_service_disconnect()
                       ? "WIFI disconnected.\r\n"
                       : "WIFI disconnect failed.\r\n");
            return true;
        }
        if (command_match(arguments, "CONNECT", &wifi_arguments))
        {
            char ssid[33];
            char password[65];
            if (!parse_wifi_connect_arguments(wifi_arguments,
                                              ssid,
                                              sizeof(ssid),
                                              password,
                                              sizeof(password)))
            {
                printf("Usage: WIFI CONNECT \"SSID\" \"PASSWORD\"\r\n");
                return true;
            }
            bool credentials_saved = false;
            bool connected = wifi_service_connect(ssid, password, &credentials_saved);
            if (!connected)
            {
                printf("WIFI connection failed.\r\n");
            }
            else
            {
                printf(credentials_saved
                           ? "WIFI connected; credentials saved.\r\n"
                           : "WIFI connected; credentials not saved.\r\n");
            }
            return true;
        }
        if (command_match(arguments, "LOAD", &wifi_arguments))
        {
            char url[MESSAGE_HTTP_MAX_URL_LENGTH + 1U];
            size_t downloaded_length;
            if (!parse_filename(*interpreter,
                                wifi_arguments,
                                url,
                                sizeof(url)))
            {
                printf("Usage: WIFI LOAD \"URL\"\r\n");
                return true;
            }
            printf("WIFI LOAD: starting download.\r\n");
            (void)fflush(stdout);
            if (!wifi_service_download(url,
                                       (uint8_t *)translated_program,
                                       BASIC_PROGRAM_CAPACITY - 1U,
                                       &downloaded_length))
            {
                printf("Unable to download BASIC program from URL.\r\n");
                return true;
            }
            memcpy(basic_program, translated_program, downloaded_length);
            basic_program[downloaded_length] = '\0';
            basic_program_length = downloaded_length;
            printf("Loaded program: %s\r\n", url);
            return true;
        }
        printf("Usage: WIFI SCAN | WIFI CONNECT \"SSID\" \"PASSWORD\" | WIFI DISCONNECT | WIFI LOAD \"URL\"\r\n");
        return true;
    }
    if (command_match(input, "NEW", &arguments))
    {
        basic_console_initialize();
        *status = mb_reset(interpreter, false, true);
        printf("New program.\r\n");
        return true;
    }
    if (command_match(input, "CLEAR", &arguments))
    {
        *status = mb_reset(interpreter, false, true);
        printf("Variables cleared.\r\n");
        return true;
    }
    if (command_match(input, "RENUM", &arguments))
    {
        renumber_program(arguments);
        return true;
    }
    if (command_match(input, "LOAD", &arguments))
    {
        char filename[FILE_SERVICE_PATH_MAX];
        if (!parse_filename(*interpreter, arguments, filename, sizeof(filename)))
        {
            printf("Usage: LOAD \"filename\"\r\n");
            return true;
        }

        if (matches_case_insensitive_name(filename, strlen(filename), "TREK"))
        {
            (void)load_builtin_program("\"TREK\"", "TREK");
            printf("Loaded program: TREK\r\n");
            return true;
        }

        size_t loaded_length;
        basic_console_initialize();
        if (!file_service_load(filename,
                               basic_program,
                               sizeof(basic_program),
                               &loaded_length))
        {
            printf("Unable to load \"%s\".\r\n", filename);
            return true;
        }
        basic_program_length = loaded_length;
        printf("Loaded program: %s\r\n", filename);
        return true;
    }
    if (command_match(input, "SAVE", &arguments))
    {
        char filename[FILE_SERVICE_PATH_MAX];
        if (!parse_filename(*interpreter, arguments, filename, sizeof(filename)))
        {
            printf("Usage: SAVE \"filename\"\r\n");
            return true;
        }
        if (file_service_save(filename, basic_program, basic_program_length))
        {
            printf("Saved program: %s\r\n", filename);
        }
        else
        {
            printf("Unable to save \"%s\".\r\n", filename);
        }
        return true;
    }
    if (command_match(input, "HELP", &arguments))
    {
        printf("Commands: CLS, DIR, PWD, CD [\"path\"], MKDIR \"path\", DEL \"filename\", FORMAT [YES], LIST [line|first-last], RUN, WIFI, LOAD \"filename\", SAVE \"filename\", NEW, CLEAR, RENUM [start[,step]], HELP\r\n");
        printf("Enter a numbered line to store it; enter its number alone to delete it.\r\n");
        return true;
    }
    if (command_match(input, "CLS", &arguments))
    {
        printf("\x1b[2J\x1b[;H");
        (void)fflush(stdout);
        return true;
    }

    return false;
}
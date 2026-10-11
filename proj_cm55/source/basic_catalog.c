/******************************************************************************
 * File: basic_catalog.c
 * Description: Bounded parser for the remote BASIC program catalog.
 ******************************************************************************/
#include "basic_catalog.h"

#include <ctype.h>
#include <string.h>

#define CATALOG_MAX_DEPTH (16U)

typedef struct
{
    const char *cursor;
    const char *end;
    unsigned int depth;
} catalog_parser_t;

static void skip_whitespace(catalog_parser_t *parser)
{
    while ((parser->cursor < parser->end) &&
           ((*parser->cursor == ' ') || (*parser->cursor == '\t') ||
            (*parser->cursor == '\r') || (*parser->cursor == '\n')))
    {
        ++parser->cursor;
    }
}

static bool consume(catalog_parser_t *parser, char expected)
{
    skip_whitespace(parser);
    if ((parser->cursor >= parser->end) || (*parser->cursor != expected))
    {
        return false;
    }
    ++parser->cursor;
    return true;
}

static bool peek(catalog_parser_t *parser, char expected)
{
    skip_whitespace(parser);
    return (parser->cursor < parser->end) && (*parser->cursor == expected);
}

static int hex_value(char value)
{
    if ((value >= '0') && (value <= '9')) return value - '0';
    if ((value >= 'a') && (value <= 'f')) return value - 'a' + 10;
    if ((value >= 'A') && (value <= 'F')) return value - 'A' + 10;
    return -1;
}

/* Copies a JSON string when output is non-NULL; otherwise validates and skips it. */
static bool parse_string(catalog_parser_t *parser, char *output, size_t capacity)
{
    size_t length = 0U;
    if (!consume(parser, '"')) return false;
    while (parser->cursor < parser->end)
    {
        char value = *parser->cursor++;
        if (value == '"')
        {
            if (output != NULL) output[length] = '\0';
            return true;
        }
        if ((unsigned char)value < 0x20U) return false;
        if (value == '\\')
        {
            if (parser->cursor >= parser->end) return false;
            value = *parser->cursor++;
            switch (value)
            {
                case '"': case '\\': case '/': break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                case 'u':
                {
                    unsigned int code = 0U;
                    if ((size_t)(parser->end - parser->cursor) < 4U) return false;
                    for (unsigned int index = 0U; index < 4U; ++index)
                    {
                        int digit = hex_value(*parser->cursor++);
                        if (digit < 0) return false;
                        code = (code << 4U) | (unsigned int)digit;
                    }
                    /* The serial console stores catalog text as bytes; escaped Unicode is limited to ASCII. */
                    if ((code < 0x20U) || (code > 0x7eU)) return false;
                    value = (char)code;
                    break;
                }
                default:
                    return false;
            }
        }
        if (output != NULL)
        {
            if (length + 1U >= capacity) return false;
            output[length++] = value;
        }
    }
    return false;
}

static bool parse_size(catalog_parser_t *parser, uint32_t *size)
{
    uint32_t value = 0U;
    skip_whitespace(parser);
    if ((parser->cursor >= parser->end) || !isdigit((unsigned char)*parser->cursor))
    {
        return false;
    }
    if ((*parser->cursor == '0') && (parser->cursor + 1 < parser->end) &&
        isdigit((unsigned char)parser->cursor[1]))
    {
        return false;
    }
    while ((parser->cursor < parser->end) && isdigit((unsigned char)*parser->cursor))
    {
        uint32_t digit = (uint32_t)(*parser->cursor++ - '0');
        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = (value * 10U) + digit;
    }
    if ((parser->cursor < parser->end) &&
        ((*parser->cursor == '.') || (*parser->cursor == 'e') || (*parser->cursor == 'E')))
    {
        return false;
    }
    *size = value;
    return true;
}

static bool skip_digits(catalog_parser_t *parser)
{
    const char *start = parser->cursor;
    while ((parser->cursor < parser->end) && isdigit((unsigned char)*parser->cursor))
    {
        ++parser->cursor;
    }
    return parser->cursor != start;
}

static bool skip_number(catalog_parser_t *parser)
{
    if ((parser->cursor < parser->end) && (*parser->cursor == '-')) ++parser->cursor;
    if ((parser->cursor < parser->end) && (*parser->cursor == '0'))
    {
        ++parser->cursor;
    }
    else if (!skip_digits(parser))
    {
        return false;
    }
    if ((parser->cursor < parser->end) && (*parser->cursor == '.'))
    {
        ++parser->cursor;
        if (!skip_digits(parser)) return false;
    }
    if ((parser->cursor < parser->end) &&
        ((*parser->cursor == 'e') || (*parser->cursor == 'E')))
    {
        ++parser->cursor;
        if ((parser->cursor < parser->end) &&
            ((*parser->cursor == '+') || (*parser->cursor == '-')))
        {
            ++parser->cursor;
        }
        if (!skip_digits(parser)) return false;
    }
    return true;
}

static bool skip_literal(catalog_parser_t *parser, const char *word)
{
    size_t length = strlen(word);
    if (((size_t)(parser->end - parser->cursor) < length) ||
        (memcmp(parser->cursor, word, length) != 0))
    {
        return false;
    }
    parser->cursor += length;
    return true;
}

static bool skip_value(catalog_parser_t *parser);

static bool skip_container(catalog_parser_t *parser, char close, bool object)
{
    if (++parser->depth > CATALOG_MAX_DEPTH) return false;
    ++parser->cursor;
    if (consume(parser, close))
    {
        --parser->depth;
        return true;
    }
    do
    {
        if (object && (!parse_string(parser, NULL, 0U) || !consume(parser, ':')))
        {
            return false;
        }
        if (!skip_value(parser)) return false;
    } while (consume(parser, ','));
    if (!consume(parser, close)) return false;
    --parser->depth;
    return true;
}

static bool skip_value(catalog_parser_t *parser)
{
    skip_whitespace(parser);
    if (parser->cursor >= parser->end) return false;
    switch (*parser->cursor)
    {
        case '"': return parse_string(parser, NULL, 0U);
        case '{': return skip_container(parser, '}', true);
        case '[': return skip_container(parser, ']', false);
        case 't': return skip_literal(parser, "true");
        case 'f': return skip_literal(parser, "false");
        case 'n': return skip_literal(parser, "null");
        default: return skip_number(parser);
    }
}

static bool same_name(const char *left, const char *right, size_t length)
{
    for (size_t index = 0U; index < length; ++index)
    {
        if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index]))
        {
            return false;
        }
    }
    return true;
}

/* Filenames are appended to the catalog URL, so path and query syntax is rejected. */
static bool valid_filename(const char *filename)
{
    if (!isalnum((unsigned char)filename[0]) || (strstr(filename, "..") != NULL))
    {
        return false;
    }
    for (const char *cursor = filename; *cursor != '\0'; ++cursor)
    {
        if (!isalnum((unsigned char)*cursor) && (*cursor != '.') &&
            (*cursor != '_') && (*cursor != '-'))
        {
            return false;
        }
    }
    return true;
}

static bool parse_program(catalog_parser_t *parser, basic_catalog_program_t *program)
{
    bool has_filename = false;
    bool has_type = false;
    bool has_description = false;
    bool has_size = false;
    char key[16];

    memset(program, 0, sizeof(*program));
    if (!consume(parser, '{')) return false;
    if (consume(parser, '}')) return false;
    do
    {
        if (!parse_string(parser, key, sizeof(key)) || !consume(parser, ':')) return false;
        if (strcmp(key, "filename") == 0)
        {
            if (has_filename ||
                !parse_string(parser, program->filename, sizeof(program->filename)))
            {
                return false;
            }
            has_filename = true;
        }
        else if (strcmp(key, "type") == 0)
        {
            if (has_type || !parse_string(parser, program->type, sizeof(program->type)))
            {
                return false;
            }
            has_type = true;
        }
        else if (strcmp(key, "description") == 0)
        {
            if (has_description ||
                !parse_string(parser, program->description, sizeof(program->description)))
            {
                return false;
            }
            has_description = true;
        }
        else if (strcmp(key, "size") == 0)
        {
            if (has_size || !parse_size(parser, &program->size)) return false;
            has_size = true;
        }
        else if (!skip_value(parser))
        {
            return false;
        }
    } while (consume(parser, ','));
    return consume(parser, '}') && has_filename && has_size &&
           valid_filename(program->filename);
}

static bool parse_programs(catalog_parser_t *parser, basic_catalog_t *catalog)
{
    if (!consume(parser, '[')) return false;
    if (consume(parser, ']')) return true;
    do
    {
        if (catalog->count >= BASIC_CATALOG_MAX_PROGRAMS) return false;
        basic_catalog_program_t *program = &catalog->programs[catalog->count];
        if (!parse_program(parser, program)) return false;
        for (size_t index = 0U; index < catalog->count; ++index)
        {
            size_t length = strlen(program->filename);
            if ((strlen(catalog->programs[index].filename) == length) &&
                same_name(catalog->programs[index].filename, program->filename, length))
            {
                return false;
            }
        }
        ++catalog->count;
    } while (consume(parser, ','));
    return consume(parser, ']');
}

bool basic_catalog_parse(const char *json, size_t length, basic_catalog_t *catalog)
{
    static basic_catalog_t parsed;
    bool has_programs = false;
    char key[16];
    catalog_parser_t parser = {json, json + length, 0U};

    if ((json == NULL) || (catalog == NULL)) return false;
    memset(&parsed, 0, sizeof(parsed));
    if ((length >= 3U) && (memcmp(json, "\xEF\xBB\xBF", 3U) == 0)) parser.cursor += 3;
    if (!consume(&parser, '{') || peek(&parser, '}')) return false;
    do
    {
        if (!parse_string(&parser, key, sizeof(key)) || !consume(&parser, ':')) return false;
        if (strcmp(key, "programs") == 0)
        {
            if (has_programs || !parse_programs(&parser, &parsed)) return false;
            has_programs = true;
        }
        else if (!skip_value(&parser))
        {
            return false;
        }
    } while (consume(&parser, ','));
    if (!consume(&parser, '}') || !has_programs) return false;
    skip_whitespace(&parser);
    if (parser.cursor != parser.end) return false;
    *catalog = parsed;
    return true;
}

const basic_catalog_program_t *basic_catalog_find(const basic_catalog_t *catalog,
                                                  const char *name)
{
    if ((catalog == NULL) || (name == NULL)) return NULL;
    size_t name_length = strlen(name);
    for (size_t index = 0U; index < catalog->count; ++index)
    {
        const char *filename = catalog->programs[index].filename;
        size_t filename_length = strlen(filename);
        if (((filename_length == name_length) && same_name(filename, name, name_length)) ||
            ((filename_length == name_length + 4U) && same_name(filename, name, name_length) &&
             same_name(&filename[name_length], ".bas", 4U)))
        {
            return &catalog->programs[index];
        }
    }
    return NULL;
}

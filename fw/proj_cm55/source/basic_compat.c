/******************************************************************************
 * File: basic_compat.c
 * Description: Bounded classic BASIC syntax translation and runtime functions.
 ******************************************************************************/
#include "basic_compat.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "my_basic.h"

#define COMPAT_LINE_CAPACITY (1024U)
#define COMPAT_INPUT_CAPACITY (256U)
#define COMPAT_IDENTIFIER_CAPACITY (64U)

typedef struct
{
    char *output;
    size_t capacity;
    size_t length;
    uint32_t *map;
    size_t map_capacity;
    size_t row;
    uint32_t line;
    unsigned int label;
    struct
    {
        char variable[COMPAT_IDENTIFIER_CAPACITY];
        unsigned int label;
    } loops[32];
    size_t loop_count;
    bool hoist;
    bool trace;
    const char *source;
} compat_writer_t;

static const char *compat_error;
static basic_compat_platform_t compat_platform;
static unsigned int compat_column;
static real_t compat_last_random;
static char compat_arrays[32][COMPAT_IDENTIFIER_CAPACITY];
static size_t compat_array_count;
static bool compat_trace;
static const char *compat_trace_source;
static size_t compat_trace_size;

void basic_compat_set_trace(bool enabled)
{
    compat_trace = enabled;
}

static bool fail(const char *message)
{
    compat_error = message;
    return false;
}

const char *basic_compat_error(void)
{
    return compat_error;
}

static bool equal(const char *text, size_t length, const char *word)
{
    if (length != strlen(word))
    {
        return false;
    }
    for (size_t index = 0U; index < length; ++index)
    {
        if (toupper((unsigned char)text[index]) != word[index])
        {
            return false;
        }
    }
    return true;
}

static const char *spaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t') || (*text == '\r'))
    {
        ++text;
    }
    return text;
}

static const char *identifier_end(const char *text)
{
    while (isalnum((unsigned char)*text) || (*text == '_') || (*text == '$'))
    {
        ++text;
    }
    return text;
}

static bool array_name(const char *name, size_t length)
{
    for (size_t index = 0U; index < compat_array_count; ++index)
    {
        if (equal(name, length, compat_arrays[index])) return true;
    }
    return false;
}

static bool put(compat_writer_t *writer, const char *text, size_t length)
{
    if (length >= writer->capacity - writer->length)
    {
        return fail("Translated BASIC program exceeds runtime capacity.");
    }
    for (size_t index = 0U; index < length; ++index)
    {
        if (writer->map != NULL)
        {
            if (writer->row >= writer->map_capacity)
            {
                return fail("Translated BASIC program has too many statements.");
            }
            writer->map[writer->row] = writer->line;
        }
        if (text[index] == '\n')
        {
            ++writer->row;
        }
    }
    memcpy(&writer->output[writer->length], text, length);
    writer->length += length;
    writer->output[writer->length] = '\0';
    return true;
}

static bool text(compat_writer_t *writer, const char *value)
{
    return put(writer, value, strlen(value));
}

/* Find the parenthesis closing the one at open, ignoring quoted text. */
static const char *matching_close(const char *open, const char *end)
{
    bool quoted = false;
    unsigned int depth = 0U;
    for (const char *cursor = open; cursor < end; ++cursor)
    {
        if (*cursor == '"') quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '(') ++depth;
        else if ((*cursor == ')') && (--depth == 0U)) return cursor;
    }
    return NULL;
}

static bool constant_bound(const char *begin, const char *end)
{
    begin = spaces(begin);
    while ((end > begin) && isspace((unsigned char)end[-1])) --end;
    if (begin == end) return false;
    for (const char *cursor = begin; cursor < end; ++cursor)
    {
        if (!isdigit((unsigned char)*cursor)) return false;
    }
    return true;
}

/* Emit a runtime call that reports one original source statement when TRACE is on. */
static bool trace_marker(compat_writer_t *writer, const char *begin, const char *end)
{
    if (!writer->trace) return true;
    begin = spaces(begin);
    while ((end > begin) && isspace((unsigned char)end[-1])) --end;
    if (begin == end) return true;
    char marker[64];
    (void)snprintf(marker, sizeof(marker), "BCTRACE(%lu,%lu,%lu)\n",
                   (unsigned long)writer->line,
                   (unsigned long)(begin - writer->source),
                   (unsigned long)(end - begin));
    return text(writer, marker);
}

/* Find punctuation or a keyword outside strings and nested expressions. */
static const char *find(const char *begin, const char *end, const char *word)
{
    bool quoted = false;
    unsigned int depth = 0U;
    size_t length = strlen(word);
    for (const char *cursor = begin; cursor < end; ++cursor)
    {
        if (*cursor == '"')
        {
            quoted = !quoted;
        }
        if (quoted)
        {
            continue;
        }
        if (*cursor == '\'') return NULL;
        if (*cursor == '(')
        {
            ++depth;
        }
        else if ((*cursor == ')') && (depth > 0U))
        {
            --depth;
        }
        if ((depth == 0U) && ((size_t)(end - cursor) >= length) &&
            equal(cursor, length, word) &&
            ((length == 1U) ||
             (((cursor == begin) || !isalnum((unsigned char)cursor[-1])) &&
              !isalnum((unsigned char)cursor[length]))))
        {
            return cursor;
        }
    }
    return NULL;
}

static bool expression(compat_writer_t *writer, const char *begin, const char *end)
{
    while (begin < end)
    {
        if (*begin == '"')
        {
            const char *close = begin + 1;
            while ((close < end) && (*close != '"'))
            {
                ++close;
            }
            if (close == end)
            {
                return fail("Unterminated BASIC string.");
            }
            if (!put(writer, begin, (size_t)(close + 1 - begin)))
            {
                return false;
            }
            begin = close + 1;
        }
        else if (*begin == '\'')
        {
            return put(writer, begin, (size_t)(end - begin));
        }
        else if ((*begin == '&') && ((end - begin) >= 2) &&
                 ((begin[1] == 'H') || (begin[1] == 'h')))
        {
            const char *digits = begin + 2;
            const char *finish = digits;
            uint32_t value = 0U;
            while ((finish < end) && isxdigit((unsigned char)*finish))
            {
                unsigned int digit = isdigit((unsigned char)*finish)
                    ? (unsigned int)(*finish - '0')
                    : (unsigned int)(toupper((unsigned char)*finish) - 'A' + 10);
                if (value > (UINT32_MAX - digit) / 16U)
                {
                    return fail("BASIC hexadecimal literal exceeds 32 bits.");
                }
                value = value * 16U + digit;
                ++finish;
            }
            if ((finish == digits) ||
                ((finish < end) &&
                 (isalnum((unsigned char)*finish) || (*finish == '_') || (*finish == '$'))))
            {
                return fail("Invalid BASIC hexadecimal literal.");
            }
            char decimal[16];
            /* Force real parsing above MY-BASIC's signed integer range. */
            (void)snprintf(decimal, sizeof(decimal), "%lu%s", (unsigned long)value,
                           (value > INT32_MAX) ? ".0" : "");
            if (!text(writer, decimal))
            {
                return false;
            }
            begin = finish;
        }
        else if (isalpha((unsigned char)*begin) || (*begin == '_'))
        {
            const char *finish = identifier_end(begin);
            size_t length = (size_t)(finish - begin);
            const char *replacement = NULL;
            if (equal(begin, length, "INT")) replacement = "FLOOR";
            else if (equal(begin, length, "RND"))
                replacement = (*spaces(finish) == '(') ? "BCRND" : "BCRND()";
            else if (equal(begin, length, "MID$")) replacement = "BCMID";
            else if (equal(begin, length, "LEFT$")) replacement = "BCLEFT";
            else if (equal(begin, length, "RIGHT$")) replacement = "BCRIGHT";
            else if (equal(begin, length, "STR$")) replacement = "STR";
            else if (equal(begin, length, "CHR$")) replacement = "CHR";
            else if (equal(begin, length, "TAB")) replacement = "BCTAB";
            else if (equal(begin, length, "TIMER"))
                replacement = (*spaces(finish) == '(') ? "BCTIMER" : "BCTIMER()";
            else if (equal(begin, length, "INKEY$"))
                replacement = (*spaces(finish) == '(') ? "BCINKEY" : "BCINKEY()";
            if ((replacement == NULL) && (*spaces(finish) == '(') &&
                array_name(begin, length) && !text(writer, "BCA_"))
            {
                return false;
            }
            if (!(replacement ? text(writer, replacement) : put(writer, begin, length)))
            {
                return false;
            }
            begin = finish;
        }
        else
        {
            if (!put(writer, begin++, 1U))
            {
                return false;
            }
        }
    }
    return true;
}

static bool statements(compat_writer_t *writer, const char *begin,
                        const char *end, unsigned int nesting);

static bool target(compat_writer_t *writer, const char *begin, const char *end)
{
    begin = spaces(begin);
    if (begin == end)
    {
        return fail("Missing BASIC branch target.");
    }
    if (isdigit((unsigned char)*begin))
    {
        if (!text(writer, "L"))
        {
            return false;
        }
    }
    return put(writer, begin, (size_t)(end - begin));
}

/**
 * @brief Translate one statement, including classic DIM and computed branches.
 */
static bool statement(compat_writer_t *writer, const char *begin, const char *end)
{
    begin = spaces(begin);
    while ((end > begin) && isspace((unsigned char)end[-1]))
    {
        --end;
    }
    if (begin == end)
    {
        return true;
    }
    const char *name_end = identifier_end(begin);
    size_t length = (size_t)(name_end - begin);
    const char *arguments = spaces(name_end);
    if (arguments > end) arguments = end;
    if (writer->hoist && !equal(begin, length, "DIM"))
    {
        return true;
    }
    if (equal(begin, length, "REM"))
    {
        return put(writer, begin, (size_t)(end - begin)) && text(writer, "\n");
    }
    if (equal(begin, length, "DEF"))
    {
        const char *assignment = find(arguments, end, "=");
        if (assignment == NULL)
        {
            return expression(writer, begin, end) && text(writer, "\n");
        }
        return text(writer, "DEF ") &&
               put(writer, arguments, (size_t)(assignment - arguments)) &&
               text(writer, "\nRETURN ") &&
               expression(writer, assignment + 1, end) &&
               text(writer, "\nENDDEF\n");
    }
    if (equal(begin, length, "ON"))
    {
        const char *branch = find(arguments, end, "GOTO");
        const char *operation = "GOTO ";
        if (branch == NULL)
        {
            branch = find(arguments, end, "GOSUB");
            operation = "GOSUB ";
        }
        if (branch == NULL)
        {
            return fail("ON requires GOTO or GOSUB.");
        }
        if (!text(writer, "BCON = FLOOR(") ||
            !expression(writer, arguments, branch) || !text(writer, ")\n"))
        {
            return false;
        }
        const char *cursor = spaces(identifier_end(branch));
        unsigned int index = 1U;
        while (cursor < end)
        {
            const char *comma = find(cursor, end, ",");
            const char *finish = (comma != NULL) ? comma : end;
            char condition[64];
            (void)snprintf(condition, sizeof(condition), "IF BCON = %u THEN %s",
                           index++, operation);
            if (!text(writer, condition) || !target(writer, cursor, finish) ||
                !text(writer, "\n"))
            {
                return false;
            }
            cursor = (comma != NULL) ? spaces(comma + 1) : end;
        }
        return true;
    }
    if (equal(begin, length, "FOR"))
    {
        const char *variable_end = identifier_end(arguments);
        const char *assignment = spaces(variable_end);
        const char *to = find(assignment, end, "TO");
        const char *step = (to != NULL) ? find(to + 2, end, "STEP") : NULL;
        size_t name_length = (size_t)(variable_end - arguments);
        if ((name_length == 0U) || (name_length >= COMPAT_IDENTIFIER_CAPACITY) ||
            (*assignment != '=') || (to == NULL) || (writer->loop_count >= 32U))
        {
            return fail("Invalid or excessively nested FOR loop.");
        }
        unsigned int label = ++writer->label;
        size_t index = writer->loop_count++;
        for (size_t character = 0U; character < name_length; ++character)
        {
            writer->loops[index].variable[character] =
                (char)toupper((unsigned char)arguments[character]);
        }
        writer->loops[index].variable[name_length] = '\0';
        writer->loops[index].label = label;
        char control[256];
        if (!expression(writer, arguments, to) ||
            !text(writer, "\n")) return false;
        (void)snprintf(control, sizeof(control), "BCLIMIT%u=", label);
        if (!text(writer, control) ||
            !expression(writer, to + 2, (step != NULL) ? step : end) ||
            !text(writer, "\n")) return false;
        (void)snprintf(control, sizeof(control), "BCSTEP%u=", label);
        if (!text(writer, control) ||
            !((step != NULL) ? expression(writer, step + 4, end) : text(writer, "1")) ||
            !text(writer, "\n")) return false;
        (void)snprintf(control, sizeof(control),
                       "BCLOOP%u: IF (BCSTEP%u>=0 AND %s>BCLIMIT%u) OR "
                       "(BCSTEP%u<0 AND %s<BCLIMIT%u) THEN GOTO BCEXIT%u\n",
                       label, label, writer->loops[index].variable, label,
                       label, writer->loops[index].variable, label, label);
        return text(writer, control);
    }
    if (equal(begin, length, "NEXT"))
    {
        if (writer->loop_count == 0U) return fail("NEXT without FOR.");
        size_t index = --writer->loop_count;
        if ((arguments < end) &&
            !equal(arguments, (size_t)(identifier_end(arguments) - arguments),
                   writer->loops[index].variable))
        {
            return fail("NEXT variable does not match FOR.");
        }
        unsigned int label = writer->loops[index].label;
        char control[256];
        (void)snprintf(control, sizeof(control),
                       "%s=%s+BCSTEP%u\nGOTO BCLOOP%u\nBCEXIT%u: BCNOOP()\n",
                       writer->loops[index].variable, writer->loops[index].variable,
                       label, label, label);
        return text(writer, control);
    }
    if (equal(begin, length, "DIM"))
    {
        /* Constant arrays are hoisted; computed bounds get a hoisted binding and run in place. */
        const char *cursor = arguments;
        while (cursor < end)
        {
            const char *name_end = identifier_end(cursor);
            const char *open = spaces(name_end);
            const char *close = ((open < end) && (*open == '(')) ? matching_close(open, end) : NULL;
            if ((close == NULL) || (name_end == cursor))
            {
                return fail("Invalid classic DIM declaration.");
            }
            bool computed = false;
            unsigned int bounds = 0U;
            for (const char *bound = open + 1; bound < close;)
            {
                const char *comma = find(bound, close, ",");
                const char *finish = (comma != NULL) ? comma : close;
                if (constant_bound(bound, finish))
                {
                    if (strtoul(spaces(bound), NULL, 10) >= 65535UL)
                    {
                        return fail("Classic DIM bound is too large.");
                    }
                }
                else if (spaces(bound) == finish)
                {
                    return fail("Invalid classic DIM bound.");
                }
                else
                {
                    computed = true;
                }
                ++bounds;
                bound = (comma != NULL) ? comma + 1 : close;
            }
            if (bounds == 0U) return fail("Invalid classic DIM bound.");
            size_t name_length = (size_t)(name_end - cursor);
            if (writer->hoist && !array_name(cursor, name_length))
            {
                if ((name_length >= COMPAT_IDENTIFIER_CAPACITY) || (compat_array_count >= 32U))
                {
                    return fail("Too many or invalid classic arrays.");
                }
                for (size_t index = 0U; index < name_length; ++index)
                {
                    compat_arrays[compat_array_count][index] =
                        (char)toupper((unsigned char)cursor[index]);
                }
                compat_arrays[compat_array_count++][name_length] = '\0';
            }
            if (writer->hoist || computed)
            {
                if (!text(writer, "DIM BCA_") ||
                    !put(writer, cursor, (size_t)(name_end - cursor)) ||
                    !text(writer, "(")) return false;
                for (const char *bound = open + 1; bound < close;)
                {
                    const char *comma = find(bound, close, ",");
                    const char *finish = (comma != NULL) ? comma : close;
                    if (writer->hoist && computed)
                    {
                        if (!text(writer, "1")) return false;
                    }
                    else if (constant_bound(bound, finish))
                    {
                        char value[16];
                        (void)snprintf(value, sizeof(value), "%lu",
                                       strtoul(spaces(bound), NULL, 10) + 1UL);
                        if (!text(writer, value)) return false;
                    }
                    else if (!text(writer, "(") || !expression(writer, bound, finish) ||
                             !text(writer, ")+1"))
                    {
                        return false;
                    }
                    if ((comma != NULL) && !text(writer, ",")) return false;
                    bound = (comma != NULL) ? comma + 1 : close;
                }
                if (!text(writer, ")\n")) return false;
            }
            cursor = spaces(close + 1);
            if (cursor < end)
            {
                if (*cursor != ',') return fail("Invalid classic DIM list.");
                cursor = spaces(cursor + 1);
            }
        }
        return true;
    }
    if (equal(begin, length, "INPUT"))
    {
        if (!text(writer, "BCINPUT(")) return false;
        if ((arguments < end) && (*arguments == '"'))
        {
            const char *close = strchr(arguments + 1, '"');
            if ((close == NULL) || (close >= end) ||
                !put(writer, arguments, (size_t)(close + 1 - arguments)))
            {
                return fail("Invalid INPUT prompt.");
            }
            arguments = spaces(close + 1);
            if ((*arguments != ';') && (*arguments != ','))
            {
                return fail("INPUT prompt requires a separator.");
            }
            arguments = spaces(arguments + 1);
        }
        else if (!text(writer, "\"\"")) return false;
        while (arguments < end)
        {
            const char *finish = identifier_end(arguments);
            if ((finish == arguments) || !text(writer, ",\"") ||
                !put(writer, arguments, (size_t)(finish - arguments)) ||
                !text(writer, "\""))
            {
                return fail("INPUT requires variable names.");
            }
            arguments = spaces(finish);
            if (arguments < end)
            {
                if (*arguments != ',') return fail("Invalid INPUT variable list.");
                arguments = spaces(arguments + 1);
            }
        }
        return text(writer, ")\n");
    }
    if (equal(begin, length, "SLEEP"))
    {
        return text(writer, "BCSLEEP(") && expression(writer, arguments, end) &&
               text(writer, ")\n");
    }
    if (equal(begin, length, "RANDOMIZE"))
    {
        return text(writer, "BCRANDOMIZE(") && expression(writer, arguments, end) &&
               text(writer, ")\n");
    }
    if (equal(begin, length, "STOP"))
    {
        return text(writer, "END\n");
    }
    if (equal(begin, length, "PRINT"))
    {
        if (!text(writer, "PRINT ")) return false;
        if (arguments == end) return text(writer, "\"\"\n");
        const char *cursor = arguments;
        while (cursor < end)
        {
            const char *separator = find(cursor, end, ";");
            const char *comma = find(cursor, end, ",");
            if ((comma != NULL) && ((separator == NULL) || (comma < separator)))
            {
                separator = comma;
            }
            const char *finish = (separator != NULL) ? separator : end;
            if (!expression(writer, cursor, finish)) return false;
            if (separator != NULL)
            {
                if (!text(writer, ",")) return false;
            }
            cursor = (separator != NULL) ? separator + 1 : end;
        }
        return text(writer, "\n");
    }
    if (equal(begin, length, "GOTO") || equal(begin, length, "GOSUB") ||
        equal(begin, length, "RESTORE"))
    {
        return put(writer, begin, length) && text(writer, " ") &&
               target(writer, arguments, end) && text(writer, "\n");
    }
    return expression(writer, begin, end) && text(writer, "\n");
}

static const char *matching_else(const char *begin, const char *end)
{
    unsigned int nested = 0U;
    bool quoted = false;
    for (const char *cursor = begin; cursor < end; ++cursor)
    {
        if (*cursor == '"') quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '\'') break;
        if (isalpha((unsigned char)*cursor))
        {
            const char *finish = identifier_end(cursor);
            if (equal(cursor, (size_t)(finish - cursor), "REM")) break;
            if (equal(cursor, (size_t)(finish - cursor), "IF")) ++nested;
            if (equal(cursor, (size_t)(finish - cursor), "ELSE"))
            {
                if (nested == 0U) return cursor;
                --nested;
            }
            cursor = finish - 1;
        }
    }
    return NULL;
}

/**
 * @brief Parse colon-separated statements and scope inline IF to its line.
 */
static bool statements(compat_writer_t *writer, const char *begin,
                        const char *end, unsigned int nesting)
{
    if (nesting > 16U)
    {
        return fail("BASIC IF nesting exceeds 16 levels.");
    }
    while (begin < end)
    {
        begin = spaces(begin);
        const char *name_end = identifier_end(begin);
        if (equal(begin, (size_t)(name_end - begin), "REM"))
        {
            return trace_marker(writer, begin, end) && statement(writer, begin, end);
        }
        if (equal(begin, (size_t)(name_end - begin), "IF"))
        {
            const char *then = find(name_end, end, "THEN");
            if (then == NULL) return fail("IF requires THEN.");
            const char *body = spaces(then + 4);
            const char *otherwise = matching_else(body, end);
            const char *finish = (otherwise != NULL) ? otherwise : end;
            if (writer->hoist)
            {
                if ((body < finish) && isdigit((unsigned char)*body))
                {
                    const char *colon = find(body, finish, ":");
                    body = (colon != NULL) ? colon + 1 : finish;
                }
                return statements(writer, body, finish, nesting + 1U) &&
                       ((otherwise == NULL) ||
                        statements(writer, otherwise + 4, end, nesting + 1U));
            }
            unsigned int label = ++writer->label;
            char skip[64];
            const char *jump_end = NULL;
            if ((body < finish) && isdigit((unsigned char)*body))
            {
                const char *colon = find(body, finish, ":");
                jump_end = (colon != NULL) ? colon : finish;
            }
            (void)snprintf(skip, sizeof(skip), ") THEN GOTO BCSKIP%u\n", label);
            if (!trace_marker(writer, begin, (jump_end != NULL) ? jump_end : then + 4) ||
                !text(writer, "IF NOT(") || !expression(writer, name_end, then) ||
                !text(writer, skip))
            {
                return false;
            }
            if (jump_end != NULL)
            {
                if (!text(writer, "GOTO ") || !target(writer, body, jump_end) ||
                    !text(writer, "\n")) return false;
                body = (jump_end < finish) ? jump_end + 1 : finish;
            }
            if (!statements(writer, body, finish, nesting + 1U)) return false;
            if (otherwise != NULL)
            {
                (void)snprintf(skip, sizeof(skip),
                               "GOTO BCEND%u\nBCSKIP%u: BCNOOP()\n", label, label);
                if (!text(writer, skip) ||
                    !statements(writer, otherwise + 4, end, nesting + 1U)) return false;
                (void)snprintf(skip, sizeof(skip), "BCEND%u: BCNOOP()\n", label);
            }
            else
            {
                (void)snprintf(skip, sizeof(skip), "BCSKIP%u: BCNOOP()\n", label);
            }
            return text(writer, skip);
        }
        const char *colon = find(begin, end, ":");
        const char *finish = (colon != NULL) ? colon : end;
        if (!trace_marker(writer, begin, finish)) return false;
        if (isdigit((unsigned char)*begin))
        {
            if (!writer->hoist &&
                (!text(writer, "GOTO ") || !target(writer, begin, finish) ||
                 !text(writer, "\n"))) return false;
        }
        else if (!statement(writer, begin, finish))
        {
            return false;
        }
        begin = (colon != NULL) ? colon + 1 : end;
    }
    return true;
}

/**
 * @brief Translate classic source without modifying the stored program.
 */
bool basic_compat_translate(const char *source, bool numbered,
                             char *output, size_t capacity,
                             uint32_t *line_map, size_t map_capacity)
{
    compat_error = NULL;
    if ((source == NULL) || (output == NULL) || (capacity == 0U))
    {
        return fail("Invalid BASIC parser buffer.");
    }
    compat_writer_t writer = {0};
    writer.output = output;
    writer.capacity = capacity;
    writer.map = line_map;
    writer.map_capacity = map_capacity;
    writer.source = source;
    output[0] = '\0';
    if (numbered)
    {
        compat_array_count = 0U;
        compat_trace_source = source;
        compat_trace_size = strlen(source);
    }
    /* Bind arrays and expression functions before forward references are parsed. */
    for (unsigned int pass = 0U; pass < 3U; ++pass)
    {
        const char *cursor = source;
        uint32_t row = 0U;
        while (*cursor != '\0')
        {
            ++row;
            const char *end = strchr(cursor, '\n');
            if (end == NULL) end = cursor + strlen(cursor);
            const char *body = spaces(cursor);
            writer.line = row;
            if (numbered)
            {
                char *number_end;
                unsigned long number = strtoul(body, &number_end, 10);
                if ((number_end == body) || (number == 0UL) || (number > 65535UL))
                {
                    return fail("Invalid BASIC line number.");
                }
                writer.line = (uint32_t)number;
                body = spaces(number_end);
            }
            bool definition = equal(body, (size_t)(identifier_end(body) - body), "DEF");
            writer.hoist = (pass == 0U);
            writer.trace = compat_trace && numbered && (pass == 2U);
            if ((pass == 0U) || (definition && (pass == 1U)) ||
                (!definition && (pass == 2U)))
            {
                if ((pass == 2U) && numbered)
                {
                    char label[40];
                    (void)snprintf(label, sizeof(label), "L%lu: BCNOOP()\n",
                                   (unsigned long)writer.line);
                    if (!text(&writer, label)) return false;
                }
                if (!statements(&writer, body, end, 0U)) return false;
            }
            else if ((pass == 2U) && numbered)
            {
                char label[40];
                (void)snprintf(label, sizeof(label), "L%lu: BCNOOP()\n",
                               (unsigned long)writer.line);
                if (!text(&writer, label) ||
                    !trace_marker(&writer, body, end)) return false;
            }
            cursor = (*end == '\n') ? end + 1 : end;
        }
    }
    return (writer.loop_count == 0U) ? true : fail("FOR without NEXT.");
}

static int runtime_error(struct mb_interpreter_t *interpreter, void **list)
{
    return mb_raise_error(interpreter, list, SE_RN_INVALID_EXPRESSION, MB_FUNC_ERR);
}

static int no_operation(struct mb_interpreter_t *interpreter, void **list)
{
    mb_check(mb_attempt_open_bracket(interpreter, list));
    return mb_attempt_close_bracket(interpreter, list);
}

static int trace_statement(struct mb_interpreter_t *interpreter, void **list)
{
    int_t line;
    int_t offset;
    int_t length;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_int(interpreter, list, &line));
    mb_check(mb_pop_int(interpreter, list, &offset));
    mb_check(mb_pop_int(interpreter, list, &length));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if ((compat_trace_source == NULL) || (offset < 0) || (length < 0) ||
        ((size_t)offset > compat_trace_size) ||
        ((size_t)length > compat_trace_size - (size_t)offset))
    {
        return runtime_error(interpreter, list);
    }
    /* Trace output always starts on its own line so it never splits program output. */
    if (compat_column != 0U) (void)fputs("\r\n", stdout);
    printf("TRACE %ld: ", (long)line);
    (void)fwrite(&compat_trace_source[offset], 1U, (size_t)length, stdout);
    (void)fputs("\r\n", stdout);
    compat_column = 0U;
    return MB_FUNC_OK;
}

static int random_value(struct mb_interpreter_t *interpreter, void **list)
{
    real_t argument = 1;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    if (mb_has_arg(interpreter, list))
    {
        mb_check(mb_pop_real(interpreter, list, &argument));
    }
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (!isfinite(argument) || (argument < -(double)UINT32_MAX))
    {
        return runtime_error(interpreter, list);
    }
    if (argument < 0) srand((unsigned int)-argument);
    if (argument != 0)
    {
        compat_last_random = (real_t)((double)rand() / ((double)RAND_MAX + 1.0));
    }
    return mb_push_real(interpreter, list, compat_last_random);
}

static int random_seed(struct mb_interpreter_t *interpreter, void **list)
{
    real_t seed;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_real(interpreter, list, &seed));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (!isfinite(seed) || (seed < 0) || (seed > UINT32_MAX))
    {
        return runtime_error(interpreter, list);
    }
    srand((unsigned int)seed);
    return MB_FUNC_OK;
}

static int timer_value(struct mb_interpreter_t *interpreter, void **list)
{
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (compat_platform.seconds == NULL) return runtime_error(interpreter, list);
    return mb_push_real(interpreter, list, (real_t)compat_platform.seconds());
}

static int sleep_seconds(struct mb_interpreter_t *interpreter, void **list)
{
    real_t seconds;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_real(interpreter, list, &seconds));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (!isfinite(seconds) || (seconds < 0) || (compat_platform.sleep == NULL))
    {
        return runtime_error(interpreter, list);
    }
    return compat_platform.sleep(seconds) ? MB_FUNC_OK : MB_EXTENDED_ABORT;
}

static int key_value(struct mb_interpreter_t *interpreter, void **list)
{
    char key[2] = {0};
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (compat_platform.read_key == NULL) return runtime_error(interpreter, list);
    if (compat_platform.read_key != NULL)
    {
        (void)compat_platform.read_key(&key[0]);
    }
    return mb_push_string(interpreter, list, mb_memdup(key, (unsigned int)strlen(key) + 1U));
}

static int middle_string(struct mb_interpreter_t *interpreter, void **list)
{
    char *value;
    int_t start;
    int_t count;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_string(interpreter, list, &value));
    mb_check(mb_pop_int(interpreter, list, &start));
    count = (int_t)strlen(value);
    if (mb_has_arg(interpreter, list)) mb_check(mb_pop_int(interpreter, list, &count));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if ((start < 1) || (count < 0)) return runtime_error(interpreter, list);
    size_t length = strlen(value);
    size_t offset = ((size_t)start > length) ? length : (size_t)start - 1U;
    size_t size = ((size_t)count > length - offset) ? length - offset : (size_t)count;
    char *result = mb_memdup(&value[offset], (unsigned int)size + 1U);
    if (result == NULL) return mb_raise_error(interpreter, list, SE_RN_OUT_OF_MEMORY, MB_FUNC_ERR);
    result[size] = '\0';
    return mb_push_string(interpreter, list, result);
}

/**
 * @brief Clamp classic LEFT$/RIGHT$ counts, including zero and empty input.
 */
static int edge_string(struct mb_interpreter_t *interpreter, void **list, bool right)
{
    char *value;
    int_t count;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_string(interpreter, list, &value));
    mb_check(mb_pop_int(interpreter, list, &count));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (count < 0) return runtime_error(interpreter, list);
    size_t length = strlen(value);
    size_t size = ((size_t)count > length) ? length : (size_t)count;
    size_t offset = right ? length - size : 0U;
    char *result = mb_memdup(value + offset, (unsigned int)size + 1U);
    if (result == NULL) return mb_raise_error(interpreter, list, SE_RN_OUT_OF_MEMORY, MB_FUNC_ERR);
    result[size] = '\0';
    return mb_push_string(interpreter, list, result);
}

static int left_string(struct mb_interpreter_t *interpreter, void **list)
{
    return edge_string(interpreter, list, false);
}

static int right_string(struct mb_interpreter_t *interpreter, void **list)
{
    return edge_string(interpreter, list, true);
}

static int tab_string(struct mb_interpreter_t *interpreter, void **list)
{
    int_t column;
    char result[COMPAT_INPUT_CAPACITY];
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_int(interpreter, list, &column));
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if ((column < 1) || (column >= (int_t)sizeof(result))) return runtime_error(interpreter, list);
    size_t length = ((unsigned int)column > compat_column + 1U)
                        ? (unsigned int)column - compat_column - 1U : 0U;
    memset(result, ' ', length);
    result[length] = '\0';
    return mb_push_string(interpreter, list, mb_memdup(result, (unsigned int)length + 1U));
}

static int compat_print(struct mb_interpreter_t *interpreter, const char *format, ...)
{
    (void)interpreter;
    va_list arguments;
    va_start(arguments, format);
    char output[COMPAT_LINE_CAPACITY];
    int length = vsnprintf(output, sizeof(output), format, arguments);
    va_end(arguments);
    if ((length < 0) || ((size_t)length >= sizeof(output))) return -1;
    for (int index = 0; index < length; ++index)
    {
        if ((output[index] == '\n') || (output[index] == '\r')) compat_column = 0U;
        else ++compat_column;
    }
    return (int)fwrite(output, 1U, (size_t)length, stdout);
}

/**
 * @brief Read a classic INPUT line and assign comma-separated numeric/string fields.
 */
static int input_values(struct mb_interpreter_t *interpreter, void **list)
{
    char *prompt;
    char names[8][COMPAT_IDENTIFIER_CAPACITY];
    size_t count = 0U;
    mb_check(mb_attempt_open_bracket(interpreter, list));
    mb_check(mb_pop_string(interpreter, list, &prompt));
    char prompt_copy[COMPAT_INPUT_CAPACITY];
    if (strlen(prompt) >= sizeof(prompt_copy)) return runtime_error(interpreter, list);
    strcpy(prompt_copy, prompt);
    while (mb_has_arg(interpreter, list))
    {
        char *name;
        mb_check(mb_pop_string(interpreter, list, &name));
        if ((count >= 8U) || (strlen(name) >= sizeof(names[0]))) return runtime_error(interpreter, list);
        strcpy(names[count++], name);
    }
    mb_check(mb_attempt_close_bracket(interpreter, list));
    if (count == 0U) return runtime_error(interpreter, list);
    if (compat_print(interpreter, "%s? ", prompt_copy) < 0)
    {
        return runtime_error(interpreter, list);
    }
    (void)fflush(stdout);
    char input[COMPAT_INPUT_CAPACITY];
    if (compat_platform.input == NULL) return runtime_error(interpreter, list);
    int length = compat_platform.input(interpreter, prompt_copy, input, sizeof(input));
    if (length < 0) return MB_EXTENDED_ABORT;
    char *cursor = input;
    mb_value_t values[8];
    for (size_t index = 0U; index < count; ++index)
    {
        char *comma = strchr(cursor, ',');
        if (comma != NULL) *comma = '\0';
        mb_value_t value;
        mb_make_nil(value);
        if (names[index][strlen(names[index]) - 1U] == '$')
        {
            value.type = MB_DT_STRING;
            value.value.string = cursor;
        }
        else
        {
            char *finish;
            double number = strtod(cursor, &finish);
            if ((finish == cursor) || (*spaces(finish) != '\0') || !isfinite(number))
            {
                return runtime_error(interpreter, list);
            }
#ifndef MB_DOUBLE_FLOAT
            if ((number > FLT_MAX) || (number < -FLT_MAX))
            {
                return runtime_error(interpreter, list);
            }
#endif
            value.type = MB_DT_REAL;
            value.value.float_point = (real_t)number;
        }
        values[index] = value;
        if ((index + 1U < count) && (comma == NULL)) return runtime_error(interpreter, list);
        if ((index + 1U == count) && (comma != NULL)) return runtime_error(interpreter, list);
        if (comma != NULL) cursor = comma + 1;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        mb_check(mb_assign_input(interpreter, names[index], values[index]));
    }
    return MB_FUNC_OK;
}

/**
 * @brief Install classic functions while preserving MY-BASIC's built-in APIs.
 */
int basic_compat_register(struct mb_interpreter_t *interpreter,
                           const basic_compat_platform_t *platform)
{
    if (platform != NULL) compat_platform = *platform;
    compat_column = 0U;
    mb_check(mb_set_classic_basic(interpreter, true));
    if (!mb_register_func(interpreter, "BCRND", random_value) ||
        !mb_register_func(interpreter, "BCRANDOMIZE", random_seed) ||
        !mb_register_func(interpreter, "BCTIMER", timer_value) ||
        !mb_register_func(interpreter, "BCSLEEP", sleep_seconds) ||
        !mb_register_func(interpreter, "BCMID", middle_string) ||
        !mb_register_func(interpreter, "BCLEFT", left_string) ||
        !mb_register_func(interpreter, "BCRIGHT", right_string) ||
        !mb_register_func(interpreter, "BCTAB", tab_string) ||
        !mb_register_func(interpreter, "BCINKEY", key_value) ||
        !mb_register_func(interpreter, "BCNOOP", no_operation) ||
        !mb_register_func(interpreter, "BCTRACE", trace_statement) ||
        !mb_register_func(interpreter, "BCINPUT", input_values))
    {
        return MB_FUNC_ERR;
    }
    return mb_set_printer(interpreter, compat_print);
}

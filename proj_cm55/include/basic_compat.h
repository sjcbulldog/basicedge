/******************************************************************************
 * File: basic_compat.h
 * Description: Classic numbered BASIC parser and MY-BASIC runtime adapters.
 ******************************************************************************/
#ifndef BASIC_COMPAT_H
#define BASIC_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mb_interpreter_t;

typedef struct
{
    int (*input)(struct mb_interpreter_t *interpreter, const char *prompt,
                 char *buffer, int capacity);
    double (*seconds)(void);
    bool (*sleep)(double seconds);
    bool (*read_key)(char *character);
} basic_compat_platform_t;

bool basic_compat_translate(const char *source, bool numbered,
                             char *output, size_t capacity,
                             uint32_t *line_map, size_t map_capacity);
int basic_compat_register(struct mb_interpreter_t *interpreter,
                           const basic_compat_platform_t *platform);
const char *basic_compat_error(void);

#endif

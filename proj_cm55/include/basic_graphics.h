/******************************************************************************
 * File: basic_graphics.h
 * Description: Bounded BASIC-to-LVGL graphics bridge owned by the CM55 task.
 ******************************************************************************/

#ifndef BASIC_GRAPHICS_H
#define BASIC_GRAPHICS_H

#include <stdbool.h>

struct mb_interpreter_t;

void basic_graphics_initialize(void);
void basic_graphics_register(struct mb_interpreter_t *interpreter);
void basic_graphics_mark_ready(void);
void basic_graphics_process(void);
bool basic_graphics_command(const char *arguments);

#endif /* BASIC_GRAPHICS_H */
/******************************************************************************
 * File: basic_console.h
 * Description: Classic BASIC program storage and command interface.
 ******************************************************************************/

#ifndef BASIC_CONSOLE_H
#define BASIC_CONSOLE_H

#include <stdbool.h>

struct mb_interpreter_t;

void basic_console_initialize(void);
void basic_console_trace_step(const char *file,
                              int position,
                              unsigned short row,
                              unsigned short column);
bool basic_console_process(struct mb_interpreter_t **interpreter,
                           const char *input,
                           int *status);

#endif /* BASIC_CONSOLE_H */
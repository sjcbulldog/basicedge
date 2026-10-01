/******************************************************************************
 * File: basic_task.c
 * Description: Runs the MY-BASIC interpreter on CM55 using UART standard I/O.
 ******************************************************************************/

#include "basic_task.h"

#include <ctype.h>
#include <stdio.h>

#include "basic_console.h"
#include "basic_graphics.h"
#include "message_tasks.h"
#include "my_basic.h"
#include "retarget_io_init.h"
#include "task.h"

#define BASIC_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 8U)
#define BASIC_TASK_PRIORITY   (tskIDLE_PRIORITY + 1U)
#define BASIC_INPUT_SIZE      (256U)
#define BASIC_CTRL_C          (0x03)

static bool basic_interrupt_requested;

static int basic_uart_read_line(struct mb_interpreter_t *interpreter,
                                char *buffer,
                                int buffer_size,
                                bool program_input)
{
    static bool discard_line_feed;
    size_t length = 0U;

    (void)fflush(stdout);
    while (length < ((size_t)buffer_size - 1U))
    {
        char character;
        if (CY_RSLT_SUCCESS != retarget_io_read_character(&character))
        {
            return 0;
        }

        if (BASIC_CTRL_C == (unsigned char)character)
        {
            basic_interrupt_requested = true;
            if (program_input)
            {
                (void)mb_schedule_suspend(interpreter, MB_EXTENDED_ABORT);
            }
            printf("^C\r\n");
            buffer[0] = '\0';
            return 0;
        }

        if (discard_line_feed)
        {
            discard_line_feed = false;
            if ('\n' == character)
            {
                continue;
            }
        }

        if (('\r' == character) || ('\n' == character))
        {
            discard_line_feed = ('\r' == character);
            printf("\r\n");
            break;
        }

        if ((('\b' == character) || (0x7F == character)) && (length > 0U))
        {
            --length;
            printf("\b \b");
            (void)fflush(stdout);
        }
        else if (isprint((unsigned char)character))
        {
            buffer[length++] = character;
            (void)putchar(character);
            (void)fflush(stdout);
        }
    }

    buffer[length] = '\0';

    return (int)length;
}

static int basic_uart_input(struct mb_interpreter_t *interpreter,
                            const char *prompt,
                            char *buffer,
                            int buffer_size)
{
    CY_UNUSED_PARAMETER(prompt);

    return basic_uart_read_line(interpreter, buffer, buffer_size, true);
}

static int basic_runtime_step(struct mb_interpreter_t *interpreter,
                              void **statement,
                              const char *file,
                              int position,
                              unsigned short row,
                              unsigned short column)
{
    char character;

    CY_UNUSED_PARAMETER(interpreter);
    CY_UNUSED_PARAMETER(statement);
    CY_UNUSED_PARAMETER(file);
    CY_UNUSED_PARAMETER(position);
    CY_UNUSED_PARAMETER(row);
    CY_UNUSED_PARAMETER(column);

    basic_console_trace_step(file, position, row, column);

    while (retarget_io_try_read_character(&character))
    {
        if (BASIC_CTRL_C == (unsigned char)character)
        {
            basic_interrupt_requested = true;
        }
    }

    return basic_interrupt_requested ? MB_EXTENDED_ABORT : MB_FUNC_OK;
}

static void basic_error_handler(struct mb_interpreter_t *interpreter,
                                mb_error_e error,
                                const char *description,
                                const char *file,
                                int position,
                                unsigned short row,
                                unsigned short column,
                                int abort_code)
{
    CY_UNUSED_PARAMETER(interpreter);
    CY_UNUSED_PARAMETER(file);
    CY_UNUSED_PARAMETER(position);
    CY_UNUSED_PARAMETER(abort_code);

    if (basic_interrupt_requested)
    {
        return;
    }

    printf("MY-BASIC error %d at %u:%u: %s\r\n",
           (int)error,
           (unsigned int)row,
           (unsigned int)column,
           (NULL != description) ? description : "unknown error");
}

static void basic_task(void *argument)
{
    struct mb_interpreter_t *interpreter = NULL;
    char input[BASIC_INPUT_SIZE];
    int status;

    CY_UNUSED_PARAMETER(argument);

    message_tasks_wait_for_sd_probe();
    message_tasks_wait_for_wifi_ready();
    printf("Starting MY-BASIC %s on CM55\r\n", mb_ver_string());

    status = mb_init();
    if (MB_FUNC_OK == status)
    {
        status = mb_open(&interpreter);
    }
    if (MB_FUNC_OK == status)
    {
        basic_graphics_register(interpreter);
        (void)mb_set_inputer(interpreter, basic_uart_input);
        (void)mb_set_error_handler(interpreter, basic_error_handler);
        (void)mb_debug_set_stepped_handler(interpreter,
                           basic_runtime_step,
                           NULL);
    }

    basic_console_initialize();
    while (NULL != interpreter)
    {
        printf("BASIC> ");
        basic_interrupt_requested = false;
        int input_length = basic_uart_read_line(interpreter,
                            input,
                            (int)sizeof(input),
                            false);
        if (input_length <= 0)
        {
            continue;
        }

        if (basic_console_process(&interpreter, input, &status))
        {
            if (basic_interrupt_requested)
            {
                printf("Break\r\n");
                basic_interrupt_requested = false;
                status = MB_FUNC_OK;
            }
            else if (MB_FUNC_OK != status)
            {
                printf("Command failed: %d\r\n", status);
                status = MB_FUNC_OK;
            }
            continue;
        }

        status = mb_reset(&interpreter, false, false);
        if (MB_FUNC_OK == status)
        {
            status = mb_load_string(interpreter, input, true);
        }
        if (MB_FUNC_OK == status)
        {
            status = mb_run(interpreter, true);
        }
        if (basic_interrupt_requested)
        {
            printf("Break\r\n");
            basic_interrupt_requested = false;
        }
        else if (MB_FUNC_OK != status)
        {
            printf("Command failed: %d\r\n", status);
        }
    }

    vTaskDelete(NULL);
}

BaseType_t basic_task_create(void)
{
    return xTaskCreate(basic_task,
                       "BASIC",
                       BASIC_TASK_STACK_SIZE,
                       NULL,
                       BASIC_TASK_PRIORITY,
                       NULL);
}
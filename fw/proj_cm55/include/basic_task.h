/******************************************************************************
 * File: basic_task.h
 * Description: Public interface for the CM55 BASIC interpreter task.
 ******************************************************************************/

#ifndef BASIC_TASK_H
#define BASIC_TASK_H

#include "FreeRTOS.h"

BaseType_t basic_task_create(void);

#endif /* BASIC_TASK_H */
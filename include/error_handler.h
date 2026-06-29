#pragma once
#include "main.h"
void     ErrorHandler_Init(void);
void     ErrorHandler_Report(ErrorCode_t code, const char *source);
void     ErrorHandler_StackOverflow(const char *taskName);
void     ErrorHandler_Fatal(ErrorCode_t code);
uint32_t ErrorHandler_GetTotalErrors(void);

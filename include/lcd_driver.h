#pragma once
#include "main.h"

HAL_StatusTypeDef LCD_Init(void);
HAL_StatusTypeDef LCD_Command(uint8_t cmd);
HAL_StatusTypeDef LCD_Data(uint8_t data);
HAL_StatusTypeDef LCD_Clear(void);
HAL_StatusTypeDef LCD_SetCursor(uint8_t row, uint8_t col);
HAL_StatusTypeDef LCD_Print(const char *str);
HAL_StatusTypeDef LCD_PrintN(const char *str, uint8_t max_chars);

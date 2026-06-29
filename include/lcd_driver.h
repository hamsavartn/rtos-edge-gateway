#pragma once
#include "main.h"
void LCD_Driver_Init(void);
void LCD_Command(uint8_t cmd);
void LCD_Data(uint8_t data);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(const char *str);
void LCD_PrintN(const char *str, uint8_t max_chars);

#pragma once
#include "main.h"
#include <string.h>
void              UART_Driver_Init(void);
void              UART_Driver_Transmit(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef UART_Driver_Receive(uint8_t *buf, uint16_t len, uint32_t timeout_ms);

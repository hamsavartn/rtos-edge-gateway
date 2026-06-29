#pragma once
#include "main.h"

typedef enum {
    UART_SENS_OK = 0,
    UART_SENS_TIMEOUT,
    UART_SENS_OVERFLOW,
    UART_SENS_CRC_ERROR
} UARTSensor_Status_t;

void                UARTSensor_Driver_Init(void);
UARTSensor_Status_t UARTSensor_ReadFrame(uint8_t *payload_out, uint8_t *len_out);

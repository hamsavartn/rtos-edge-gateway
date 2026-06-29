#pragma once
#include "main.h"
void              ADC_Driver_Init(void);
HAL_StatusTypeDef ADC_Driver_Read(uint16_t *out_raw);

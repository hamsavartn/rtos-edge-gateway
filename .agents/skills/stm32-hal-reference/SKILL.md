---
name: stm32-hal-reference
description: Quick reference for STM32F1xx HAL functions to prevent hallucination.
---
# STM32 HAL Reference
- **GPIO**: `HAL_GPIO_WritePin(GPIOx, GPIO_Pin, PinState)`, `HAL_GPIO_TogglePin`, `HAL_GPIO_ReadPin`.
- **UART**: `HAL_UART_Transmit(&huartx, pData, Size, Timeout)`, `HAL_UART_Receive`.
- **I2C**: `HAL_I2C_Master_Transmit(&hi2cx, DevAddress, pData, Size, Timeout)`.
- **ADC**: `HAL_ADC_Start(&hadc1)`, `HAL_ADC_PollForConversion`, `HAL_ADC_GetValue`.
Always check return values against `HAL_OK`.

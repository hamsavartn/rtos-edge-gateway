---
name: hardware-interaction-abstraction
description: Standardizes the use of STM32 HAL vs Low-Layer (LL) APIs.
---
# Hardware Interaction & HAL Abstraction
1. **Initialization**: Use the STM32Cube HAL (Hardware Abstraction Layer) for peripheral initialization (e.g., `HAL_ADC_Init`, `HAL_UART_Init`) as it improves readability and portability.
2. **Critical Paths / High-Freq ISRs**: For extremely latency-sensitive operations (e.g., 100kHz+ interrupts or microsecond delays like DHT22 bit-banging), avoid HAL overhead. Use Low-Layer (LL) APIs or direct register manipulation (e.g., `GPIOA->BSRR = GPIO_PIN_0`).
3. **Blocking vs Non-Blocking**: Avoid blocking HAL calls with large timeouts (e.g., `HAL_I2C_Master_Transmit` with 1000ms timeout) inside RTOS tasks; prefer DMA or IT (Interrupt) based HAL functions combined with RTOS Semaphores.

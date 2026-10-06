---
name: freertos-tickless-idle
description: Implementing configUSE_TICKLESS_IDLE on STM32F1xx for power optimization.
---
# FreeRTOS Tickless Idle
1. **Configuration**: Set `configUSE_TICKLESS_IDLE 1` in `FreeRTOSConfig.h`.
2. **LPTIM/RTC**: The default SysTick stops during STOP mode. You must configure the RTC or a Low Power Timer (LPTIM) to generate the tick interrupt to wake the MCU.
3. **Pre/Post Sleep Processing**: Define `configPRE_SLEEP_PROCESSING` to disable high-speed clocks (HSE/PLL) and drop to STOP mode. Define `configPOST_SLEEP_PROCESSING` to reconfigure the clock tree back to 72MHz upon wake.

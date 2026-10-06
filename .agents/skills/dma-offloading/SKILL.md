---
name: dma-offloading
description: Guidelines for utilizing Direct Memory Access (DMA) for ADC and UART to save CPU cycles.
---
# DMA Offloading
1. **ADC**: Configure ADC in continuous scan mode and link it to a DMA channel. Use `HAL_ADC_Start_DMA`. Process data in the `HAL_ADC_ConvCpltCallback` to send it to a queue, keeping the CPU completely free during sampling.
2. **UART TX/RX**: Use `HAL_UART_Transmit_DMA` instead of blocking calls for the log output. Use `HAL_UARTEx_ReceiveToIdle_DMA` for receiving framed variable-length sensor data, which fires an interrupt only when the line goes idle.

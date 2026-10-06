---
name: freertos-isr-thread-safety
description: Enforces ISR safety and Thread safety rules for FreeRTOS.
---
# FreeRTOS ISR & Thread Safety Enforcer
When interacting with FreeRTOS APIs:
1. **ISR Context**: If inside an Interrupt Service Routine (e.g., `HAL_UART_RxCpltCallback`), you MUST use the `...FromISR()` variants of FreeRTOS functions (e.g., `xQueueSendFromISR`, `xSemaphoreGiveFromISR`).
2. **Yielding in ISR**: Always pass `&xHigherPriorityTaskWoken` to the `...FromISR` function and call `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` at the end of the ISR if required.
3. **Mutexes**: Never use Mutexes inside an ISR.
4. **Thread Safety**: Protect shared resources across tasks using `xSemaphoreTake` and `xSemaphoreGive`. Prefer RAII wrappers (like a custom `LockGuard`) if available.

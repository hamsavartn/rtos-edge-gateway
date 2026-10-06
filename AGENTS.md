# Agent Guidelines for RTOS Edge Gateway

## 1. Zero-Heap Policy
This project enforces a STRICT ZERO-HEAP architecture.
- **NEVER** use `malloc`, `calloc`, `realloc`, `free`, `new`, or `delete`.
- All FreeRTOS primitives (Tasks, Queues, Mutexes, EventGroups) MUST be created using their `*Static` equivalents (e.g., `xTaskCreateStatic`, `xQueueCreateStatic`).
- All structs, buffers, and arrays must be statically allocated.

## 2. Task Health Monitoring (Watchdog)
A hardware Independent Watchdog (IWDG) is running, supported by a FreeRTOS Event Group (`xHealthGroupHandle`).
- Any newly created task MUST set its unique bit in `xHealthGroupHandle` at the end of its main `for(;;)` loop to prove it is not frozen.
- If a task fails to check in, the Watchdog task will intentionally starve the hardware IWDG, forcing a system reset.
- If you add a new task, update `ALL_TASKS_MASK` in `main.h`.

## 3. Hardware Interactions
- Prefer STM32 HAL for complex peripherals (I2C, UART, ADC).
- For simple GPIO toggling inside loops or high-speed operations, raw register access or Low-Layer (LL) is acceptable.
- Do not use blocking HAL calls inside tasks without a timeout (`HAL_MAX_DELAY` is forbidden). Always use finite timeouts.

## 4. FreeRTOS ISR Safety
- Any FreeRTOS API called from an Interrupt Service Routine MUST end in `FromISR`.
- Never block (delay, wait for semaphore) inside an ISR.

Follow these rules strictly to ensure the edge gateway remains fault-tolerant and stable for industrial environments.

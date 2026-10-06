---
name: freertos_concurrency_and_kernel_mechanics
description: Enforces FreeRTOS concurrency rules, context switching awareness, and mandates FromISR variants in interrupts.
---

# Real-Time OS Concurrency & Execution Mechanics

## Implementation Directives
- **Strict Constraint**: Never allow standard FreeRTOS APIs within an ISR; ALWAYS mandate `FromISR` variants (e.g., `xQueueSendFromISR`, `xSemaphoreGiveFromISR`).
- You must abide by exact API rules regarding task preemption, context switching, and state machines.
- Use the FreeRTOS Kernel Website (https://www.freertos.org/Documentation/RTOS_book.html) or the FreeRTOS GitHub repository as a reference for concurrency and execution mechanics when solving problems.

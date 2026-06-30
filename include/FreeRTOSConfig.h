/*
 * FreeRTOSConfig.h — Tuned for STM32F103C8T6 @ 72 MHz
 * ======================================================
 * Key design decisions:
 *   - configSUPPORT_STATIC_ALLOCATION  = 1  (static only)
 *   - configSUPPORT_DYNAMIC_ALLOCATION = 0  (NO heap)
 *   - configUSE_MALLOC_FAILED_HOOK     = 1  (catch violations)
 *   - configCHECK_FOR_STACK_OVERFLOW   = 2  (full pattern check)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ─── Scheduler ────────────────────────────────────────────────────────────────
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0

// ─── Clock ────────────────────────────────────────────────────────────────────
#define configCPU_CLOCK_HZ                      72000000UL
#define configTICK_RATE_HZ                      1000U       // 1 ms resolution

// ─── Task Priorities ──────────────────────────────────────────────────────────
#define configMAX_PRIORITIES                    5U
#define configMINIMAL_STACK_SIZE                128U        // words

// ─── Memory Policy: ZERO HEAP ────────────────────────────────────────────────
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0           // disabled — no heap
#define configTOTAL_HEAP_SIZE                   0U          // enforced zero

// ─── Safety Hooks ─────────────────────────────────────────────────────────────
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2           // full watermark pattern
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

// ─── Task & Queue Limits ──────────────────────────────────────────────────────
#define configMAX_TASK_NAME_LEN                 16U
#define configQUEUE_REGISTRY_SIZE               10U

// ─── Timer Task ───────────────────────────────────────────────────────────────
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                5U
#define configTIMER_TASK_STACK_DEPTH            256U

// ─── Mutexes & Semaphores ─────────────────────────────────────────────────────
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_BINARY_SEMAPHORES             1

// ─── Debug & Stats ────────────────────────────────────────────────────────────
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configGENERATE_RUN_TIME_STATS           0
#define configRECORD_STACK_HIGH_ADDRESS         1

// ─── Co-routines (unused) ────────────────────────────────────────────────────
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2U

// ─── Cortex-M3 Interrupt Priority Config ─────────────────────────────────────
// STM32F103 uses 4 priority bits → 0..15
// FreeRTOS syscall must be ≤ configMAX_SYSCALL_INTERRUPT_PRIORITY
#define configPRIO_BITS                         4U
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U

#define configKERNEL_INTERRUPT_PRIORITY     ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

// ─── Assert ───────────────────────────────────────────────────────────────────
extern void vAssertCalled(const char *file, uint32_t line);
#define configASSERT(x) if ((x) == 0) vAssertCalled(__FILE__, __LINE__)

// ─── API Include Map ──────────────────────────────────────────────────────────
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  0
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetHandle               1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_xSemaphoreGetMutexHolder     1
#define INCLUDE_pcTaskGetName                1

#ifdef __cplusplus
}
#endif
#define configUSE_16_BIT_TICKS 0
#undef configSUPPORT_DYNAMIC_ALLOCATION
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE 2048

/**
 * diagnostics.cpp — Task 8: System Health & Telemetry Dashboard
 * ==============================================================
 * Runs at PRIORITY_SYSTEM. Every 10 seconds prints a full
 * system snapshot over UART:
 *   - Uptime
 *   - Packets routed per sensor
 *   - Queue fill levels
 *   - Stack high-watermarks for all tasks
 *   - Total error count
 *   - Heap usage (should always be 0 — validated here)
 */

#include "diagnostics.h"
#include "queue_manager.h"
#include "error_handler.h"
#include "uart_driver.h"
#include <stdio.h>
#include <string.h>

#define DIAG_INTERVAL_MS   10000U
#define DIAG_SEPARATOR     "════════════════════════════════════════\r\n"

static uint32_t s_snapshot_count = 0;

void Diagnostics_Init(void)
{
    s_snapshot_count = 0;
}

void vTaskDiagnostics(void *pvParams)
{
    (void)pvParams;
    char buf[128];

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(DIAG_INTERVAL_MS));

        s_snapshot_count++;
        uint32_t uptime = xTaskGetTickCount();

        // ── Header ───────────────────────────────────────────────────────────
        UART_Driver_Transmit((uint8_t*)DIAG_SEPARATOR, strlen(DIAG_SEPARATOR));

        snprintf(buf, sizeof(buf),
                 " DIAG SNAPSHOT #%lu | Uptime: %lu.%03lu s\r\n",
                 s_snapshot_count, uptime / 1000, uptime % 1000);
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        UART_Driver_Transmit((uint8_t*)DIAG_SEPARATOR, strlen(DIAG_SEPARATOR));

        // ── Queue Depths ─────────────────────────────────────────────────────
        snprintf(buf, sizeof(buf),
                 " QUEUES  ADC:%2u/%u  DHT22:%u/%u  MPU:%2u/%u  "
                 "UART:%u/%u  OUT:%2u/%u\r\n",
                 (unsigned)QueueManager_GetADCDepth(),   QUEUE_DEPTH_ADC,
                 (unsigned)QueueManager_GetDHT22Depth(), QUEUE_DEPTH_DHT22,
                 (unsigned)QueueManager_GetMPUDepth(),   QUEUE_DEPTH_MPU6050,
                 (unsigned)QueueManager_GetUARTDepth(),  QUEUE_DEPTH_UART,
                 (unsigned)QueueManager_GetOutputDepth(),QUEUE_DEPTH_OUTPUT);
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        // ── Packets Routed ────────────────────────────────────────────────────
        snprintf(buf, sizeof(buf),
                 " ROUTED  Total: %lu packets\r\n",
                 QueueManager_GetTotalRouted());
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        // ── Error Count ───────────────────────────────────────────────────────
        snprintf(buf, sizeof(buf),
                 " ERRORS  Total: %lu\r\n",
                 ErrorHandler_GetTotalErrors());
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        // ── Stack Watermarks ──────────────────────────────────────────────────
        extern TaskHandle_t hADCTask, hDHT22Task, hMPU6050Task;
        extern TaskHandle_t hUARTSensTask, hQueueMgrTask, hOutputTask, hErrorTask;

        snprintf(buf, sizeof(buf),
                 " STACKS (min free words):\r\n"
                 "   ADC:%3u  DHT22:%3u  MPU:%3u  UART:%3u\r\n",
                 (unsigned)uxTaskGetStackHighWaterMark(hADCTask),
                 (unsigned)uxTaskGetStackHighWaterMark(hDHT22Task),
                 (unsigned)uxTaskGetStackHighWaterMark(hMPU6050Task),
                 (unsigned)uxTaskGetStackHighWaterMark(hUARTSensTask));
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        snprintf(buf, sizeof(buf),
                 "   QMgr:%3u  Out:%3u  Err:%3u  Diag:---\r\n",
                 (unsigned)uxTaskGetStackHighWaterMark(hQueueMgrTask),
                 (unsigned)uxTaskGetStackHighWaterMark(hOutputTask),
                 (unsigned)uxTaskGetStackHighWaterMark(hErrorTask));
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        // ── Heap Validation (must always be 0) ───────────────────────────────
        size_t heap_free      = xPortGetFreeHeapSize();
        size_t heap_min_ever  = xPortGetMinimumEverFreeHeapSize();

        snprintf(buf, sizeof(buf),
                 " HEAP    Free: %u B | MinEver: %u B | "
                 "Alloc: %s\r\n",
                 (unsigned)heap_free,
                 (unsigned)heap_min_ever,
                 (heap_free == heap_min_ever) ? "NONE [OK]" : "DETECTED [WARN]");
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

        // If heap was ever used, report it as an error
        if (heap_free != heap_min_ever) {
            ErrorHandler_Report(ERR_HEAP_ALLOC_ATTEMPTED, "DIAG");
        }

        UART_Driver_Transmit((uint8_t*)DIAG_SEPARATOR, strlen(DIAG_SEPARATOR));
    }
}

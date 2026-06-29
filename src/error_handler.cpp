/**
 * error_handler.cpp — Fault Detection & System Protection
 * ========================================================
 * Task 7 runs at highest priority (PRIORITY_SYSTEM).
 * Monitors: stack high watermarks, error log ring buffer,
 * queue saturation alerts, fatal fault handlers.
 *
 * Error log is a static ring buffer — zero heap use.
 */

#include "error_handler.h"
#include "uart_driver.h"
#include <stdio.h>
#include <string.h>

// ─── Error Log Ring Buffer ────────────────────────────────────────────────────
#define ERROR_LOG_SIZE   32U

typedef struct {
    uint32_t    timestamp_ms;
    ErrorCode_t code;
    char        source[16];
} ErrorEntry_t;

static ErrorEntry_t  s_error_log[ERROR_LOG_SIZE];
static uint8_t       s_log_head   = 0;
static uint8_t       s_log_count  = 0;
static uint32_t      s_total_errors = 0;

// Mutex to protect error log from concurrent access
static StaticSemaphore_t  xErrorMutexBuffer;
static SemaphoreHandle_t  hErrorMutex;

// ─── Init ─────────────────────────────────────────────────────────────────────
void ErrorHandler_Init(void)
{
    memset(s_error_log, 0, sizeof(s_error_log));
    hErrorMutex = xSemaphoreCreateMutexStatic(&xErrorMutexBuffer);
    configASSERT(hErrorMutex != nullptr);
}

// ─── Report (called from any task) ───────────────────────────────────────────
void ErrorHandler_Report(ErrorCode_t code, const char *source)
{
    if (xSemaphoreTake(hErrorMutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        ErrorEntry_t *entry = &s_error_log[s_log_head];
        entry->timestamp_ms = xTaskGetTickCount();
        entry->code         = code;
        strncpy(entry->source, source ? source : "UNKNOWN", sizeof(entry->source) - 1);
        entry->source[sizeof(entry->source) - 1] = '\0';

        s_log_head = (s_log_head + 1) % ERROR_LOG_SIZE;
        if (s_log_count < ERROR_LOG_SIZE) s_log_count++;
        s_total_errors++;

        xSemaphoreGive(hErrorMutex);
    }

    // Immediate UART print for critical codes
    if (code >= ERR_STACK_OVERFLOW)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[ERR] Code=0x%02X Src=%s T=%lu\r\n",
                 code, source ? source : "?", xTaskGetTickCount());
        UART_Driver_Transmit((uint8_t*)buf, strlen(buf));
    }
}

// ─── Stack Overflow (called from hook) ───────────────────────────────────────
void ErrorHandler_StackOverflow(const char *taskName)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "[FATAL] STACK OVERFLOW: %s\r\n", taskName);
    UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

    // Blink LED rapidly to signal fatal fault
    for (;;) {
        HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
        HAL_Delay(100);
    }
}

// ─── Fatal (unrecoverable) ────────────────────────────────────────────────────
void ErrorHandler_Fatal(ErrorCode_t code)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "[FATAL] Code=0x%02X — System halted\r\n", code);
    UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

    taskDISABLE_INTERRUPTS();
    for (;;) {
        HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
        HAL_Delay(200);
    }
}

// ─── Accessors ────────────────────────────────────────────────────────────────
uint32_t ErrorHandler_GetTotalErrors(void) { return s_total_errors; }

// ─── Task 7: Error Handler ────────────────────────────────────────────────────
// Periodically flushes error log to UART and monitors stack watermarks.
void vTaskErrorHandler(void *pvParams)
{
    (void)pvParams;
    char          buf[80];
    uint8_t       last_printed = 0;

    for (;;)
    {
        // ── Print any new error log entries ──────────────────────────────────
        if (xSemaphoreTake(hErrorMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            while (last_printed != s_log_head)
            {
                ErrorEntry_t *e = &s_error_log[last_printed];
                snprintf(buf, sizeof(buf),
                         "[ERR_LOG] T=%8lu Code=0x%02X Src=%-12s\r\n",
                         e->timestamp_ms, e->code, e->source);
                UART_Driver_Transmit((uint8_t*)buf, strlen(buf));
                last_printed = (last_printed + 1) % ERROR_LOG_SIZE;
            }
            xSemaphoreGive(hErrorMutex);
        }

        // ── Check Stack Watermarks every 5 seconds ────────────────────────
        vTaskDelay(pdMS_TO_TICKS(5000));

        extern TaskHandle_t hADCTask, hDHT22Task, hMPU6050Task;
        extern TaskHandle_t hUARTSensTask, hQueueMgrTask, hOutputTask, hDiagTask;

        const struct { TaskHandle_t h; const char *name; } tasks[] = {
            { hADCTask,      "ADC_Sens"  },
            { hDHT22Task,    "DHT22"     },
            { hMPU6050Task,  "MPU6050"   },
            { hUARTSensTask, "UART_Sens" },
            { hQueueMgrTask, "Queue_Mgr" },
            { hOutputTask,   "Output"    },
            { hDiagTask,     "Diag"      },
        };

        for (uint8_t i = 0; i < 7; i++) {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(tasks[i].h);
            if (hwm < 32U) {
                // Less than 32 words remaining — critical warning
                snprintf(buf, sizeof(buf),
                         "[WARN] LOW STACK: %s HWM=%u words\r\n",
                         tasks[i].name, (unsigned)hwm);
                UART_Driver_Transmit((uint8_t*)buf, strlen(buf));
                ErrorHandler_Report(ERR_STACK_OVERFLOW, tasks[i].name);
            }
        }
    }
}

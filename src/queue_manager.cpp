/**
 * queue_manager.cpp — Static Thread-Safe Queue System
 * =====================================================
 * Central nervous system of the gateway.
 * All 5 queues use StaticQueue_t — ZERO heap allocation.
 *
 * Design: One queue per sensor, one output queue.
 * QueueManager task drains sensor queues and routes
 * formatted packets to the output queue.
 */

#include "queue_manager.h"
#include "error_handler.h"
#include "uart_driver.h"
#include <stdio.h>
#include <string.h>

// ─── Static Queue Storage ─────────────────────────────────────────────────────
// Each queue has a StaticQueue_t control block + a raw storage buffer.
// No malloc, no new — all in .bss section.

// ADC Queue
static StaticQueue_t   xADCQueueStruct;
static uint8_t         xADCQueueStorage[QUEUE_DEPTH_ADC * sizeof(ADCPacket_t)];
QueueHandle_t          hADCQueue;

// DHT22 Queue
static StaticQueue_t   xDHT22QueueStruct;
static uint8_t         xDHT22QueueStorage[QUEUE_DEPTH_DHT22 * sizeof(DHT22Packet_t)];
QueueHandle_t          hDHT22Queue;

// MPU6050 Queue
static StaticQueue_t   xMPU6050QueueStruct;
static uint8_t         xMPU6050QueueStorage[QUEUE_DEPTH_MPU6050 * sizeof(MPU6050Packet_t)];
QueueHandle_t          hMPU6050Queue;

// UART Sensor Queue
static StaticQueue_t   xUARTSensQueueStruct;
static uint8_t         xUARTSensQueueStorage[QUEUE_DEPTH_UART * sizeof(UARTSensorPacket_t)];
QueueHandle_t          hUARTSensQueue;

// Output Queue (feeds OutputManager task)
static StaticQueue_t   xOutputQueueStruct;
static uint8_t         xOutputQueueStorage[QUEUE_DEPTH_OUTPUT * sizeof(OutputPacket_t)];
QueueHandle_t          hOutputQueue;

// ─── Packet Router State ──────────────────────────────────────────────────────
static uint32_t s_total_routed = 0;

// ─── Init ─────────────────────────────────────────────────────────────────────
void QueueManager_Init(void)
{
    hADCQueue = xQueueCreateStatic(
        QUEUE_DEPTH_ADC,
        sizeof(ADCPacket_t),
        xADCQueueStorage,
        &xADCQueueStruct
    );

    hDHT22Queue = xQueueCreateStatic(
        QUEUE_DEPTH_DHT22,
        sizeof(DHT22Packet_t),
        xDHT22QueueStorage,
        &xDHT22QueueStruct
    );

    hMPU6050Queue = xQueueCreateStatic(
        QUEUE_DEPTH_MPU6050,
        sizeof(MPU6050Packet_t),
        xMPU6050QueueStorage,
        &xMPU6050QueueStruct
    );

    hUARTSensQueue = xQueueCreateStatic(
        QUEUE_DEPTH_UART,
        sizeof(UARTSensorPacket_t),
        xUARTSensQueueStorage,
        &xUARTSensQueueStruct
    );

    hOutputQueue = xQueueCreateStatic(
        QUEUE_DEPTH_OUTPUT,
        sizeof(OutputPacket_t),
        xOutputQueueStorage,
        &xOutputQueueStruct
    );

    // Verify all queues created (null = critical init failure)
    configASSERT(hADCQueue      != nullptr);
    configASSERT(hDHT22Queue    != nullptr);
    configASSERT(hMPU6050Queue  != nullptr);
    configASSERT(hUARTSensQueue != nullptr);
    configASSERT(hOutputQueue   != nullptr);
}

// ─── Queue Manager Task ───────────────────────────────────────────────────────
// Runs at PRIORITY_MANAGER.
// Polls all sensor queues in round-robin; formats and pushes to OutputQueue.
void vTaskQueueManager(void *pvParams)
{
    (void)pvParams;

    ADCPacket_t        adcPkt;
    DHT22Packet_t      dhtPkt;
    MPU6050Packet_t    mpuPkt;
    UARTSensorPacket_t uartPkt;
    OutputPacket_t     outPkt;

    char buf[34];

    for (;;)
    {
        // ── Route ADC packets ────────────────────────────────────────────────
        while (xQueueReceive(hADCQueue, &adcPkt, 0) == pdTRUE)
        {
            if (adcPkt.error != ERR_NONE) {
                ErrorHandler_Report(adcPkt.error, "ADC");
                continue;
            }
            outPkt.timestamp_ms = adcPkt.timestamp_ms;
            outPkt.source_id    = 0x01;
            snprintf((char*)outPkt.line1, LCD_COLS + 1, "ADC:%4umV      ", adcPkt.voltage_mv);
            snprintf((char*)outPkt.line2, LCD_COLS + 1, "Raw: %4u        ", adcPkt.raw_value);
            xQueueSend(hOutputQueue, &outPkt, 0);
            s_total_routed++;
        }

        // ── Route DHT22 packets ──────────────────────────────────────────────
        while (xQueueReceive(hDHT22Queue, &dhtPkt, 0) == pdTRUE)
        {
            if (dhtPkt.error != ERR_NONE) {
                ErrorHandler_Report(dhtPkt.error, "DHT22");
                continue;
            }
            outPkt.timestamp_ms = dhtPkt.timestamp_ms;
            outPkt.source_id    = 0x02;
            snprintf((char*)outPkt.line1, LCD_COLS + 1, "T:%3d.%dC        ",
                     dhtPkt.temperature_x10 / 10, dhtPkt.temperature_x10 % 10);
            snprintf((char*)outPkt.line2, LCD_COLS + 1, "H:%3d.%d%%RH     ",
                     dhtPkt.humidity_x10 / 10, dhtPkt.humidity_x10 % 10);
            xQueueSend(hOutputQueue, &outPkt, 0);
            s_total_routed++;
        }

        // ── Route MPU6050 packets ────────────────────────────────────────────
        while (xQueueReceive(hMPU6050Queue, &mpuPkt, 0) == pdTRUE)
        {
            if (mpuPkt.error != ERR_NONE) {
                ErrorHandler_Report(mpuPkt.error, "MPU6050");
                continue;
            }
            outPkt.timestamp_ms = mpuPkt.timestamp_ms;
            outPkt.source_id    = 0x03;
            snprintf((char*)outPkt.line1, LCD_COLS + 1, "AX:%5d AY:%5d",
                     mpuPkt.accel_x, mpuPkt.accel_y);
            snprintf((char*)outPkt.line2, LCD_COLS + 1, "GX:%5d GY:%5d",
                     mpuPkt.gyro_x, mpuPkt.gyro_y);
            xQueueSend(hOutputQueue, &outPkt, 0);
            s_total_routed++;
        }

        // ── Route UART Sensor packets ────────────────────────────────────────
        while (xQueueReceive(hUARTSensQueue, &uartPkt, 0) == pdTRUE)
        {
            if (uartPkt.error != ERR_NONE) {
                ErrorHandler_Report(uartPkt.error, "UART_SENS");
                continue;
            }
            outPkt.timestamp_ms = uartPkt.timestamp_ms;
            outPkt.source_id    = 0x04;
            snprintf((char*)outPkt.line1, LCD_COLS + 1, "UART[%2uB]       ", uartPkt.length);
            memset(outPkt.line2, ' ', LCD_COLS);
            uint8_t copy_len = (uartPkt.length < LCD_COLS) ? uartPkt.length : LCD_COLS;
            memcpy(outPkt.line2, uartPkt.payload, copy_len);
            outPkt.line2[LCD_COLS] = '\0';
            xQueueSend(hOutputQueue, &outPkt, 0);
            s_total_routed++;
        }

        // Yield for one tick — prevents starving lower-priority tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ─── Accessors (used by Diagnostics) ─────────────────────────────────────────
uint32_t QueueManager_GetTotalRouted(void)  { return s_total_routed; }
UBaseType_t QueueManager_GetADCDepth(void)  { return uxQueueMessagesWaiting(hADCQueue); }
UBaseType_t QueueManager_GetDHT22Depth(void){ return uxQueueMessagesWaiting(hDHT22Queue); }
UBaseType_t QueueManager_GetMPUDepth(void)  { return uxQueueMessagesWaiting(hMPU6050Queue); }
UBaseType_t QueueManager_GetUARTDepth(void) { return uxQueueMessagesWaiting(hUARTSensQueue); }
UBaseType_t QueueManager_GetOutputDepth(void){ return uxQueueMessagesWaiting(hOutputQueue); }

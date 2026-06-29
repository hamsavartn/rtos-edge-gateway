/**
 * freertos_tasks.cpp — All Sensor Task Implementations
 * =====================================================
 * Task 1: vTaskADCSensor     — PA0 analog read, 20 Hz
 * Task 2: vTaskDHT22Sensor   — PB0 1-wire, 0.5 Hz
 * Task 3: vTaskMPU6050Sensor — I2C1, 100 Hz
 * Task 4: vTaskUARTSensor    — USART2, frame-based
 *
 * Each task:
 *   1. Samples its sensor
 *   2. Validates / error-checks the reading
 *   3. Stamps the packet with xTaskGetTickCount()
 *   4. Pushes to its dedicated static queue (non-blocking)
 *   5. Reports queue-full condition to ErrorHandler
 *   6. Delays for its sample period
 */

#include "freertos_tasks.h"
#include "queue_manager.h"
#include "error_handler.h"
#include "adc_driver.h"
#include "dht22_driver.h"
#include "mpu6050_driver.h"
#include "uart_sensor_driver.h"
#include "uart_driver.h"
#include "lcd_driver.h"
#include <stdio.h>
#include <string.h>

// ═════════════════════════════════════════════════════════════════════════════
// TASK 1 — ADC Sensor (PA0, 12-bit, 20 Hz)
// ═════════════════════════════════════════════════════════════════════════════
void vTaskADCSensor(void *pvParams)
{
    (void)pvParams;
    ADCPacket_t pkt;
    TickType_t  xLastWake = xTaskGetTickCount();

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;

        if (ADC_Driver_Read(&pkt.raw_value) != HAL_OK) {
            pkt.error      = ERR_ADC_NOT_READY;
            pkt.raw_value  = 0;
            pkt.voltage_mv = 0;
        } else {
            // Convert raw 12-bit to millivolts: mv = (raw / 4095) * 3300
            pkt.voltage_mv = (uint16_t)(((uint32_t)pkt.raw_value * ADC_VREF_MV) / 4095U);
        }

        // Non-blocking send — drop if full and report error
        if (xQueueSend(hADCQueue, &pkt, 0) != pdTRUE) {
            ErrorHandler_Report(ERR_QUEUE_FULL, "ADC_Q");
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_ADC_MS));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 2 — DHT22 Sensor (PB0, 1-Wire, 0.5 Hz)
// ═════════════════════════════════════════════════════════════════════════════
void vTaskDHT22Sensor(void *pvParams)
{
    (void)pvParams;
    DHT22Packet_t pkt;
    DHT22_Data_t  raw;
    TickType_t    xLastWake = xTaskGetTickCount();

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;

        DHT22_Status_t status = DHT22_Read(&raw);

        switch (status) {
            case DHT22_OK:
                pkt.temperature_x10 = raw.temperature_x10;
                pkt.humidity_x10    = raw.humidity_x10;
                break;
            case DHT22_CHECKSUM_ERROR:
                pkt.error = ERR_DHT22_CHECKSUM;
                pkt.temperature_x10 = 0;
                pkt.humidity_x10    = 0;
                break;
            case DHT22_TIMEOUT:
                pkt.error = ERR_SENSOR_TIMEOUT;
                pkt.temperature_x10 = 0;
                pkt.humidity_x10    = 0;
                break;
            default:
                pkt.error = ERR_SENSOR_TIMEOUT;
                break;
        }

        if (xQueueSend(hDHT22Queue, &pkt, 0) != pdTRUE) {
            ErrorHandler_Report(ERR_QUEUE_FULL, "DHT22_Q");
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_DHT22_MS));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 3 — MPU6050 Sensor (I2C1, 100 Hz)
// ═════════════════════════════════════════════════════════════════════════════
void vTaskMPU6050Sensor(void *pvParams)
{
    (void)pvParams;
    MPU6050Packet_t  pkt;
    MPU6050_RawData_t raw;
    TickType_t        xLastWake = xTaskGetTickCount();

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;

        if (MPU6050_ReadAll(&raw) != HAL_OK) {
            pkt.error   = ERR_I2C_NACK;
            memset(&pkt.accel_x, 0, sizeof(int16_t) * 6);
            pkt.temperature_x10 = 0;
        } else {
            pkt.accel_x         = raw.accel_x;
            pkt.accel_y         = raw.accel_y;
            pkt.accel_z         = raw.accel_z;
            pkt.gyro_x          = raw.gyro_x;
            pkt.gyro_y          = raw.gyro_y;
            pkt.gyro_z          = raw.gyro_z;
            // MPU6050 temp formula: Temp(°C) = raw/340 + 36.53 → *10 for fixed-point
            pkt.temperature_x10 = (int16_t)((raw.temp_raw * 10) / 340 + 365);
        }

        if (xQueueSend(hMPU6050Queue, &pkt, 0) != pdTRUE) {
            ErrorHandler_Report(ERR_QUEUE_FULL, "MPU_Q");
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MPU6050_MS));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 4 — UART Sensor (USART2, frame-based, 10 Hz poll)
// ═════════════════════════════════════════════════════════════════════════════
// Protocol: [0xAA][LEN][DATA...][CHECKSUM]
// Frame max payload: 28 bytes
void vTaskUARTSensor(void *pvParams)
{
    (void)pvParams;
    UARTSensorPacket_t pkt;
    TickType_t         xLastWake = xTaskGetTickCount();

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;
        pkt.length       = 0;
        memset(pkt.payload, 0, sizeof(pkt.payload));

        UARTSensor_Status_t status = UARTSensor_ReadFrame(pkt.payload, &pkt.length);

        if (status == UART_SENS_TIMEOUT) {
            // No data available — skip this cycle, no error
            vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_UART_MS));
            continue;
        } else if (status == UART_SENS_OVERFLOW) {
            pkt.error = ERR_UART_OVERFLOW;
        } else if (status == UART_SENS_CRC_ERROR) {
            pkt.error = ERR_SENSOR_CRC;
        }

        if (xQueueSend(hUARTSensQueue, &pkt, 0) != pdTRUE) {
            ErrorHandler_Report(ERR_QUEUE_FULL, "UART_SENS_Q");
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_UART_MS));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 6 — Output Manager (UART log + LCD)
// ═════════════════════════════════════════════════════════════════════════════
void vTaskOutputManager(void *pvParams)
{
    (void)pvParams;
    OutputPacket_t pkt;
    char           uart_buf[64];

    for (;;)
    {
        // Block on output queue — up to 100 ms wait
        if (xQueueReceive(hOutputQueue, &pkt, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // ── UART Log ────────────────────────────────────────────────────
            snprintf(uart_buf, sizeof(uart_buf),
                     "[%8lu] SRC=0x%02X | %s | %s\r\n",
                     pkt.timestamp_ms,
                     pkt.source_id,
                     (char*)pkt.line1,
                     (char*)pkt.line2);
            UART_Driver_Transmit((uint8_t*)uart_buf, strlen(uart_buf));

            // ── LCD Update ──────────────────────────────────────────────────
            LCD_Clear();
            LCD_SetCursor(0, 0);
            LCD_Print((char*)pkt.line1);
            LCD_SetCursor(1, 0);
            LCD_Print((char*)pkt.line2);

            // Blink status LED on successful output
            HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
        }
    }
}

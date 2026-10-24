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
    UART_Driver_Print("[T] adc\r\n");

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

        xEventGroupSetBits(xHealthGroupHandle, TASK_ADC_BIT);
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
    UART_Driver_Print("[T] dht22\r\n");

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

        xEventGroupSetBits(xHealthGroupHandle, TASK_DHT22_BIT);
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
    UART_Driver_Print("[T] mpu\r\n");

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;

        if (MPU6050_ReadAll(&raw) != HAL_OK) {
            pkt.error   = ERR_I2C_NACK;
            memset(&pkt.accel_x, 0, sizeof(int16_t) * 6);
            pkt.temperature_x10 = 0;
            MPU6050_Recover();
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

        xEventGroupSetBits(xHealthGroupHandle, TASK_MPU6050_BIT);
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
    UART_Driver_Print("[T] uarts\r\n");

    for (;;)
    {
        pkt.timestamp_ms = xTaskGetTickCount();
        pkt.error        = ERR_NONE;
        pkt.length       = 0;
        memset(pkt.payload, 0, sizeof(pkt.payload));

        UARTSensor_Status_t status = UARTSensor_ReadFrame(pkt.payload, &pkt.length);

        if (status == UART_SENS_TIMEOUT) {
            // No data available — skip this cycle, no error
            xEventGroupSetBits(xHealthGroupHandle, TASK_UART_SENS_BIT);
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

        xEventGroupSetBits(xHealthGroupHandle, TASK_UART_SENS_BIT);
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_UART_MS));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 6 — Output Manager (UART log + LCD)
// ═════════════════════════════════════════════════════════════════════════════
// Full LCD bring-up: init + clear + boot splash. Returns true when the panel
// acknowledges every step, false when the bus or the PCF8574 is unreachable.
static bool try_lcd_init(void)
{
    HAL_StatusTypeDef st = LCD_Init();
    if (st == HAL_OK) st = LCD_Clear();
    if (st == HAL_OK) st = LCD_SetCursor(0, 0);
    if (st == HAL_OK) st = LCD_Print("Gateway Booting");
    if (st == HAL_OK) st = LCD_SetCursor(1, 0);
    if (st == HAL_OK) st = LCD_Print("Waiting for data");
    return st == HAL_OK;
}

void vTaskOutputManager(void *pvParams)
{
    (void)pvParams;
    OutputPacket_t pkt;
    char           uart_buf[64];
    HAL_StatusTypeDef lcdStatus;
    bool           lcd_ok = false;
    bool           init_fail_logged = false;
    uint32_t       last_lcd_update = 0;
    UART_Driver_Print("[T] output\r\n");

    // Attempt to initialize the LCD through the translation driver.
    // If the LCD stays unreachable, this task never sets TASK_OUTPUT_BIT, so
    // the Watchdog task will flag it (and IWDG resets the gateway on real HW).
    lcd_ok = try_lcd_init();

    for (;;)
    {
        // Block on output queue — up to 100 ms wait
        if (xQueueReceive(hOutputQueue, &pkt, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // ── UART Log ────────────────────────────────────────────────────
            snprintf(uart_buf, sizeof(uart_buf),
                     "[%8lu] SRC=0x%02X | %s | %s\r\n",
                     (unsigned long)pkt.timestamp_ms,
                     pkt.source_id,
                     (char*)pkt.line1,
                     (char*)pkt.line2);
            UART_Driver_Transmit((uint8_t*)uart_buf, strlen(uart_buf));

            // ── LCD Update (throttled to 2 Hz to prevent flickering) ────────
            if (pkt.timestamp_ms - last_lcd_update > 500) {
                last_lcd_update = pkt.timestamp_ms;

                if (!lcd_ok) {
                    // Retry initialization at most once per throttle window so
                    // a transient I2C glitch (or newly plugged LCD) recovers.
                    lcd_ok = try_lcd_init();
                    if (!lcd_ok && !init_fail_logged) {
                        ErrorHandler_Report(ERR_I2C_NACK, "LCD_INIT");
                        init_fail_logged = true;   // once per failure episode
                    }
                    if (lcd_ok) init_fail_logged = false;
                }

                if (lcd_ok) {
                    lcdStatus = LCD_Clear();
                    if (lcdStatus == HAL_OK) {
                        lcdStatus = LCD_SetCursor(0, 0);
                        if (lcdStatus == HAL_OK) lcdStatus = LCD_Print((char*)pkt.line1);
                        if (lcdStatus == HAL_OK) lcdStatus = LCD_SetCursor(1, 0);
                        if (lcdStatus == HAL_OK) lcdStatus = LCD_Print((char*)pkt.line2);
                    }
                    if (lcdStatus != HAL_OK) {
                        lcd_ok = false; // Bus lost mid-frame — re-init next window
                        ErrorHandler_Report(ERR_I2C_NACK, "LCD_WRITE");
                    }
                }
            }

            // Blink status LED on successful output
            HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);

            /*
             * Health reporting: this task is demonstrably alive whenever it
             * reaches this point (UART log flushed, queue drained). A dead LCD
             * is an HMI fault only — it must NOT starve the whole gateway's
             * watchdog and kill telemetry. LCD failures are reported to the
             * ErrorHandler above and retried each throttle window instead.
             */
            xEventGroupSetBits(xHealthGroupHandle, TASK_OUTPUT_BIT);
        } else {
            // No data received within timeout, but task is still healthy.
            xEventGroupSetBits(xHealthGroupHandle, TASK_OUTPUT_BIT);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 9 — Watchdog (Highest Priority)
// ═════════════════════════════════════════════════════════════════════════════
void vTaskWatchdog(void *pvParams)
{
    (void)pvParams;
    UART_Driver_Print("[T] watchdog\r\n");
    for (;;)
    {
        // Wait for all tasks to check in. The window MUST be longer than the
        // DHT22 task's check-in period (2 s — the DHT22's minimum conversion
        // interval), otherwise a healthy system trips the watchdog forever.
        EventBits_t uxBits = xEventGroupWaitBits(
            xHealthGroupHandle,
            ALL_TASKS_MASK,
            pdTRUE, // Clear bits on exit
            pdTRUE, // Wait for ALL bits
            pdMS_TO_TICKS(WATCHDOG_WINDOW_MS)
        );

        if ((uxBits & ALL_TASKS_MASK) == ALL_TASKS_MASK) {
            // All good, pet hardware watchdog (no-op unless IWDG is started)
            IWDG->KR = 0xAAAA;
        } else {
            // A task hung!
            uint8_t hung_tasks = ALL_TASKS_MASK ^ (uxBits & ALL_TASKS_MASK);
            char buf[64];
            snprintf(buf, sizeof(buf), "[FATAL] WATCHDOG TIMEOUT. Hung Mask: 0x%02X\r\n", hung_tasks);
            UART_Driver_Transmit((uint8_t*)buf, strlen(buf));

            ErrorHandler_Report(ERR_WATCHDOG_TIMEOUT, "WATCHDOG");

            // Highest-priority spin — starves the IWDG feeder so the hardware
            // watchdog resets the MCU. With IWDG disabled (Wokwi default) this
            // is a visible, logged halt instead of a silent one.
            for (;;) {
                HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
                for (volatile uint32_t i = 0; i < 400000; i++) { __NOP(); }
            }
        }
    }
}

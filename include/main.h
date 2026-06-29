/**
 * main.h — Project-wide Configuration & Constants
 * RTOS-Based Industrial Edge Gateway
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ─── STM32 HAL ───────────────────────────────────────────────────────────────
#include "stm32f1xx_hal.h"

// ─── FreeRTOS ─────────────────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

// ─── System Clock ─────────────────────────────────────────────────────────────
#define SYS_CLOCK_HZ         72000000UL
#define TICK_RATE_HZ         1000U       // 1 ms tick

// ─── Task Stack Sizes (words) ─────────────────────────────────────────────────
#define STACK_SIZE_SENSOR    256U
#define STACK_SIZE_MANAGER   384U
#define STACK_SIZE_SYSTEM    256U

// ─── Task Priorities ──────────────────────────────────────────────────────────
// Higher number = higher priority in FreeRTOS
#define PRIORITY_SYSTEM      (configMAX_PRIORITIES - 1)   // 4: Error/Diag
#define PRIORITY_MANAGER     (configMAX_PRIORITIES - 2)   // 3: Queue/Output
#define PRIORITY_SENSOR      (configMAX_PRIORITIES - 3)   // 2: All sensors

// ─── Queue Depths (static, pre-allocated) ────────────────────────────────────
#define QUEUE_DEPTH_ADC      16U
#define QUEUE_DEPTH_DHT22    8U
#define QUEUE_DEPTH_MPU6050  16U
#define QUEUE_DEPTH_UART     8U
#define QUEUE_DEPTH_OUTPUT   32U

// ─── Sensor Sampling Periods (ms) ────────────────────────────────────────────
#define SAMPLE_PERIOD_ADC_MS       50U    // 20 Hz
#define SAMPLE_PERIOD_DHT22_MS     2000U  // 0.5 Hz (DHT22 max rate)
#define SAMPLE_PERIOD_MPU6050_MS   10U    // 100 Hz
#define SAMPLE_PERIOD_UART_MS      100U   // 10 Hz

// ─── UART Configuration ───────────────────────────────────────────────────────
#define UART_LOG_BAUDRATE    115200U  // USART1 — debug/log output
#define UART_SENS_BAUDRATE   9600U    // USART2 — UART sensor input
#define UART_TIMEOUT_MS      10U

// ─── I2C Addresses ────────────────────────────────────────────────────────────
#define MPU6050_I2C_ADDR     (0x68 << 1)  // AD0 pulled low
#define LCD_I2C_ADDR         (0x27 << 1)  // PCF8574 I2C backpack

// ─── LCD Dimensions ───────────────────────────────────────────────────────────
#define LCD_COLS             16U
#define LCD_ROWS             2U

// ─── ADC Configuration ───────────────────────────────────────────────────────
#define ADC_CHANNEL          ADC_CHANNEL_0  // PA0
#define ADC_RESOLUTION_BITS  12U
#define ADC_VREF_MV          3300U          // 3.3V reference

// ─── GPIO Pin Aliases ─────────────────────────────────────────────────────────
#define LED_STATUS_PORT      GPIOC
#define LED_STATUS_PIN       GPIO_PIN_13
#define DHT22_PORT           GPIOB
#define DHT22_PIN            GPIO_PIN_0

// ─── Error Codes ──────────────────────────────────────────────────────────────
typedef enum {
    ERR_NONE               = 0x00,
    ERR_QUEUE_FULL         = 0x01,
    ERR_QUEUE_EMPTY        = 0x02,
    ERR_SENSOR_TIMEOUT     = 0x03,
    ERR_SENSOR_CRC         = 0x04,
    ERR_I2C_NACK           = 0x05,
    ERR_UART_OVERFLOW      = 0x06,
    ERR_STACK_OVERFLOW     = 0x07,
    ERR_HEAP_ALLOC_ATTEMPTED = 0x08,
    ERR_SCHEDULER_FAILED   = 0x09,
    ERR_WATCHDOG_TIMEOUT   = 0x0A,
    ERR_ADC_NOT_READY      = 0x0B,
    ERR_DHT22_CHECKSUM     = 0x0C,
    ERR_MPU6050_INIT       = 0x0D,
} ErrorCode_t;

// ─── Sensor Data Packets (fixed-size, queue-safe) ────────────────────────────

typedef struct {
    uint32_t  timestamp_ms;
    uint16_t  raw_value;
    uint16_t  voltage_mv;
    ErrorCode_t error;
} ADCPacket_t;

typedef struct {
    uint32_t  timestamp_ms;
    int16_t   temperature_x10;  // °C * 10 (e.g., 253 = 25.3°C)
    uint16_t  humidity_x10;     // %RH * 10
    ErrorCode_t error;
} DHT22Packet_t;

typedef struct {
    uint32_t  timestamp_ms;
    int16_t   accel_x;          // Raw 16-bit, ±2g range
    int16_t   accel_y;
    int16_t   accel_z;
    int16_t   gyro_x;           // Raw 16-bit, ±250°/s range
    int16_t   gyro_y;
    int16_t   gyro_z;
    int16_t   temperature_x10;  // On-die temp * 10
    ErrorCode_t error;
} MPU6050Packet_t;

typedef struct {
    uint32_t  timestamp_ms;
    uint8_t   payload[32];      // Raw UART frame
    uint8_t   length;
    ErrorCode_t error;
} UARTSensorPacket_t;

typedef struct {
    uint32_t  timestamp_ms;
    uint8_t   source_id;        // Which sensor produced this
    uint8_t   line1[LCD_COLS + 1];
    uint8_t   line2[LCD_COLS + 1];
} OutputPacket_t;

// ─── Diagnostic Snapshot ──────────────────────────────────────────────────────
typedef struct {
    uint32_t uptime_ms;
    uint32_t total_packets_routed;
    uint32_t error_count;
    uint16_t adc_queue_depth;
    uint16_t dht22_queue_depth;
    uint16_t mpu6050_queue_depth;
    uint16_t uart_queue_depth;
    uint16_t output_queue_depth;
    UBaseType_t min_stack_adc;
    UBaseType_t min_stack_dht22;
    UBaseType_t min_stack_mpu6050;
    UBaseType_t min_stack_uart_sens;
    UBaseType_t min_stack_queue_mgr;
    UBaseType_t min_stack_output;
    UBaseType_t min_stack_error;
    UBaseType_t min_stack_diag;
} DiagSnapshot_t;

// ─── Task Function Declarations ───────────────────────────────────────────────
void vTaskADCSensor      (void *pvParams);
void vTaskDHT22Sensor    (void *pvParams);
void vTaskMPU6050Sensor  (void *pvParams);
void vTaskUARTSensor     (void *pvParams);
void vTaskQueueManager   (void *pvParams);
void vTaskOutputManager  (void *pvParams);
void vTaskErrorHandler   (void *pvParams);
void vTaskDiagnostics    (void *pvParams);

// ─── Global Task Handles (defined in main.cpp) ───────────────────────────────
extern TaskHandle_t hADCTask;
extern TaskHandle_t hDHT22Task;
extern TaskHandle_t hMPU6050Task;
extern TaskHandle_t hUARTSensTask;
extern TaskHandle_t hQueueMgrTask;
extern TaskHandle_t hOutputTask;
extern TaskHandle_t hErrorTask;
extern TaskHandle_t hDiagTask;

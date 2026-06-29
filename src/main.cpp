/**
 * ============================================================
 * RTOS-Based Industrial Edge Gateway
 * Real-Time Telemetry Manager — Main Entry Point
 * ============================================================
 * Target MCU : STM32F103C8T6 (ARM Cortex-M3, 72 MHz)
 * RTOS       : FreeRTOS (static allocation only)
 * Simulator  : Wokwi (Digital Twin)
 *
 * Architecture: 5-Task FreeRTOS System
 *   Task 1 — SensorADC     : Reads analog ADC sensor (PA0)
 *   Task 2 — SensorDHT22   : Reads DHT22 temp/humidity (PB0)
 *   Task 3 — SensorMPU6050 : Reads MPU6050 via I2C (PB6/PB7)
 *   Task 4 — UARTSensor    : Reads UART-based sensor (PA9/PA10)
 *   Task 5 — QueueManager  : Aggregates all sensor queues
 *   Task 6 — OutputManager : UART log + I2C LCD display
 *   Task 7 — ErrorHandler  : Monitors faults, stack overflows
 *   Task 8 — Diagnostics   : System health, uptime, CPU stats
 *
 * Memory Policy: ZERO dynamic allocation — all tasks, queues,
 * and buffers use static memory (StaticTask_t / StaticQueue_t).
 * ============================================================
 * Author : Hamsavarthan
 * Board  : STM32F103C8T6 (Blue Pill)
 * ============================================================
 */

#include "main.h"
#include "freertos_tasks.h"
#include "queue_manager.h"
#include "error_handler.h"
#include "diagnostics.h"
#include "uart_driver.h"
#include "lcd_driver.h"
#include "adc_driver.h"
#include "dht22_driver.h"
#include "mpu6050_driver.h"
#include "uart_sensor_driver.h"

// ─── FreeRTOS Static Task Buffers ────────────────────────────────────────────

// Task 1: ADC Sensor
static StaticTask_t  xADCTaskBuffer;
static StackType_t   xADCStack[STACK_SIZE_SENSOR];

// Task 2: DHT22 Sensor
static StaticTask_t  xDHT22TaskBuffer;
static StackType_t   xDHT22Stack[STACK_SIZE_SENSOR];

// Task 3: MPU6050 Sensor
static StaticTask_t  xMPU6050TaskBuffer;
static StackType_t   xMPU6050Stack[STACK_SIZE_SENSOR];

// Task 4: UART Sensor
static StaticTask_t  xUARTSensorTaskBuffer;
static StackType_t   xUARTSensorStack[STACK_SIZE_SENSOR];

// Task 5: Queue Manager
static StaticTask_t  xQueueMgrTaskBuffer;
static StackType_t   xQueueMgrStack[STACK_SIZE_MANAGER];

// Task 6: Output Manager
static StaticTask_t  xOutputTaskBuffer;
static StackType_t   xOutputStack[STACK_SIZE_MANAGER];

// Task 7: Error Handler
static StaticTask_t  xErrorTaskBuffer;
static StackType_t   xErrorStack[STACK_SIZE_SYSTEM];

// Task 8: Diagnostics
static StaticTask_t  xDiagTaskBuffer;
static StackType_t   xDiagStack[STACK_SIZE_SYSTEM];

// ─── Task Handles ─────────────────────────────────────────────────────────────
TaskHandle_t hADCTask       = nullptr;
TaskHandle_t hDHT22Task     = nullptr;
TaskHandle_t hMPU6050Task   = nullptr;
TaskHandle_t hUARTSensTask  = nullptr;
TaskHandle_t hQueueMgrTask  = nullptr;
TaskHandle_t hOutputTask    = nullptr;
TaskHandle_t hErrorTask     = nullptr;
TaskHandle_t hDiagTask      = nullptr;

// ─── System Init ──────────────────────────────────────────────────────────────
static void SystemClock_Config(void);
static void GPIO_Init(void);
static void Peripherals_Init(void);

int main(void)
{
    // HAL + Clock
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    Peripherals_Init();

    // Init subsystems (all static — no heap)
    QueueManager_Init();
    ErrorHandler_Init();
    Diagnostics_Init();

    // ── Spawn Task 1: ADC Sensor ─────────────────────────────────────────────
    hADCTask = xTaskCreateStatic(
        vTaskADCSensor,
        "ADC_Sensor",
        STACK_SIZE_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        xADCStack,
        &xADCTaskBuffer
    );

    // ── Spawn Task 2: DHT22 Sensor ───────────────────────────────────────────
    hDHT22Task = xTaskCreateStatic(
        vTaskDHT22Sensor,
        "DHT22_Sensor",
        STACK_SIZE_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        xDHT22Stack,
        &xDHT22TaskBuffer
    );

    // ── Spawn Task 3: MPU6050 Sensor ─────────────────────────────────────────
    hMPU6050Task = xTaskCreateStatic(
        vTaskMPU6050Sensor,
        "MPU6050_Sensor",
        STACK_SIZE_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        xMPU6050Stack,
        &xMPU6050TaskBuffer
    );

    // ── Spawn Task 4: UART Sensor ────────────────────────────────────────────
    hUARTSensTask = xTaskCreateStatic(
        vTaskUARTSensor,
        "UART_Sensor",
        STACK_SIZE_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        xUARTSensorStack,
        &xUARTSensorTaskBuffer
    );

    // ── Spawn Task 5: Queue Manager ──────────────────────────────────────────
    hQueueMgrTask = xTaskCreateStatic(
        vTaskQueueManager,
        "Queue_Manager",
        STACK_SIZE_MANAGER,
        nullptr,
        PRIORITY_MANAGER,
        xQueueMgrStack,
        &xQueueMgrTaskBuffer
    );

    // ── Spawn Task 6: Output Manager ─────────────────────────────────────────
    hOutputTask = xTaskCreateStatic(
        vTaskOutputManager,
        "Output_Manager",
        STACK_SIZE_MANAGER,
        nullptr,
        PRIORITY_MANAGER,
        xOutputStack,
        &xOutputTaskBuffer
    );

    // ── Spawn Task 7: Error Handler ──────────────────────────────────────────
    hErrorTask = xTaskCreateStatic(
        vTaskErrorHandler,
        "Error_Handler",
        STACK_SIZE_SYSTEM,
        nullptr,
        PRIORITY_SYSTEM,
        xErrorStack,
        &xErrorTaskBuffer
    );

    // ── Spawn Task 8: Diagnostics ────────────────────────────────────────────
    hDiagTask = xTaskCreateStatic(
        vTaskDiagnostics,
        "Diagnostics",
        STACK_SIZE_SYSTEM,
        nullptr,
        PRIORITY_SYSTEM,
        xDiagStack,
        &xDiagTaskBuffer
    );

    // Verify all tasks created (critical safety check)
    configASSERT(hADCTask      != nullptr);
    configASSERT(hDHT22Task    != nullptr);
    configASSERT(hMPU6050Task  != nullptr);
    configASSERT(hUARTSensTask != nullptr);
    configASSERT(hQueueMgrTask != nullptr);
    configASSERT(hOutputTask   != nullptr);
    configASSERT(hErrorTask    != nullptr);
    configASSERT(hDiagTask     != nullptr);

    // Hand control to FreeRTOS scheduler — never returns
    vTaskStartScheduler();

    // Should never reach here
    ErrorHandler_Fatal(ERR_SCHEDULER_FAILED);
    while (1) {}
}

// ─── Clock Configuration: 72 MHz via PLL ──────────────────────────────────────
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInit = {};
    RCC_ClkInitTypeDef RCC_ClkInit = {};

    RCC_OscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInit.HSEState       = RCC_HSE_ON;
    RCC_OscInit.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInit.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInit.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInit.PLL.PLLMUL     = RCC_PLL_MUL9; // 8 MHz * 9 = 72 MHz
    HAL_RCC_OscConfig(&RCC_OscInit);

    RCC_ClkInit.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInit.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInit.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInit.APB1CLKDivider = RCC_HCLK_DIV2; // APB1 max 36 MHz
    RCC_ClkInit.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInit, FLASH_LATENCY_2);
}

// ─── GPIO Init ────────────────────────────────────────────────────────────────
static void GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};

    // PC13 — Onboard LED (status indicator)
    gpio.Pin   = GPIO_PIN_13;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    // PB0 — DHT22 data pin (open-drain, pull-up)
    gpio.Pin   = GPIO_PIN_0;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

// ─── Peripherals Init ─────────────────────────────────────────────────────────
static void Peripherals_Init(void)
{
    UART_Driver_Init();       // USART1 — PA9 TX, PA10 RX (115200 baud)
    ADC_Driver_Init();        // ADC1   — PA0 Channel 0
    MPU6050_Init();           // I2C1   — PB6 SCL, PB7 SDA
    LCD_Driver_Init();        // I2C1   — shared with MPU6050 (0x27 addr)
    UARTSensor_Driver_Init(); // USART2 — PA2 TX, PA3 RX (9600 baud)
}

// ─── FreeRTOS Static Memory Hooks (required for configSUPPORT_STATIC_ALLOCATION) ──
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  xIdleStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = xIdleStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t  **ppxTimerTaskStackBuffer,
                                    uint32_t      *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t  xTimerStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = xTimerStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

// ─── Stack Overflow Hook ──────────────────────────────────────────────────────
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    ErrorHandler_StackOverflow(pcTaskName);
}

// ─── Malloc Fail Hook (catches any accidental heap use) ───────────────────────
void vApplicationMallocFailedHook(void)
{
    ErrorHandler_Fatal(ERR_HEAP_ALLOC_ATTEMPTED);
}

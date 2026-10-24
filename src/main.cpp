/**
 * ============================================================
 * RTOS-Based Industrial Edge Gateway
 * Real-Time Telemetry Manager — Main Entry Point
 * ============================================================
 * Target MCU : STM32F103C8T6 (ARM Cortex-M3, 72 MHz)
 * RTOS       : FreeRTOS (static allocation only)
 * Simulator  : Wokwi (Digital Twin)
 *
 * Architecture: 9-Task FreeRTOS System
 *   Task 1 — SensorADC     : Reads analog ADC sensor (PA0)
 *   Task 2 — SensorDHT22   : Reads DHT22 temp/humidity (PB0)
 *   Task 3 — SensorMPU6050 : Reads MPU6050 via I2C (PB6/PB7)
 *   Task 4 — UARTSensor    : Reads UART-based sensor (PA2/PA3)
 *   Task 5 — QueueManager  : Aggregates all sensor queues
 *   Task 6 — OutputManager : UART log + I2C LCD display
 *   Task 7 — ErrorHandler  : Monitors faults, stack overflows
 *   Task 8 — Diagnostics   : System health, uptime, CPU stats
 *   Task 9 — Watchdog      : Health monitor, IWDG feeder
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
alignas(8) static StackType_t   xADCStack[STACK_SIZE_SENSOR];

// Task 2: DHT22 Sensor
static StaticTask_t  xDHT22TaskBuffer;
alignas(8) static StackType_t   xDHT22Stack[STACK_SIZE_SENSOR];

// Task 3: MPU6050 Sensor
static StaticTask_t  xMPU6050TaskBuffer;
alignas(8) static StackType_t   xMPU6050Stack[STACK_SIZE_SENSOR];

// Task 4: UART Sensor
static StaticTask_t  xUARTSensorTaskBuffer;
alignas(8) static StackType_t   xUARTSensorStack[STACK_SIZE_SENSOR];

// Task 5: Queue Manager
static StaticTask_t  xQueueMgrTaskBuffer;
alignas(8) static StackType_t   xQueueMgrStack[STACK_SIZE_MANAGER];

// Task 6: Output Manager
static StaticTask_t  xOutputTaskBuffer;
alignas(8) static StackType_t   xOutputStack[STACK_SIZE_MANAGER];

// Task 7: Error Handler
static StaticTask_t  xErrorTaskBuffer;
alignas(8) static StackType_t   xErrorStack[STACK_SIZE_SYSTEM];

// Task 8: Diagnostics
static StaticTask_t  xDiagTaskBuffer;
alignas(8) static StackType_t   xDiagStack[STACK_SIZE_SYSTEM];

// Task 9: Watchdog
static StaticTask_t  xWatchdogTaskBuffer;
alignas(8) static StackType_t   xWatchdogStack[STACK_SIZE_SYSTEM];

static StaticEventGroup_t xHealthEventGroup;
EventGroupHandle_t xHealthGroupHandle = nullptr;

#ifdef CANARY_ONLY
static StaticTask_t  xCanaryTaskBuffer;
alignas(8) static StackType_t xCanaryStack[configMINIMAL_STACK_SIZE * 2U];
#endif

// ─── Task Handles ─────────────────────────────────────────────────────────────
TaskHandle_t hADCTask       = nullptr;
TaskHandle_t hDHT22Task     = nullptr;
TaskHandle_t hMPU6050Task   = nullptr;
TaskHandle_t hUARTSensTask  = nullptr;
TaskHandle_t hQueueMgrTask  = nullptr;
TaskHandle_t hOutputTask    = nullptr;
TaskHandle_t hErrorTask     = nullptr;
TaskHandle_t hDiagTask      = nullptr;
TaskHandle_t hWatchdogTask  = nullptr;

// ─── System Init ──────────────────────────────────────────────────────────────
static void SystemClock_Config(void);
static void GPIO_Init(void);
static void Peripherals_Init(void);

// ─── Boot trace ───────────────────────────────────────────────────────────────
// One short marker per init stage and per task first-run. The LAST marker on
// the serial monitor pinpoints the exact stage of any silent hang. Uses the
// raw (mutex-free) path so markers work before the UART mutex even exists.
static void boot_trace(const char *msg)
{
    UART_Driver_PrintRaw("[BOOT] ");
    UART_Driver_PrintRaw(msg);
    UART_Driver_PrintRaw("\r\n");
}

#ifdef CANARY_ONLY
// ─── Diagnostic canary (build with -DCANARY_ONLY=1) ──────────────────────────
// One minimal task, no mutexes, raw prints. Discriminates: context-restore
// broken (assert again) vs scheduler OK but tick dead (first print only) vs
// fully working (both prints repeat).
static void vTaskCanary(void *pvParams)
{
    (void)pvParams;
    UART_Driver_PrintRaw("[T] canary\r\n");
    for (;;)
    {
        UART_Driver_PrintRaw("[T] canary alive (tick ok)\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

int main(void)
{
    // HAL + Clock + GPIO + Peripherals (UART_Driver_Init prints the banner and
    // creates the log mutex — only after this point are markers guaranteed out)
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    Peripherals_Init();
    boot_trace("S1 clock+gpio+peripherals ok");

    // Init subsystems (all static — no heap)
    QueueManager_Init();
    ErrorHandler_Init();
    Diagnostics_Init();
    boot_trace("S2 subsystems ok");

    // ── NVIC priority-bit probe (diagnostic, matches the kernel's own check) ──
    // Wokwi's CPU model leaves the IPR register stubbed (read-back 0x00); real
    // F103 silicon reads 0xF0 (4 implemented bits, top-aligned). The printed
    // value documents which world we are running in.
    {
        volatile uint8_t *const pucIPR0 = (volatile uint8_t *const)0xE000E400U;
        uint8_t original = *pucIPR0;
        *pucIPR0 = 0xFFU;
        uint8_t readback = *pucIPR0;
        *pucIPR0 = original;
        char hex[18] = "[BOOT] nvic ipr=";
        static const char HEX[] = "0123456789ABCDEF";
        hex[16] = HEX[(readback >> 4) & 0x0F];
        hex[17] = HEX[readback & 0x0F];
        UART_Driver_PrintRaw(hex);
        UART_Driver_PrintRaw("\r\n");
    }

    xHealthGroupHandle = xEventGroupCreateStatic(&xHealthEventGroup);

#ifdef ENABLE_IWDG
    // Hardware Independent Watchdog: LSI 40 kHz / 64 = 625 Hz, reload 2499 → 4.0 s.
    // Period must exceed WATCHDOG_WINDOW_MS (3 s) so the Watchdog task can check
    // in. Enabled via build flag for real hardware; off by default for Wokwi.
    RCC->CSR |= RCC_CSR_LSION;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U) {}
    IWDG->KR = 0x5555; // Enable write access
    IWDG->PR = 0x04;   // Prescaler 64
    IWDG->RLR = 2499;  // Reload value → 4.0 s
    IWDG->KR = 0xCCCC; // Start IWDG
#endif

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

    // ── Spawn Task 9: Watchdog ───────────────────────────────────────────────
    hWatchdogTask = xTaskCreateStatic(
        vTaskWatchdog,
        "Watchdog",
        STACK_SIZE_SYSTEM,
        nullptr,
        PRIORITY_WATCHDOG,
        xWatchdogStack,
        &xWatchdogTaskBuffer
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
    configASSERT(hWatchdogTask != nullptr);
    configASSERT(xHealthGroupHandle != nullptr);
    boot_trace("S3 tasks spawned");

#ifdef CANARY_ONLY
    (void)hADCTask; (void)hDHT22Task; (void)hMPU6050Task; (void)hUARTSensTask;
    (void)hQueueMgrTask; (void)hOutputTask; (void)hErrorTask; (void)hDiagTask;
    (void)hWatchdogTask;
    xTaskCreateStatic(vTaskCanary, "Canary", configMINIMAL_STACK_SIZE * 2U,
                      nullptr, PRIORITY_SENSOR,
                      xCanaryStack, &xCanaryTaskBuffer);
    boot_trace("S3c canary spawned (CANARY_ONLY build)");
#else
    // Hand control to FreeRTOS scheduler — never returns
    boot_trace("S4 starting scheduler");
#endif
    vTaskStartScheduler();

    // Should never reach here
    ErrorHandler_Fatal(ERR_SCHEDULER_FAILED);
    while (1) {}
}

// ─── Clock Configuration: 64 MHz via PLL (HSI/2 × 16) ─────────────────────────
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInit = {};
    RCC_ClkInitTypeDef RCC_ClkInit = {};

    RCC_OscInit.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInit.HSIState       = RCC_HSI_ON;
    RCC_OscInit.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInit.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInit.PLL.PLLSource  = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInit.PLL.PLLMUL     = RCC_PLL_MUL16; // (8MHz/2) * 16 = 64 MHz
    HAL_RCC_OscConfig(&RCC_OscInit);

    RCC_ClkInit.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInit.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInit.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInit.APB1CLKDivider = RCC_HCLK_DIV2; // APB1 max 36 MHz
    RCC_ClkInit.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInit, FLASH_LATENCY_2);

    // ADC clock: PCLK2/6 = 10.67 MHz — must stay ≤ 14 MHz per the F1 datasheet.
    // The reset default (PCLK2/2 = 32 MHz) is out of spec and corrupts accuracy.
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
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

    // Release the DHT22 bus (idle-high) — the ODR defaults to LOW, which would
    // hold a spurious start condition on the line until the sensor task runs.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
}

// ─── Peripherals Init ─────────────────────────────────────────────────────────
static void Peripherals_Init(void)
{
    UART_Driver_Init();       // USART1 — PA9 TX, PA10 RX (115200 baud)
    boot_trace("S1a usart1");
    ADC_Driver_Init();        // ADC1   — PA0 Channel 0 (bounded calibration)
    boot_trace("S1b adc");
    MPU6050_Init();           // I2C1   — PB6 SCL, PB7 SDA
    boot_trace("S1c i2c-mpu");
    UARTSensor_Driver_Init(); // USART2 — PA2 TX, PA3 RX (9600 baud)
    boot_trace("S1d usart2");
}

// ─── FreeRTOS Static Memory Hooks (required for configSUPPORT_STATIC_ALLOCATION) ──
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  xIdleStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = xIdleStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

// (No vApplicationGetTimerTaskMemory — configUSE_TIMERS is 0, no timer service.)

// ─── Stack Overflow Hook ──────────────────────────────────────────────────────
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    ErrorHandler_StackOverflow(pcTaskName);
}

// ─── Malloc Fail Hook (catches any accidental heap use) ───────────────────────
extern "C" void vApplicationMallocFailedHook(void)
{
    ErrorHandler_Fatal(ERR_HEAP_ALLOC_ATTEMPTED);
}

// ─── Idle Hook ────────────────────────────────────────────────────────────────
// One-shot marker: if "[T] idle" appears but no task markers do, the scheduler
// runs and the tick is dead; if nothing appears at all, the first context
// switch itself never completes (assert/fault — now loud).
extern "C" void vApplicationIdleHook(void)
{
    static bool s_idle_announced = false;
    if (!s_idle_announced) {
        s_idle_announced = true;
        UART_Driver_PrintRaw("[T] idle (scheduler alive)\r\n");
    }
}

// ─── Fatal LED blink shared by fault handlers ─────────────────────────────────
extern "C" void LED_FatalBlink(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
        HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
        for (volatile uint32_t i = 0; i < 400000; i++) { __NOP(); }
    }
}

// ─── Hard Fault Handler ───────────────────────────────────────────────────────
// Overrides the startup file's silent infinite-loop default: a hard fault is
// reported over UART and blinked on the LED instead of vanishing.
extern "C" void HardFault_Handler(void)
{
    UART_Driver_PrintRaw("\r\n[FATAL] HARD FAULT\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
        HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
        for (volatile uint32_t i = 0; i < 400000; i++) { __NOP(); }
    }
}

// ─── SysTick Handler ──────────────────────────────────────────────────────────
// Required because we don't use STM32Cube generated stm32f1xx_it.c
extern "C" void SysTick_Handler(void)
{
    // 1. Increment HAL tick (required for HAL_Delay)
    HAL_IncTick();

    // 2. Increment FreeRTOS tick if scheduler is running
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        extern void xPortSysTickHandler(void);
        xPortSysTickHandler();
    }
}

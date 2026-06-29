/**
 * uart_driver.cpp — USART1 Log/Debug Output Driver
 * ==================================================
 * USART1: PA9=TX, PA10=RX — 115200 8N1
 * Used by: OutputManager, ErrorHandler, Diagnostics
 *
 * Thread safety: protected by a static binary semaphore.
 * Any task can call UART_Driver_Transmit() safely.
 */

#include "uart_driver.h"

static UART_HandleTypeDef  huart1;
static StaticSemaphore_t   xUARTMutexBuf;
static SemaphoreHandle_t   hUARTMutex;

void UART_Driver_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    // PA9 — TX (AF push-pull)
    gpio.Pin   = GPIO_PIN_9;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    // PA10 — RX (input floating)
    gpio.Pin  = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = UART_LOG_BAUDRATE;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    hUARTMutex = xSemaphoreCreateMutexStatic(&xUARTMutexBuf);
    configASSERT(hUARTMutex != nullptr);

    // Boot banner
    const char *banner =
        "\r\n"
        "╔══════════════════════════════════════════╗\r\n"
        "║  RTOS Industrial Edge Gateway v1.0       ║\r\n"
        "║  STM32F103C8 @ 72 MHz | FreeRTOS Static  ║\r\n"
        "║  8 Tasks | 5 Queues | 0 Bytes Heap       ║\r\n"
        "╚══════════════════════════════════════════╝\r\n\r\n";

    HAL_UART_Transmit(&huart1, (uint8_t*)banner, strlen(banner), 1000);
}

void UART_Driver_Transmit(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;

    if (xSemaphoreTake(hUARTMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        HAL_UART_Transmit(&huart1, (uint8_t*)data, len, UART_TIMEOUT_MS * 10);
        xSemaphoreGive(hUARTMutex);
    }
}

HAL_StatusTypeDef UART_Driver_Receive(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    return HAL_UART_Receive(&huart1, buf, len, timeout_ms);
}

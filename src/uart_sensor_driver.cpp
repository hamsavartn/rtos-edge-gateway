/**
 * uart_sensor_driver.cpp — USART2 Sensor Input Driver
 * =====================================================
 * USART2: PA2=TX, PA3=RX — 9600 8N1
 * Implements simple framing protocol:
 *   [0xAA][LEN][DATA × LEN][CHECKSUM]
 *   Checksum = XOR of all DATA bytes
 *   Max payload: 28 bytes
 *   Min frame: 3 bytes (0xAA + LEN=0 + 0x00)
 *
 * Used to simulate any serial industrial sensor
 * (e.g., CO2 sensor, flow meter, pressure transmitter).
 *
 * RX is INTERRUPT-DRIVEN: the F103 UART has a 1-byte register and no FIFO, so
 * polling from a task that sleeps 100 ms between reads would overrun and
 * corrupt any frame arriving off-schedule. A per-byte ISR feeds a static ring
 * buffer; the task drains complete frames from it at its own pace.
 */

#include "uart_sensor_driver.h"

#define FRAME_SOF       0xAAU
#define MAX_PAYLOAD     28U
#define FRAME_OVERHEAD  3U    // SOF + LEN + CHK
#define RX_RING_SIZE    128U  // Power of two — masks instead of modulo
#define RX_RING_MASK    (RX_RING_SIZE - 1U)

static UART_HandleTypeDef huart2;

// ─── IRQ ring buffer (single-producer ISR / single-consumer task) ────────────
static uint8_t          s_ring[RX_RING_SIZE];
static volatile uint8_t s_head = 0;   // Written by ISR
static volatile uint8_t s_tail = 0;   // Written by task
static volatile uint8_t s_overrun = 0;
static uint8_t          s_rx_byte;     // HAL per-byte transfer target

void UARTSensor_Driver_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    // PA2 — TX
    gpio.Pin   = GPIO_PIN_2;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    // PA3 — RX
    gpio.Pin  = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = UART_SENS_BAUDRATE;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);

    // Priority 6 ≥ configMAX_SYSCALL_INTERRUPT_PRIORITY (5): the ISR is masked
    // inside FreeRTOS critical sections. It touches no RTOS primitives anyway.
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    // Arm the first byte receive — the RxCplt callback re-arms after each byte.
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);
}

// ─── ISR plumbing ─────────────────────────────────────────────────────────────
extern "C" void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    uint8_t next = (uint8_t)(s_head + 1);
    if (next == s_tail) {
        s_overrun = 1;                    // Ring full — drop byte, flag it
    } else {
        s_ring[s_head] = s_rx_byte;
        s_head = next;
    }
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);   // Re-arm
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;
    // ORE/noise/framing abort the IT transfer (HAL leaves state READY) —
    // re-arm so the stream keeps flowing. Corrupted bytes fail the checksum
    // and the frame parser resynchronizes on the next SOF hunt.
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);
}

// ─── Ring pop (task context) ─────────────────────────────────────────────────
// Waits up to timeout_ms for the next byte. Deadlines compare tick deltas as
// signed values, so HAL tick wraparound is handled correctly.
static int ring_pop(uint8_t *out, uint32_t timeout_ms)
{
    uint32_t deadline = HAL_GetTick() + timeout_ms;
    for (;;)
    {
        if (s_tail != s_head) {
            *out = s_ring[s_tail];
            s_tail = (uint8_t)(s_tail + 1);
            return 1;
        }
        if ((int32_t)(deadline - HAL_GetTick()) <= 0) return 0; // expired
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ─── Read one framed packet ───────────────────────────────────────────────────
// Returns: UART_SENS_OK, UART_SENS_TIMEOUT, UART_SENS_OVERFLOW, UART_SENS_CRC_ERROR
UARTSensor_Status_t UARTSensor_ReadFrame(uint8_t *payload_out, uint8_t *len_out)
{
    uint8_t byte;

    if (s_overrun) {
        s_overrun = 0;
        return UART_SENS_OVERFLOW;
    }

    // ── Hunt for SOF byte (bounded — bytes drain at 9600 baud while we wait) ─
    uint32_t deadline = HAL_GetTick() + 50U;
    for (;;)
    {
        if (!ring_pop(&byte, 10U)) return UART_SENS_TIMEOUT;
        if (byte == FRAME_SOF) break;
        if ((int32_t)(deadline - HAL_GetTick()) <= 0) return UART_SENS_TIMEOUT;
    }

    // ── Read LEN ─────────────────────────────────────────────────────────────
    uint8_t len = 0;
    if (!ring_pop(&len, UART_TIMEOUT_MS)) return UART_SENS_TIMEOUT;
    if (len > MAX_PAYLOAD) return UART_SENS_OVERFLOW;

    // ── Read DATA ─────────────────────────────────────────────────────────────
    for (uint8_t i = 0; i < len; i++) {
        if (!ring_pop(&payload_out[i], UART_TIMEOUT_MS * 4)) return UART_SENS_TIMEOUT;
    }

    // ── Read CHECKSUM ────────────────────────────────────────────────────────
    uint8_t rx_chk = 0;
    if (!ring_pop(&rx_chk, UART_TIMEOUT_MS)) return UART_SENS_TIMEOUT;

    // ── Verify XOR checksum ──────────────────────────────────────────────────
    uint8_t calc_chk = 0;
    for (uint8_t i = 0; i < len; i++) calc_chk ^= payload_out[i];

    if (calc_chk != rx_chk) return UART_SENS_CRC_ERROR;

    *len_out = len;
    return UART_SENS_OK;
}

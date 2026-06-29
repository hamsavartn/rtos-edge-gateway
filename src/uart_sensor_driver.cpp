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
 */

#include "uart_sensor_driver.h"

#define FRAME_SOF       0xAAU
#define MAX_PAYLOAD     28U
#define FRAME_OVERHEAD  3U    // SOF + LEN + CHK

static UART_HandleTypeDef huart2;

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
}

// ─── Read one framed packet ───────────────────────────────────────────────────
// Returns: UART_SENS_OK, UART_SENS_TIMEOUT, UART_SENS_OVERFLOW, UART_SENS_CRC_ERROR
UARTSensor_Status_t UARTSensor_ReadFrame(uint8_t *payload_out, uint8_t *len_out)
{
    uint8_t byte;

    // ── Hunt for SOF byte ────────────────────────────────────────────────────
    if (HAL_UART_Receive(&huart2, &byte, 1, UART_TIMEOUT_MS) != HAL_OK) {
        return UART_SENS_TIMEOUT;
    }
    if (byte != FRAME_SOF) return UART_SENS_TIMEOUT;

    // ── Read LEN ─────────────────────────────────────────────────────────────
    uint8_t len = 0;
    if (HAL_UART_Receive(&huart2, &len, 1, UART_TIMEOUT_MS) != HAL_OK) {
        return UART_SENS_TIMEOUT;
    }
    if (len > MAX_PAYLOAD) return UART_SENS_OVERFLOW;

    // ── Read DATA ─────────────────────────────────────────────────────────────
    if (len > 0) {
        if (HAL_UART_Receive(&huart2, payload_out, len, UART_TIMEOUT_MS * 4) != HAL_OK) {
            return UART_SENS_TIMEOUT;
        }
    }

    // ── Read CHECKSUM ────────────────────────────────────────────────────────
    uint8_t rx_chk = 0;
    if (HAL_UART_Receive(&huart2, &rx_chk, 1, UART_TIMEOUT_MS) != HAL_OK) {
        return UART_SENS_TIMEOUT;
    }

    // ── Verify XOR checksum ──────────────────────────────────────────────────
    uint8_t calc_chk = 0;
    for (uint8_t i = 0; i < len; i++) calc_chk ^= payload_out[i];

    if (calc_chk != rx_chk) return UART_SENS_CRC_ERROR;

    *len_out = len;
    return UART_SENS_OK;
}

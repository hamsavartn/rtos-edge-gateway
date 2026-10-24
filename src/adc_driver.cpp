/**
 * adc_driver.cpp — Low-Level ADC1 Driver
 * ========================================
 * Configures ADC1 Channel 0 (PA0) with:
 *   - 12-bit resolution
 *   - Single conversion mode
 *   - Software trigger
 *   - Polling readout (no DMA — deterministic timing)
 *
 * Register-level notes:
 *   ADC1->CR2  bit 0  = ADON  (power on)
 *   ADC1->CR2  bit 22 = SWSTART (trigger conversion)
 *   ADC1->SR   bit 1  = EOC   (end of conversion)
 *   ADC1->DR           = data register (12-bit result)
 */

#include "adc_driver.h"

static ADC_HandleTypeDef hadc1;

void ADC_Driver_Init(void)
{
    // PA0 must be switched to analog input mode explicitly — the F1 HAL_ADC_Init
    // does not touch GPIO, and a floating digital pin reads garbage.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Pin  = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                   = ADC1;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef sConfig = {};
    sConfig.Channel      = ADC_CHANNEL_0;        // PA0
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    // ── Self-calibration (STM32F1 specific) with a BOUNDED wait ──────────────
    // HAL_ADCEx_Calibration_Start polls ADC_CR2.CAL with no timeout; a simulator
    // (or a glitched peripheral) that never clears the flag would hang the boot
    // before the scheduler starts — total silence after the UART banner. On real
    // silicon calibration finishes in microseconds, so the bound never trips.
    __HAL_ADC_ENABLE(&hadc1);
    for (volatile uint32_t i = 0; i < 10000; i++) { __NOP(); }   // t_STAB ≥ 1 µs

    ADC1->CR2 |= ADC_CR2_RSTCAL;                                 // reset calibration
    uint32_t guard = 1000000U;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) && --guard) { __NOP(); }

    ADC1->CR2 |= ADC_CR2_CAL;                                    // start calibration
    guard = 1000000U;
    while ((ADC1->CR2 & ADC_CR2_CAL) && --guard) { __NOP(); }
    // If the guard expired, calibration is skipped — conversions still run
    // (slightly less accurate); the task surfaces errors via its queue.
}

HAL_StatusTypeDef ADC_Driver_Read(uint16_t *out_raw)
{
    if (!out_raw) return HAL_ERROR;

    HAL_ADC_Start(&hadc1);
    HAL_StatusTypeDef status = HAL_ADC_PollForConversion(&hadc1, 10);

    if (status == HAL_OK) {
        *out_raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    HAL_ADC_Stop(&hadc1);
    return status;
}

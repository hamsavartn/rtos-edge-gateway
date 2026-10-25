/**
 * dht22_driver.cpp — DHT22 1-Wire Bit-Bang Driver
 * =================================================
 * Implements DHT22 protocol entirely in software on PB0.
 * Protocol timing (critical — must be cycle-accurate):
 *   Host start: pull LOW ≥ 1 ms, then HIGH, wait 20-40 µs
 *   Sensor response: 80 µs LOW, 80 µs HIGH
 *   Bit '0': 50 µs LOW + 26-28 µs HIGH
 *   Bit '1': 50 µs LOW + 70 µs HIGH
 *   Total: 40 bits = 16b humidity + 16b temp + 8b checksum
 *
 * Uses DWT cycle counter for µs-accurate delays at 72 MHz.
 */

#include "dht22_driver.h"
#include <string.h>

// ─── µs Delay: DWT cycle counter with busy-loop fallback ─────────────────────
// If the DWT cycle counter doesn't run (some simulators / debug configs),
// waiting on CYCCNT would spin forever inside the read critical section and
// freeze the whole RTOS — so verify it advances once, and fall back to a
// calibrated busy loop if it doesn't.
static int s_dwt_ok = -1;   // -1 = untested

static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
    if (s_dwt_ok < 0) {
        dwt_enable();
        DWT->CYCCNT = 0;
        uint32_t start = DWT->CYCCNT;
        for (volatile uint32_t i = 0; i < 1000; i++) { __NOP(); }
        s_dwt_ok = (DWT->CYCCNT != start) ? 1 : 0;
    }

    if (s_dwt_ok) {
        uint32_t start = DWT->CYCCNT;
        uint32_t ticks = us * (SystemCoreClock / 1000000UL);
        while ((DWT->CYCCNT - start) < ticks) {}
    } else {
        // ~7 cycles per iteration (decrement + branch + NOP on M3): calibrate
        // from SystemCoreClock. Precision is sufficient for DHT22 thresholds.
        uint32_t loops = us * (SystemCoreClock / 7000000UL);
        while (loops--) { __NOP(); }
    }
}

// ─── GPIO helpers ─────────────────────────────────────────────────────────────
static inline void dht_output(void)
{
    GPIO_InitTypeDef g = {};
    g.Pin   = DHT22_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_PORT, &g);
}

static inline void dht_input(void)
{
    GPIO_InitTypeDef g = {};
    g.Pin  = DHT22_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT22_PORT, &g);
}

static inline void dht_low(void)  { HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_RESET); }
static inline void dht_high(void) { HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET); }
static inline uint8_t dht_read(void) { return HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN); }

// ─── Critical-section shim ───────────────────────────────────────────────────
// In SIM_SUPERLOOP builds the scheduler never starts; kernel critical sections
// would dereference a NULL current-TCB. Fall back to a plain interrupt mask.
#include "FreeRTOS.h"
#include "task.h"
static inline void dht_crit_enter(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) taskENTER_CRITICAL();
    else __disable_irq();
}
static inline void dht_crit_exit(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) taskEXIT_CRITICAL();
    else __enable_irq();
}

// ─── Read 40 bits from DHT22 ──────────────────────────────────────────────────
DHT22_Status_t DHT22_Read(DHT22_Data_t *out)
{
    if (!out) return DHT22_TIMEOUT;

    uint8_t data[5] = {0};
    uint32_t timeout;

    // ── Start signal: pull LOW ≥ 1 ms ────────────────────────────────────────
    dht_crit_enter();

    dht_output();
    dht_low();
    delay_us(1200); // 1.2 ms
    dht_high();
    delay_us(30);
    dht_input();

    // ── Wait for sensor response: 80 µs LOW ──────────────────────────────────
    timeout = 10000;
    while (dht_read() == GPIO_PIN_SET)  { if (!--timeout) { dht_crit_exit(); return DHT22_TIMEOUT; } }
    timeout = 10000;
    while (dht_read() == GPIO_PIN_RESET){ if (!--timeout) { dht_crit_exit(); return DHT22_TIMEOUT; } }
    timeout = 10000;
    while (dht_read() == GPIO_PIN_SET)  { if (!--timeout) { dht_crit_exit(); return DHT22_TIMEOUT; } }

    // ── Read 40 bits ──────────────────────────────────────────────────────────
    for (uint8_t i = 0; i < 40; i++)
    {
        // Each bit starts with 50 µs LOW
        timeout = 10000;
        while (dht_read() == GPIO_PIN_RESET) { if (!--timeout) { dht_crit_exit(); return DHT22_TIMEOUT; } }

        // Measure HIGH duration: <28 µs = '0', ~70 µs = '1'
        delay_us(40);
        data[i / 8] <<= 1;
        if (dht_read() == GPIO_PIN_SET) {
            data[i / 8] |= 1;
        }

        timeout = 10000;
        while (dht_read() == GPIO_PIN_SET) { if (!--timeout) { dht_crit_exit(); return DHT22_TIMEOUT; } }
    }

    dht_crit_exit();

    // ── Checksum ──────────────────────────────────────────────────────────────
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) return DHT22_CHECKSUM_ERROR;

    // ── Parse ─────────────────────────────────────────────────────────────────
    // Humidity: data[0]<<8 | data[1], in 0.1 %RH
    out->humidity_x10 = (uint16_t)((data[0] << 8) | data[1]);

    // Temperature: data[2]<<8 | data[3], bit15 = sign, rest = 0.1°C
    uint16_t raw_temp = (uint16_t)(((data[2] & 0x7F) << 8) | data[3]);
    out->temperature_x10 = (data[2] & 0x80) ? -(int16_t)raw_temp : (int16_t)raw_temp;

    return DHT22_OK;
}

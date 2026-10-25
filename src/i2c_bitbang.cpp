/**
 * i2c_bitbang.cpp — software I2C master for the simulator build
 * ==============================================================
 * PB6 = SCL, PB7 = SDA, open-drain with pull-ups, ~100 kHz (5 µs half-cycles,
 * DWT-timed — the DHT22 driver proves these primitives work in Wokwi).
 */

#include "i2c_bitbang.h"
#include "stm32f1xx_hal.h"

#define BB_SCL_PORT GPIOB
#define BB_SCL_PIN  GPIO_PIN_6
#define BB_SDA_PORT GPIOB
#define BB_SDA_PIN  GPIO_PIN_7

// ─── DWT µs delay (self-tested, busy-loop fallback) ─────────────────────────
static int s_bb_dwt_ok = -1;

static void bb_delay_us(uint32_t us)
{
    if (s_bb_dwt_ok < 0) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DWT->CYCCNT = 0;
        uint32_t start = DWT->CYCCNT;
        for (volatile uint32_t i = 0; i < 1000; i++) { __NOP(); }
        s_bb_dwt_ok = (DWT->CYCCNT != start) ? 1 : 0;
    }
    if (s_bb_dwt_ok) {
        uint32_t start = DWT->CYCCNT;
        uint32_t ticks = us * (SystemCoreClock / 1000000UL);
        while ((DWT->CYCCNT - start) < ticks) {}
    } else {
        uint32_t loops = us * (SystemCoreClock / 7000000UL);
        while (loops--) { __NOP(); }
    }
}

// ─── Line primitives (open-drain: "high" = release) ─────────────────────────
static inline void scl_low(void)   { HAL_GPIO_WritePin(BB_SCL_PORT, BB_SCL_PIN, GPIO_PIN_RESET); }
static inline void scl_release(void){ HAL_GPIO_WritePin(BB_SCL_PORT, BB_SCL_PIN, GPIO_PIN_SET); }
static inline void sda_low(void)   { HAL_GPIO_WritePin(BB_SDA_PORT, BB_SDA_PIN, GPIO_PIN_RESET); }
static inline void sda_release(void){ HAL_GPIO_WritePin(BB_SDA_PORT, BB_SDA_PIN, GPIO_PIN_SET); }
static inline bool sda_read(void)  { return HAL_GPIO_ReadPin(BB_SDA_PORT, BB_SDA_PIN) == GPIO_PIN_SET; }

static void bb_start(void)
{
    sda_release(); scl_release(); bb_delay_us(5);
    sda_low();                       // SDA falls while SCL high
    bb_delay_us(5);
    scl_low();
    bb_delay_us(5);
}

static void bb_stop(void)
{
    sda_low(); bb_delay_us(5);
    scl_release(); bb_delay_us(5);
    sda_release(); bb_delay_us(5);   // SDA rises while SCL high
}

static bool bb_write_bit(bool bit)
{
    if (bit) sda_release(); else sda_low();
    bb_delay_us(5);
    scl_release(); bb_delay_us(5);
    bool nak_sample = false;         // (unused for writes; keep symmetric)
    (void)nak_sample;
    scl_low(); bb_delay_us(5);
    return true;
}

static bool bb_read_bit(void)
{
    sda_release(); bb_delay_us(5);
    scl_release(); bb_delay_us(5);
    bool bit = sda_read();
    scl_low(); bb_delay_us(5);
    return bit;
}

static bool bb_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) bb_write_bit((b >> i) & 1U);
    return !bb_read_bit();           // ACK = SDA low during 9th clock
}

static uint8_t bb_read_byte(bool send_ack)
{
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) b = (uint8_t)((b << 1) | (bb_read_bit() ? 1U : 0U));
    bb_write_bit(!send_ack);         // ACK=0 for more bytes, NAK=1 for last
    return b;
}

// ─── Public API ──────────────────────────────────────────────────────────────
int bb_i2c_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin   = BB_SCL_PIN | BB_SDA_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    scl_release();
    sda_release();
    bb_delay_us(10);
    return 0;
}

int bb_i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    bb_start();
    if (!bb_write_byte((uint8_t)(addr7 << 1 | 0U))) { bb_stop(); return -1; }
    for (size_t i = 0; i < len; i++) {
        if (!bb_write_byte(data[i])) { bb_stop(); return -2; }
    }
    bb_stop();
    return 0;
}

int bb_i2c_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                      uint8_t *rd, size_t rd_len)
{
    bb_start();
    if (!bb_write_byte((uint8_t)(addr7 << 1 | 0U))) { bb_stop(); return -1; }
    for (size_t i = 0; i < wr_len; i++) {
        if (!bb_write_byte(wr[i])) { bb_stop(); return -2; }
    }
    bb_start();                      // repeated START
    if (!bb_write_byte((uint8_t)(addr7 << 1 | 1U))) { bb_stop(); return -3; }
    for (size_t i = 0; i < rd_len; i++) {
        rd[i] = bb_read_byte(i + 1 < rd_len);   // ACK all but last
    }
    bb_stop();
    return 0;
}

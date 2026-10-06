/**
 * mpu6050_driver.cpp — MPU6050 Low-Level I2C Driver
 * ===================================================
 * Communicates with MPU6050 via I2C1 (PB6=SCL, PB7=SDA).
 * Reads all 6 axes + temperature in a single 14-byte burst
 * from register 0x3B (ACCEL_XOUT_H) through 0x48 (TEMP_OUT_L).
 *
 * Key Registers:
 *   0x6B — PWR_MGMT_1   (wake up, select clock)
 *   0x1B — GYRO_CONFIG  (±250°/s full scale)
 *   0x1C — ACCEL_CONFIG (±2g full scale)
 *   0x3B — ACCEL_XOUT_H (burst read start)
 *   0x75 — WHO_AM_I     (0x68 = valid device)
 */

#include "mpu6050_driver.h"

static I2C_HandleTypeDef hi2c1;

// ─── Register Map ─────────────────────────────────────────────────────────────
#define MPU_REG_WHO_AM_I      0x75
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_GYRO_CFG     0x1B
#define MPU_REG_ACCEL_CFG    0x1C
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define MPU_REG_SMPLRT_DIV   0x19
#define MPU_REG_CONFIG       0x1A

#define MPU_WHO_AM_I_VAL     0x68

// ─── I2C Write helper ─────────────────────────────────────────────────────────
static HAL_StatusTypeDef mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(&hi2c1, MPU6050_I2C_ADDR, buf, 2, 10);
}

// ─── I2C Read helper ──────────────────────────────────────────────────────────
static HAL_StatusTypeDef mpu_read(uint8_t reg, uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef s;
    s = HAL_I2C_Master_Transmit(&hi2c1, MPU6050_I2C_ADDR, &reg, 1, 10);
    if (s != HAL_OK) return s;
    return HAL_I2C_Master_Receive(&hi2c1, MPU6050_I2C_ADDR, data, len, 10);
}

// ─── Init ─────────────────────────────────────────────────────────────────────
HAL_StatusTypeDef MPU6050_Init(void)
{
    // I2C1 peripheral init
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;   // PB6=SCL, PB7=SDA
    gpio.Mode  = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 100000;    // 100 kHz Standard Mode (Shared with PCF8574 LCD)
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) return HAL_ERROR;

    // Verify WHO_AM_I
    uint8_t who = 0;
    if (mpu_read(MPU_REG_WHO_AM_I, &who, 1) != HAL_OK) return HAL_ERROR;
    if (who != MPU_WHO_AM_I_VAL) return HAL_ERROR;

    // Wake up (clear sleep bit), use PLL with X gyro reference
    if (mpu_write(MPU_REG_PWR_MGMT_1, 0x01) != HAL_OK) return HAL_ERROR;

    // Sample rate divider: 0 = 1 kHz internal / (1+0) = 1 kHz
    if (mpu_write(MPU_REG_SMPLRT_DIV, 0x00) != HAL_OK) return HAL_ERROR;

    // DLPF: 10 Hz bandwidth (reduces noise)
    if (mpu_write(MPU_REG_CONFIG, 0x05) != HAL_OK) return HAL_ERROR;

    // Gyro: ±250°/s (FS_SEL=0)
    if (mpu_write(MPU_REG_GYRO_CFG, 0x00) != HAL_OK) return HAL_ERROR;

    // Accel: ±2g (AFS_SEL=0)
    if (mpu_write(MPU_REG_ACCEL_CFG, 0x00) != HAL_OK) return HAL_ERROR;

    return HAL_OK;
}

// ─── Read All (14-byte burst) ─────────────────────────────────────────────────
HAL_StatusTypeDef MPU6050_ReadAll(MPU6050_RawData_t *out)
{
    if (!out) return HAL_ERROR;

    uint8_t raw[14];
    if (mpu_read(MPU_REG_ACCEL_XOUT_H, raw, 14) != HAL_OK) return HAL_ERROR;

    // Big-endian 16-bit values from MPU6050
    out->accel_x  = (int16_t)((raw[0]  << 8) | raw[1]);
    out->accel_y  = (int16_t)((raw[2]  << 8) | raw[3]);
    out->accel_z  = (int16_t)((raw[4]  << 8) | raw[5]);
    out->temp_raw = (int16_t)((raw[6]  << 8) | raw[7]);
    out->gyro_x   = (int16_t)((raw[8]  << 8) | raw[9]);
    out->gyro_y   = (int16_t)((raw[10] << 8) | raw[11]);
    out->gyro_z   = (int16_t)((raw[12] << 8) | raw[13]);

    return HAL_OK;
}

// ─── Expose I2C handle (shared with LCD driver) ───────────────────────────────
I2C_HandleTypeDef* MPU6050_GetI2CHandle(void) { return &hi2c1; }

// ─── DWT µs Delay (72 MHz = 72 cycles/µs) ────────────────────────────────────
static void delay_us(uint32_t us)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    }
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000UL);
    while ((DWT->CYCCNT - start) < ticks) {}
}

void MPU6050_Recover(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. De-initialize the I2C hardware to release AF control
    HAL_I2C_DeInit(&hi2c1);

    // 2. Configure SCL and SDA as standard Open-Drain outputs
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 3. Set both lines high initially
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    delay_us(10);

    // 4. Bit-bang up to 9 clock pulses on SCL
    for (int i = 0; i < 9; i++) {
        // If SDA goes high, the bus is freed
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) {
            break;
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        delay_us(5); // ~100kHz
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        delay_us(5);
    }

    // 5. Generate a manual STOP condition
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    delay_us(5);

    // 6. Re-initialize I2C
    MPU6050_Init();
}

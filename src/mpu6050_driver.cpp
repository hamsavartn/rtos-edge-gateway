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
    hi2c1.Init.ClockSpeed      = 400000;    // 400 kHz Fast Mode
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

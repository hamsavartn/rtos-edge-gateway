#pragma once
#include "main.h"

typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t gyro_x,  gyro_y,  gyro_z;
    int16_t temp_raw;
} MPU6050_RawData_t;

HAL_StatusTypeDef  MPU6050_Init(void);
HAL_StatusTypeDef  MPU6050_ReadAll(MPU6050_RawData_t *out);
I2C_HandleTypeDef* MPU6050_GetI2CHandle(void);

#pragma once
#include "main.h"

typedef enum { DHT22_OK = 0, DHT22_TIMEOUT, DHT22_CHECKSUM_ERROR } DHT22_Status_t;

typedef struct {
    int16_t  temperature_x10;
    uint16_t humidity_x10;
} DHT22_Data_t;

DHT22_Status_t DHT22_Read(DHT22_Data_t *out);

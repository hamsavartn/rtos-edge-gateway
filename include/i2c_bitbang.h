#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Minimal bit-banged I2C master (PB6=SCL, PB7=SDA, ~100 kHz, DWT-timed).
// Used by the SIM_SUPERLOOP build: Wokwi's STM32 I2C peripheral model does
// not complete HAL transactions, while GPIO bit-banging demonstrably works
// (the DHT22 driver runs on the same primitives).
// All functions return 0 on success (slave ACKed), nonzero on NAK/timeout.

int bb_i2c_init(void);
int bb_i2c_write(uint8_t addr7, const uint8_t *data, size_t len);
int bb_i2c_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                      uint8_t *rd, size_t rd_len);

---
name: i2c-bus-recovery
description: Software techniques for recovering a locked I2C bus gracefully.
---
# I2C Bus Recovery
If a slave (e.g., MPU6050) loses power mid-transaction or gets out of sync, it may hold the SDA line low, locking the bus.
1. **Detection**: `HAL_I2C_Master_Transmit` returns `HAL_TIMEOUT` or `HAL_BUSY`.
2. **Recovery Sequence**:
   - De-initialize the I2C peripheral (`HAL_I2C_DeInit`).
   - Reconfigure SCL and SDA pins as standard GPIO outputs.
   - Manually bit-bang 9 clock pulses on SCL while reading SDA.
   - If SDA goes high, generate a STOP condition (SDA low to high while SCL is high).
   - Re-initialize the I2C peripheral (`HAL_I2C_Init`).

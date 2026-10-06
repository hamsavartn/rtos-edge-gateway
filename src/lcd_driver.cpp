/**
 * lcd_driver.cpp — I2C LCD Driver (PCF8574 Backpack, 16x2)
 * Based on the Research Agent's Diagnostic Analysis for Zero-Heap
 */

#include "lcd_driver.h"
#include "mpu6050_driver.h"
#include "FreeRTOS.h"
#include "task.h"

// PCF8574 to HD44780 Pin Masks
#define LCD_EN 0x04 // Enable bit (P2)
#define LCD_RW 0x02 // Read/Write bit (P1)
#define LCD_RS 0x01 // Register select bit (P0)
#define LCD_BL 0x08 // Backlight bit (P3)

// ─── Internal function to transmit a nibble sequence via HAL ────────────
static HAL_StatusTypeDef LCD_SendInternal(uint8_t data, uint8_t flags) {
    HAL_StatusTypeDef status;
    
    // Isolate the upper and lower 4 bits and map them to P4-P7
    uint8_t upNib = (data & 0xF0) | flags | LCD_BL;
    uint8_t loNib = ((data << 4) & 0xF0) | flags | LCD_BL;
    
    // Array holds the state changes: EN High, EN Low for both nibbles
    uint8_t data_arr[4];
    data_arr[0] = upNib | LCD_EN;   // Upper nibble, EN = 1
    data_arr[1] = upNib & ~LCD_EN;  // Upper nibble, EN = 0 (Falling edge clocks data)
    data_arr[2] = loNib | LCD_EN;   // Lower nibble, EN = 1
    data_arr[3] = loNib & ~LCD_EN;  // Lower nibble, EN = 0 (Falling edge clocks data)
    
    // Transmit the 4-byte sequence in a single I2C transaction
    I2C_HandleTypeDef* p_hi2c1 = MPU6050_GetI2CHandle();
    if (p_hi2c1 == NULL) return HAL_ERROR;
    
    status = HAL_I2C_Master_Transmit(p_hi2c1, LCD_I2C_ADDR, data_arr, 4, HAL_MAX_DELAY);
    
    // A micro-delay to satisfy HD44780 instruction execution times
    // In a zero-heap RTOS, vTaskDelay blocks, allowing other tasks to run.
    vTaskDelay(pdMS_TO_TICKS(2));
    
    return status;
}
// ─── Internal function to transmit a single nibble ──────────────────────
static HAL_StatusTypeDef LCD_SendNibble(uint8_t nibble, uint8_t flags) {
    HAL_StatusTypeDef status;
    uint8_t data = (nibble & 0xF0) | flags | LCD_BL;
    
    uint8_t data_arr[2];
    data_arr[0] = data | LCD_EN;   // EN = 1
    data_arr[1] = data & ~LCD_EN;  // EN = 0
    
    I2C_HandleTypeDef* p_hi2c1 = MPU6050_GetI2CHandle();
    if (p_hi2c1 == NULL) return HAL_ERROR;
    
    status = HAL_I2C_Master_Transmit(p_hi2c1, LCD_I2C_ADDR, data_arr, 2, HAL_MAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(2));
    return status;
}

// ─── Public API ─────────────────────────────────────────────────────────

HAL_StatusTypeDef LCD_Command(uint8_t cmd) {
    return LCD_SendInternal(cmd, 0);
}

HAL_StatusTypeDef LCD_Data(uint8_t data) {
    return LCD_SendInternal(data, LCD_RS);
}

HAL_StatusTypeDef LCD_Init(void) {
    HAL_StatusTypeDef status;
    
    // Wait for the LCD internal voltage to stabilize post-boot
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Hardware reset sequence to force 8-bit boundary sync
    // Send single nibbles (upper 4 bits of the command)
    status = LCD_SendNibble(0x30, 0);
    if (status != HAL_OK) return status;
    vTaskDelay(pdMS_TO_TICKS(5));
    
    LCD_SendNibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    LCD_SendNibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Transition to 4-bit mode (single nibble)
    LCD_SendNibble(0x20, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Now we are in 4-bit mode, we can use the dual-nibble LCD_Command
    LCD_Command(0x28); // Function set: 4-bit interface, 2 lines, 5x8 font
    LCD_Command(0x0C); // Display control: Display ON, Cursor OFF, Blink OFF
    LCD_Command(0x01); // Clear display
    vTaskDelay(pdMS_TO_TICKS(5)); // Clear display takes longer to execute
    LCD_Command(0x06); // Entry mode: Increment cursor, no display shift
    
    return HAL_OK;
}

HAL_StatusTypeDef LCD_Clear(void) {
    HAL_StatusTypeDef status = LCD_Command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    return status;
}

HAL_StatusTypeDef LCD_SetCursor(uint8_t row, uint8_t col) {
    static const uint8_t row_offsets[] = { 0x00, 0x40 };
    return LCD_Command(0x80 | (row_offsets[row & 0x01] + col));
}

HAL_StatusTypeDef LCD_Print(const char *str) {
    HAL_StatusTypeDef status = HAL_OK;
    if (!str) return HAL_ERROR;
    
    while (*str && status == HAL_OK) {
        status = LCD_Data((uint8_t)(*str));
        str++;
    }
    return status;
}

HAL_StatusTypeDef LCD_PrintN(const char *str, uint8_t max_chars) {
    HAL_StatusTypeDef status = HAL_OK;
    if (!str) return HAL_ERROR;
    
    uint8_t n = 0;
    while (*str && n < max_chars && status == HAL_OK) {
        status = LCD_Data((uint8_t)(*str));
        str++;
        n++;
    }
    
    // Pad with spaces
    while (n < max_chars && status == HAL_OK) {
        status = LCD_Data(' ');
        n++;
    }
    return status;
}

/**
 * lcd_driver.cpp — I2C LCD Driver (PCF8574 Backpack, 16x2)
 * ==========================================================
 * Shares I2C1 bus with MPU6050 (arbitrated by HAL).
 * LCD I2C address: 0x27 (PCF8574 default)
 *
 * PCF8574 → HD44780 pin mapping:
 *   P7 P6 P5 P4 | P3  P2  P1  P0
 *   D7 D6 D5 D4 | BL  EN  RW  RS
 *
 * All LCD communication is 4-bit mode.
 * EN pulse must be ≥ 450 ns — we use HAL_Delay(1) safely.
 */

#include "lcd_driver.h"
#include "mpu6050_driver.h"  // shared I2C handle

#define LCD_BL   0x08U   // Backlight bit
#define LCD_EN   0x04U   // Enable bit
#define LCD_RW   0x02U   // Read/Write (always 0 = write)
#define LCD_RS   0x01U   // Register Select (0=cmd, 1=data)

static I2C_HandleTypeDef *phi2c = nullptr;

// ─── Send raw byte to PCF8574 ────────────────────────────────────────────────
static void lcd_i2c_send(uint8_t byte)
{
    HAL_I2C_Master_Transmit(phi2c, LCD_I2C_ADDR, &byte, 1, 10);
}

// ─── Pulse EN pin to latch 4-bit nibble ──────────────────────────────────────
static void lcd_pulse_en(uint8_t data)
{
    lcd_i2c_send(data | LCD_EN);
    HAL_Delay(1);
    lcd_i2c_send(data & ~LCD_EN);
    HAL_Delay(1);
}

// ─── Send one nibble (upper 4 bits of data) ───────────────────────────────────
static void lcd_send_nibble(uint8_t nibble, uint8_t flags)
{
    uint8_t byte = (nibble & 0xF0) | flags | LCD_BL;
    lcd_pulse_en(byte);
}

// ─── Send full byte as two nibbles ────────────────────────────────────────────
static void lcd_send_byte(uint8_t byte, uint8_t flags)
{
    lcd_send_nibble(byte & 0xF0,        flags);  // High nibble
    lcd_send_nibble((byte << 4) & 0xF0, flags);  // Low nibble
}

// ─── Public API ───────────────────────────────────────────────────────────────
void LCD_Driver_Init(void)
{
    phi2c = MPU6050_GetI2CHandle();
    configASSERT(phi2c != nullptr);

    HAL_Delay(50);  // LCD power-on wait

    // Init sequence for 4-bit mode (HD44780 datasheet)
    lcd_send_nibble(0x30, 0); HAL_Delay(5);
    lcd_send_nibble(0x30, 0); HAL_Delay(1);
    lcd_send_nibble(0x30, 0); HAL_Delay(1);
    lcd_send_nibble(0x20, 0); HAL_Delay(1);  // Switch to 4-bit

    LCD_Command(0x28); // 4-bit, 2 lines, 5x8 font
    LCD_Command(0x0C); // Display ON, cursor OFF
    LCD_Command(0x06); // Entry mode: increment, no shift
    LCD_Command(0x01); // Clear display
    HAL_Delay(2);
}

void LCD_Command(uint8_t cmd)
{
    lcd_send_byte(cmd, 0);       // RS=0: command
}

void LCD_Data(uint8_t data)
{
    lcd_send_byte(data, LCD_RS); // RS=1: data
}

void LCD_Clear(void)
{
    LCD_Command(0x01);
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    static const uint8_t row_offsets[] = { 0x00, 0x40 };
    LCD_Command(0x80 | (row_offsets[row & 0x01] + col));
}

void LCD_Print(const char *str)
{
    if (!str) return;
    while (*str) {
        LCD_Data((uint8_t)(*str++));
    }
}

void LCD_PrintN(const char *str, uint8_t max_chars)
{
    if (!str) return;
    uint8_t n = 0;
    while (*str && n < max_chars) {
        LCD_Data((uint8_t)(*str++));
        n++;
    }
    // Pad with spaces to clear old characters
    while (n < max_chars) {
        LCD_Data(' ');
        n++;
    }
}

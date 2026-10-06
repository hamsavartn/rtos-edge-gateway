---
name: bare-metal-bootloader
description: Architecture for a custom bare-metal bootloader and OTA updates on STM32.
---
# Bare-Metal Bootloader
1. **Flash Partitioning**: Divide Flash into Bootloader (e.g., 0x08000000 to 0x08003FFF) and Application (e.g., 0x08004000 onwards).
2. **Linker Script**: Modify the Application's `.ld` script so `FLASH` starts at `0x08004000`. Set `SCB->VTOR = 0x08004000` in `SystemInit()`.
3. **Jump Logic**: The bootloader verifies the Application's CRC, loads the App's Stack Pointer from `0x08004000`, gets the Reset Handler from `0x08004004`, de-initializes peripherals, and executes a function pointer jump to the App.

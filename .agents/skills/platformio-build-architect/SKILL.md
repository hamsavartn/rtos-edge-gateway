---
name: platformio-build-architect
description: Guides the configuration of platformio.ini and build flags for FreeRTOS.
---
# PlatformIO Build Architect
When modifying the build system (`platformio.ini`):
1. **Build Flags**: Ensure essential flags like `-D USE_HAL_DRIVER` and target-specific defines (e.g., `-D STM32F103xB`) are preserved.
2. **Dependencies**: Manage `lib_deps` carefully, linking to correct FreeRTOS ports for ARM_CM3.
3. **Include Paths**: Ensure `-I` flags point to the correct FreeRTOS `include` and `portable/GCC/ARM_CM3` directories.
4. **Scripts**: Use `extra_scripts` to run Python scripts that handle pre-compilation tasks like pruning unused FreeRTOS heap implementations (since we use zero-heap).

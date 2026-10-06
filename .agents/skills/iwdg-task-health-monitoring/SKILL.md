---
name: iwdg-task-health-monitoring
description: Architectural guidelines for implementing a Task Health Monitoring system using STM32 IWDG in FreeRTOS.
---
# Task Health Monitoring with IWDG
To ensure fault tolerance in a multi-threaded FreeRTOS environment:
1. **Dedicated Watchdog Task**: Create a high-priority task responsible for petting the IWDG.
2. **Event Group / Bitmask**: Use a FreeRTOS Event Group where every other task sets a specific bit when it completes a cycle.
3. **Validation**: The Watchdog task waits for all bits to be set within a timeframe (e.g., 2000ms). If all bits are set, it clears the Event Group and calls `HAL_IWDG_Refresh()`. If a task hangs, its bit isn't set, the IWDG times out, and the system resets gracefully.

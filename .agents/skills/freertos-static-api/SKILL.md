---
name: freertos-static-api
description: Strict guidelines for FreeRTOS zero-heap implementation.
---
# FreeRTOS Static API Reference
Always use `Static` variants to enforce `configSUPPORT_DYNAMIC_ALLOCATION 0`.
- **Tasks**:
  ```cpp
  StaticTask_t xTaskBuffer;
  StackType_t xStack[STACK_SIZE];
  xTaskCreateStatic(TaskCode, "Name", STACK_SIZE, pvParameters, Priority, xStack, &xTaskBuffer);
  ```
- **Queues**:
  ```cpp
  StaticQueue_t xQueueBuffer;
  uint8_t ucQueueStorage[QUEUE_LENGTH * ITEM_SIZE];
  xQueueCreateStatic(QUEUE_LENGTH, ITEM_SIZE, ucQueueStorage, &xQueueBuffer);
  ```
- **Mutexes**:
  ```cpp
  StaticSemaphore_t xMutexBuffer;
  xSemaphoreCreateMutexStatic(&xMutexBuffer);
  ```

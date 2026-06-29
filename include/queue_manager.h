/**
 * queue_manager.h
 */
#pragma once
#include "main.h"

extern QueueHandle_t hADCQueue;
extern QueueHandle_t hDHT22Queue;
extern QueueHandle_t hMPU6050Queue;
extern QueueHandle_t hUARTSensQueue;
extern QueueHandle_t hOutputQueue;

void        QueueManager_Init(void);
uint32_t    QueueManager_GetTotalRouted(void);
UBaseType_t QueueManager_GetADCDepth(void);
UBaseType_t QueueManager_GetDHT22Depth(void);
UBaseType_t QueueManager_GetMPUDepth(void);
UBaseType_t QueueManager_GetUARTDepth(void);
UBaseType_t QueueManager_GetOutputDepth(void);

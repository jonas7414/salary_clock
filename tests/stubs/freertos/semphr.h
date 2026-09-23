#pragma once
using SemaphoreHandle_t=void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static int mutex;return &mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t,unsigned){return 1;}
inline int xSemaphoreGive(SemaphoreHandle_t){return 1;}

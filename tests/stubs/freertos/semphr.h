#pragma once
struct HostMutex { bool locked{}; };
using SemaphoreHandle_t=HostMutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static HostMutex pool[16];static unsigned next;return &pool[next++%16];}
inline int xSemaphoreTake(SemaphoreHandle_t m,unsigned){if(m->locked)return 0;m->locked=true;return 1;}
inline int xSemaphoreGive(SemaphoreHandle_t m){m->locked=false;return 1;}

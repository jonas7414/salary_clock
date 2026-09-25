#pragma once
#include "esp_err.h"
esp_err_t button_start();
void button_check_wakeup();
[[noreturn]] void button_enter_deep_sleep();

#pragma once
#include "app_types.h"
#include "esp_err.h"
esp_err_t app_config_init(bool *configured);
AppConfig app_config_snapshot();
esp_err_t app_config_save(const AppConfig &config);
esp_err_t app_config_reset();

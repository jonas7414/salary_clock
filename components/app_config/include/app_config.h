#pragma once
#include "app_types.h"
#include "display_preferences.h"
#include "esp_err.h"
esp_err_t app_config_init(bool *configured);
AppConfig app_config_snapshot();
DisplayTheme app_config_theme();
DisplaySchedule app_config_display_schedule();
DisplayPreferences app_config_display_preferences();
void app_config_ignored_version(char (&version)[32]);
esp_err_t app_config_ignore_version(const char *version);
esp_err_t app_config_save(const AppConfig &config);
esp_err_t app_config_save(const AppConfig &config,DisplayTheme theme);
esp_err_t app_config_save(const AppConfig &config,DisplayTheme theme,const DisplaySchedule &schedule);
esp_err_t app_config_save(const AppConfig &config,DisplayTheme theme,const DisplaySchedule &schedule,const DisplayPreferences &preferences);
esp_err_t app_config_reset();

// Serialize flash configuration writes with OTA. Snapshots remain available.
bool app_config_begin_ota();
void app_config_end_ota();
esp_err_t app_config_verify();

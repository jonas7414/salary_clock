#pragma once
#include "esp_err.h"
#include <cstdint>

#include "ota_status.h"
enum class OtaEvent { START, PROGRESS, VERIFY, SUCCESS, FAILED };
using OtaCallback=void (*)(OtaEvent event,const OtaStatus &status,void *context);
// Initialize once, after NVS, system events and the application tasks start.
// Requests are nonblocking and serialized by the OTA task. Check does not install;
// start_update requires an active prompt and revalidates that exact version.
esp_err_t ota_init();
esp_err_t ota_check_update();
esp_err_t ota_start_update();
void ota_prompt_next();
esp_err_t ota_prompt_confirm();
const char *ota_get_current_version();
OtaStatus ota_get_status();
OtaState ota_get_state();
bool ota_is_update_available();
// Latest version is copied in ota_get_status(), never returned as a shared buffer.
void ota_set_callback(OtaCallback callback,void *context=nullptr);
const char *ota_state_name(OtaState state);

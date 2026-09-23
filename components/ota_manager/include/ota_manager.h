#pragma once
#include "esp_err.h"
#include <cstdint>

enum class OtaState { IDLE, CHECKING, UPDATE_AVAILABLE, DOWNLOADING, VERIFYING, READY_TO_REBOOT, ERROR };
enum class OtaEvent { START, PROGRESS, VERIFY, SUCCESS, FAILED };
struct OtaStatus {
    OtaState state{OtaState::IDLE};
    char latest_version[32]{};
    uint32_t downloaded_bytes{},total_bytes{};
    unsigned percentage{};
    int http_status{};
    esp_err_t error{ESP_OK};
    char message[96]{};
};
using OtaCallback=void (*)(OtaEvent event,const OtaStatus &status,void *context);
// Initialize once, after NVS, system events and the application tasks start.
// Requests are nonblocking and serialized by the OTA task. Check does not install;
// start_update refreshes release metadata before installing a newer version.
esp_err_t ota_init();
esp_err_t ota_check_update();
esp_err_t ota_start_update();
const char *ota_get_current_version();
OtaStatus ota_get_status();
OtaState ota_get_state();
bool ota_is_update_available();
// Latest version is copied in ota_get_status(), never returned as a shared buffer.
void ota_set_callback(OtaCallback callback,void *context=nullptr);
const char *ota_state_name(OtaState state);

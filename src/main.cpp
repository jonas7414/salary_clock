#include "app_config.h"
#include "app_system.h"
#include "display.h"
#include "button.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "battery_manager.h"
#include "calendar_manager.h"
#include "salary.h"
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
extern "C" void app_main() {
    button_check_wakeup();
    // A stuck initializer must also reset pending firmware for bootloader rollback.
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    ESP_ERROR_CHECK(app_system_init());
    bool configured=false;
    ESP_ERROR_CHECK(app_config_init(&configured));
    if (configured) xEventGroupSetBits(system_events(),CONFIG_READY_BIT);
    ESP_LOGI("main","Salary Thief Calculator %s",APP_FIRMWARE_VERSION);
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    ESP_ERROR_CHECK(display_start());
    // The display task plays its intro while these services start and connect.
    // Never wait for the animation here: Wi-Fi and time sync run in parallel.
    const auto battery_error=battery_manager_start();
    if (battery_error!=ESP_OK) ESP_LOGW("main","Battery monitor unavailable: %s",esp_err_to_name(battery_error));
    ESP_ERROR_CHECK(button_start());
    const auto calendar_error=calendar_manager_start();
    if (calendar_error!=ESP_OK) ESP_LOGW("main","Calendar updates unavailable: %s",esp_err_to_name(calendar_error));
    ESP_ERROR_CHECK(salary_start());
    ESP_ERROR_CHECK(time_manager_start());
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    ESP_ERROR_CHECK(wifi_manager_start(configured));
    const esp_err_t ota_error=ota_init();
    if (ota_error!=ESP_OK) ESP_LOGE("main","OTA unavailable: %s",esp_err_to_name(ota_error));
    ESP_ERROR_CHECK(esp_task_wdt_delete(nullptr));
}

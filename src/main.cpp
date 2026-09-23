#include "app_config.h"
#include "app_system.h"
#include "display.h"
#include "button.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "salary.h"
#include "esp_log.h"
extern "C" void app_main() {
    ESP_ERROR_CHECK(app_system_init());
    bool configured=false;
    ESP_ERROR_CHECK(app_config_init(&configured));
    if (configured) xEventGroupSetBits(system_events(),CONFIG_READY_BIT);
    ESP_LOGI("main","Salary Thief Calculator %s",APP_FIRMWARE_VERSION);
    ESP_ERROR_CHECK(display_start());
    ESP_ERROR_CHECK(button_start());
    ESP_ERROR_CHECK(salary_start());
    ESP_ERROR_CHECK(time_manager_start());
    ESP_ERROR_CHECK(wifi_manager_start(configured));
}

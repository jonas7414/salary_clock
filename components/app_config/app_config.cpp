#include "app_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
namespace {
constexpr const char *TAG = "config";
constexpr uint32_t RECORD_MAGIC = 0x53544331;
struct Record { uint32_t magic, size, crc; AppConfig config; };
AppConfig active;
SemaphoreHandle_t mutex;
}
esp_err_t app_config_init(bool *configured) {
    *configured = false;
    mutex = xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG,"Recovering unreadable NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    active = config_defaults();
    nvs_handle_t handle;
    err = nvs_open("salary_thief",NVS_READONLY,&handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    Record record{}; size_t size = sizeof(record);
    err = nvs_get_blob(handle,"config",&record,&size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(record) && record.magic == RECORD_MAGIC &&
        record.size == sizeof(AppConfig) && record.crc == config_checksum(record.config) &&
        config_validate(record.config,true)) {
        active=record.config; *configured=true;
        ESP_LOGI(TAG,"Loaded configuration v%lu",static_cast<unsigned long>(active.version));
    } else { ESP_LOGW(TAG,"Missing, corrupt or incompatible configuration; entering setup"); }
    return ESP_OK;
}
AppConfig app_config_snapshot() {
    xSemaphoreTake(mutex,portMAX_DELAY); const auto c=active; xSemaphoreGive(mutex); return c;
}
esp_err_t app_config_save(const AppConfig &c) {
    if (!config_validate(c,true)) return ESP_ERR_INVALID_ARG;
    Record record{}; record.magic=RECORD_MAGIC; record.size=sizeof(AppConfig); record.config=c;
    record.crc=config_checksum(record.config);
    xSemaphoreTake(mutex,portMAX_DELAY);
    nvs_handle_t h; esp_err_t err=nvs_open("salary_thief",NVS_READWRITE,&h);
    if (err == ESP_OK) {
        err=nvs_set_blob(h,"config",&record,sizeof(record));
        if (err == ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    // Active configuration changes on reboot, so clock/TZ and all tasks share one generation.
    xSemaphoreGive(mutex);
    if (err == ESP_OK) ESP_LOGI(TAG,"Configuration committed; reboot required");
    return err;
}
esp_err_t app_config_reset() {
    xSemaphoreTake(mutex,portMAX_DELAY);
    nvs_handle_t h; esp_err_t err=nvs_open("salary_thief",NVS_READWRITE,&h);
    if (err == ESP_OK) { err=nvs_erase_all(h); if (err == ESP_OK) err=nvs_commit(h); nvs_close(h); }
    xSemaphoreGive(mutex); return err;
}

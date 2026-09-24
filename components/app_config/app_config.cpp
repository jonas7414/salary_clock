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
DisplayTheme active_theme=DisplayTheme::Classic;
DisplaySchedule active_schedule;
SemaphoreHandle_t mutex;
SemaphoreHandle_t maintenance;
}
esp_err_t app_config_init(bool *configured) {
    *configured = false;
    if (!mutex) mutex = xSemaphoreCreateMutex();
    if (!maintenance) maintenance = xSemaphoreCreateMutex();
    if (!mutex || !maintenance) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG,"NVS requires recovery; preserving existing data");
        return err;
    }
    if (err != ESP_OK) return err;
    active = config_defaults();
    active_theme = DisplayTheme::Classic;
    active_schedule = DisplaySchedule{};
    nvs_handle_t handle;
    err = nvs_open("salary_thief",NVS_READONLY,&handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    Record record{}; size_t size = sizeof(record);
    err = nvs_get_blob(handle,"config",&record,&size);
    uint32_t theme=0; size_t theme_size=sizeof(theme);
    const auto theme_error=nvs_get_blob(handle,"theme",&theme,&theme_size);
    DisplaySchedule schedule{}; size_t schedule_size=sizeof(schedule);
    const auto schedule_error=nvs_get_blob(handle,"display_hours",&schedule,&schedule_size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(record) && record.magic == RECORD_MAGIC &&
        record.size == sizeof(AppConfig) && record.crc == config_checksum(record.config) &&
        config_migrate(record.config)) {
        active=record.config; *configured=true;
        if (theme_error==ESP_OK && theme_size==sizeof(theme) && display_theme_valid(theme))
            active_theme=static_cast<DisplayTheme>(theme);
        if (schedule_error==ESP_OK && schedule_size==sizeof(schedule) && display_schedule_valid(schedule))
            active_schedule=schedule;
        ESP_LOGI(TAG,"Loaded configuration v%lu",static_cast<unsigned long>(active.version));
    } else { ESP_LOGW(TAG,"Missing, corrupt or incompatible configuration; entering setup"); }
    return ESP_OK;
}
AppConfig app_config_snapshot() {
    xSemaphoreTake(mutex,portMAX_DELAY); const auto c=active; xSemaphoreGive(mutex); return c;
}
esp_err_t app_config_save(const AppConfig &c) {
    return app_config_save(c,app_config_theme());
}
DisplayTheme app_config_theme() {
    xSemaphoreTake(mutex,portMAX_DELAY); const auto theme=active_theme; xSemaphoreGive(mutex); return theme;
}
esp_err_t app_config_save(const AppConfig &c,DisplayTheme theme) {
    return app_config_save(c,theme,app_config_display_schedule());
}
DisplaySchedule app_config_display_schedule() {
    xSemaphoreTake(mutex,portMAX_DELAY); const auto schedule=active_schedule; xSemaphoreGive(mutex); return schedule;
}
esp_err_t app_config_save(const AppConfig &c,DisplayTheme theme,const DisplaySchedule &schedule) {
    if (!config_validate(c,true)) return ESP_ERR_INVALID_ARG;
    if (!display_theme_valid(static_cast<uint32_t>(theme))) return ESP_ERR_INVALID_ARG;
    if (!display_schedule_valid(schedule)) return ESP_ERR_INVALID_ARG;
    if (!app_config_begin_ota()) return ESP_ERR_INVALID_STATE;
    Record record{}; record.magic=RECORD_MAGIC; record.size=sizeof(AppConfig); record.config=c;
    record.crc=config_checksum(record.config);
    xSemaphoreTake(mutex,portMAX_DELAY);
    nvs_handle_t h; esp_err_t err=nvs_open("salary_thief",NVS_READWRITE,&h);
    if (err == ESP_OK) {
        err=nvs_set_blob(h,"config",&record,sizeof(record));
        const uint32_t saved_theme=static_cast<uint32_t>(theme);
        if (err == ESP_OK) err=nvs_set_blob(h,"theme",&saved_theme,sizeof(saved_theme));
        if (err == ESP_OK) err=nvs_set_blob(h,"display_hours",&schedule,sizeof(schedule));
        if (err == ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    // Active configuration changes on reboot, so clock/TZ and all tasks share one generation.
    xSemaphoreGive(mutex);
    app_config_end_ota();
    if (err == ESP_OK) ESP_LOGI(TAG,"Configuration committed; reboot required");
    return err;
}
esp_err_t app_config_reset() {
    if (!app_config_begin_ota()) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex,portMAX_DELAY);
    nvs_handle_t h; esp_err_t err=nvs_open("salary_thief",NVS_READWRITE,&h);
    if (err == ESP_OK) { err=nvs_erase_all(h); if (err == ESP_OK) err=nvs_commit(h); nvs_close(h); }
    xSemaphoreGive(mutex); app_config_end_ota(); return err;
}

bool app_config_begin_ota() { return maintenance && xSemaphoreTake(maintenance,0)==pdTRUE; }
void app_config_end_ota() { xSemaphoreGive(maintenance); }
esp_err_t app_config_verify() {
    nvs_handle_t h;
    esp_err_t err=nvs_open("salary_thief",NVS_READONLY,&h);
    if (err!=ESP_OK) return err;
    Record record{}; size_t size=sizeof(record);
    err=nvs_get_blob(h,"config",&record,&size); nvs_close(h);
    if (err!=ESP_OK) return err;
    return size==sizeof(record) && record.magic==RECORD_MAGIC && record.size==sizeof(AppConfig) &&
        record.crc==config_checksum(record.config) && config_migrate(record.config) ? ESP_OK : ESP_FAIL;
}

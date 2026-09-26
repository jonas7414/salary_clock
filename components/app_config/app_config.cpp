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
DisplayPreferences active_preferences;
char ignored_version[32]{};
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
    active_preferences=DisplayPreferences{};
    ignored_version[0]=0;
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
    DisplayPreferences preferences{}; size_t preferences_size=sizeof(preferences);
    const auto preferences_error=nvs_get_blob(handle,"display_prefs",&preferences,&preferences_size);
    char ignored[32]{}; size_t ignored_size=sizeof(ignored);
    if (nvs_get_blob(handle,"ota_ignored",ignored,&ignored_size)==ESP_OK &&
        ignored_size==sizeof(ignored) && std::memchr(ignored,0,sizeof(ignored)))
        std::memcpy(ignored_version,ignored,sizeof(ignored));
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(record) && record.magic == RECORD_MAGIC &&
        record.size == sizeof(AppConfig) && record.crc == config_checksum(record.config) &&
        config_migrate(record.config)) {
        active=record.config; *configured=true;
        if (theme_error==ESP_OK && theme_size==sizeof(theme) && display_theme_valid(theme))
            active_theme=static_cast<DisplayTheme>(theme);
        if (schedule_error==ESP_OK && schedule_size==sizeof(schedule) && display_schedule_valid(schedule))
            active_schedule=schedule;
        if (preferences_error==ESP_OK && preferences_size==sizeof(preferences) && display_preferences_valid(preferences))
            active_preferences=preferences;
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
    return app_config_save(c,theme,schedule,app_config_display_preferences());
}
DisplayPreferences app_config_display_preferences() {
    xSemaphoreTake(mutex,portMAX_DELAY); const auto p=active_preferences; xSemaphoreGive(mutex); return p;
}
void app_config_ignored_version(char (&version)[32]) {
    xSemaphoreTake(mutex,portMAX_DELAY); std::memcpy(version,ignored_version,32); xSemaphoreGive(mutex);
}
esp_err_t app_config_ignore_version(const char *version) {
    if (!version || std::strlen(version)>=32) return ESP_ERR_INVALID_ARG;
    if (!app_config_begin_ota()) return ESP_ERR_INVALID_STATE;
    char value[32]{}; std::strcpy(value,version);
    xSemaphoreTake(mutex,portMAX_DELAY);
    nvs_handle_t h; auto err=nvs_open("salary_thief",NVS_READWRITE,&h);
    if (err==ESP_OK) {
        err=nvs_set_blob(h,"ota_ignored",value,sizeof(value));
        if (err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    if (err==ESP_OK) std::memcpy(ignored_version,value,32);
    xSemaphoreGive(mutex); app_config_end_ota(); return err;
}
esp_err_t app_config_save(const AppConfig &c,DisplayTheme theme,const DisplaySchedule &schedule,const DisplayPreferences &preferences) {
    if (!config_validate(c,true)) return ESP_ERR_INVALID_ARG;
    if (!display_theme_valid(static_cast<uint32_t>(theme))) return ESP_ERR_INVALID_ARG;
    if (!display_schedule_valid(schedule)) return ESP_ERR_INVALID_ARG;
    if (!display_preferences_valid(preferences)) return ESP_ERR_INVALID_ARG;
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
        if (err == ESP_OK) err=nvs_set_blob(h,"display_prefs",&preferences,sizeof(preferences));
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

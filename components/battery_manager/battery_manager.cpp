#include "battery_manager.h"
#include "app_system.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <algorithm>
namespace {
constexpr const char *TAG="battery";
constexpr adc_unit_t UNIT=ADC_UNIT_1;
constexpr adc_channel_t CHANNEL=ADC_CHANNEL_3; // GPIO4 on ESP32-S3; usable with Wi-Fi.
constexpr adc_atten_t ATTENUATION=ADC_ATTEN_DB_12;
constexpr int SAMPLE_COUNT=9;
esp_err_t initialize(adc_oneshot_unit_handle_t &adc,adc_cali_handle_t &calibration) {
    adc_oneshot_unit_init_cfg_t unit{};unit.unit_id=UNIT;
    auto err=adc_oneshot_new_unit(&unit,&adc);
    if (err!=ESP_OK) return err;
    adc_oneshot_chan_cfg_t channel{};channel.atten=ATTENUATION;channel.bitwidth=ADC_BITWIDTH_12;
    err=adc_oneshot_config_channel(adc,CHANNEL,&channel);
    if (err==ESP_OK) {
        adc_cali_curve_fitting_config_t config{};
        config.unit_id=UNIT;config.chan=CHANNEL;config.atten=ATTENUATION;config.bitwidth=ADC_BITWIDTH_12;
        err=adc_cali_create_scheme_curve_fitting(&config,&calibration);
    }
    if (err!=ESP_OK) { adc_oneshot_del_unit(adc);adc=nullptr; }
    return err;
}
esp_err_t sample(adc_oneshot_unit_handle_t adc,adc_cali_handle_t calibration,int &mv) {
    int readings[SAMPLE_COUNT]{};
    for (auto &value:readings) {
        const auto err=adc_oneshot_get_calibrated_result(adc,calibration,CHANNEL,&value);
        if (err!=ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    std::sort(readings,readings+SAMPLE_COUNT);
    // A source switch during sampling is not a valid cell measurement.
    if ((readings[SAMPLE_COUNT-1]-readings[0])*2>150) return ESP_ERR_INVALID_STATE;
    mv=readings[SAMPLE_COUNT/2]*2;
    return ESP_OK;
}
void task(void *) {
    adc_oneshot_unit_handle_t adc{};adc_cali_handle_t calibration{};
    const auto error=initialize(adc,calibration);
    if (error!=ESP_OK) {
        ESP_LOGW(TAG,"Calibrated battery monitor unavailable: %s",esp_err_to_name(error));
        battery_publish({BatteryState::Unavailable,0});
        vTaskDelete(nullptr);return;
    }
    ESP_LOGI(TAG,"Supply monitor on GPIO4 / ADC1 CH3; USB/5V masks battery presence");
    BatteryMonitor monitor;
    TickType_t wake=xTaskGetTickCount();
    while (true) {
        int mv=0;
        if (sample(adc,calibration,mv)!=ESP_OK) mv=0;
        battery_publish(monitor.update(mv));
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
    }
}
}
esp_err_t battery_manager_start() {
    const auto created=xTaskCreate(task,"battery",3072,nullptr,1,nullptr);
    if (created==pdPASS) return ESP_OK;
    battery_publish({BatteryState::Unavailable,0});
    return ESP_ERR_NO_MEM;
}

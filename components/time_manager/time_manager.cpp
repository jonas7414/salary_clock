#include "time_manager.h"
#include "app_config.h"
#include "app_system.h"
#include "ds3231.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <cstdlib>
#include <atomic>
#include <sys/time.h>
namespace {
std::atomic<bool> rtc_write_pending{false};
void synced(struct timeval *) {
    xEventGroupSetBits(system_events(),TIME_SYNCED_BIT|SNTP_SYNCED_BIT);
    time_wait_publish(false);
    // The lwIP callback must not block on I2C or animation. The time task owns RTC I/O.
    rtc_write_pending.store(true);
    ESP_LOGI("time","SNTP synchronized");
}
void rtc_begin_activity(RtcStatus &status,RtcOperation operation) {
    status.operation=operation;status.result=RtcResult::Pending;
    status.activity_started_us=esp_timer_get_time();
    rtc_publish(status);
}
void rtc_end_activity(RtcStatus &status,esp_err_t error) {
    status.valid=error==ESP_OK;
    status.result=status.valid?RtcResult::Success:RtcResult::Failed;
    rtc_publish(status);
    if (status.valid) xEventGroupSetBits(system_events(),RTC_READY_BIT);
    else xEventGroupClearBits(system_events(),RTC_READY_BIT);
}
void task(void *) {
    Ds3231 rtc;
    RtcStatus rtc_status{};
    system_heartbeat(CriticalTask::Time);
    const auto detected=rtc.begin();
    if (detected==ESP_OK) {
        rtc_status.present=true;
        ESP_LOGI("time","DS3231 detected at 0x68 (SDA GPIO18, SCL GPIO17)");
        rtc_begin_activity(rtc_status,RtcOperation::Read);
        time_t utc{};
        auto err=rtc.read_utc(utc);
        if (err==ESP_OK) {
            const timeval now{utc,0};
            if (settimeofday(&now,nullptr)!=0) err=ESP_FAIL;
        }
        rtc_end_activity(rtc_status,err);
        if (err==ESP_OK) {
            xEventGroupSetBits(system_events(),TIME_SYNCED_BIT);
            time_wait_publish(false);
            ESP_LOGI("time","System time restored from DS3231 (UTC)");
        } else ESP_LOGW("time","RTC time unavailable (%s); waiting for SNTP",esp_err_to_name(err));
    } else ESP_LOGI("time","No usable DS3231 (%s); using the existing SNTP clock",esp_err_to_name(detected));
    // Waiting for an access point is healthy; it must not fail boot probation.
    while (true) {
        system_heartbeat(CriticalTask::Time);
        if (xEventGroupWaitBits(system_events(),WIFI_CONNECTED_BIT,pdFALSE,pdTRUE,pdMS_TO_TICKS(1000))&WIFI_CONNECTED_BIT) break;
    }
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,ESP_SNTP_SERVER_LIST("pool.ntp.org","time.cloudflare.com"));
    cfg.sync_cb=synced;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
    int64_t start=esp_timer_get_time();
    TickType_t wake=xTaskGetTickCount(); bool warned=false;
    unsigned write_attempts_left=0;
    int64_t retry_at=0;
    while (true) {
        const auto bits=xEventGroupGetBits(system_events());
        const auto now=esp_timer_get_time();
        if (rtc_write_pending.exchange(false) && rtc_status.present) {
            write_attempts_left=3;retry_at=now;
        }
        if (write_attempts_left && now>=retry_at && (bits&WIFI_CONNECTED_BIT)) {
            rtc_begin_activity(rtc_status,RtcOperation::Write);
            const auto err=rtc.write_utc(time(nullptr));
            rtc_end_activity(rtc_status,err);
            if (err==ESP_OK) {
                write_attempts_left=0;
                ESP_LOGI("time","SNTP time saved to DS3231 and verified (UTC)");
            } else {
                --write_attempts_left;retry_at=esp_timer_get_time()+30000000;
                ESP_LOGW("time","DS3231 sync failed: %s (%u retries left)",esp_err_to_name(err),write_attempts_left);
            }
        }
        if (!(bits & TIME_SYNCED_BIT) && !warned && esp_timer_get_time()-start > 30000000) {
            ESP_LOGW("time","Still waiting for SNTP; salary remains disabled");
            time_wait_publish(true); warned=true;
        }
        // SNTP continues periodic retries. Never clear the valid clock on a Wi-Fi disconnect.
        system_heartbeat(CriticalTask::Time);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
    }
}
}
esp_err_t time_manager_start() {
    const auto config=app_config_snapshot();
    if (setenv("TZ",timezone_posix(config.timezone),1)!=0) return ESP_FAIL;
    tzset();
    return xTaskCreate(task,"time",TASK_STACK_TIME,nullptr,TASK_PRIORITY_TIME,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

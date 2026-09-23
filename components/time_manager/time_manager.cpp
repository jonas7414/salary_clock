#include "time_manager.h"
#include "app_config.h"
#include "app_system.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <cstdlib>
namespace {
void synced(struct timeval *) {
    xEventGroupSetBits(system_events(),TIME_SYNCED_BIT);
    time_wait_publish(false);
    ESP_LOGI("time","SNTP synchronized");
}
void task(void *) {
    xEventGroupWaitBits(system_events(),WIFI_CONNECTED_BIT,pdFALSE,pdTRUE,portMAX_DELAY);
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,ESP_SNTP_SERVER_LIST("pool.ntp.org","time.cloudflare.com"));
    cfg.sync_cb=synced;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
    int64_t start=esp_timer_get_time();
    TickType_t wake=xTaskGetTickCount(); bool warned=false;
    while (true) {
        const auto bits=xEventGroupGetBits(system_events());
        if (!(bits & TIME_SYNCED_BIT) && !warned && esp_timer_get_time()-start > 30000000) {
            ESP_LOGW("time","Still waiting for SNTP; salary remains disabled");
            time_wait_publish(true); warned=true;
        }
        // SNTP continues periodic retries. Never clear the valid clock on a Wi-Fi disconnect.
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

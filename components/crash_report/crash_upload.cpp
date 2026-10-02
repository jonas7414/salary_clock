#include "crash_report.h"
#include "crash_payload.h"
#include "app_config.h"
#include "app_system.h"
#include "ota_manager.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

#if !defined(SALARY_CLOCK_PUBLIC_BUILD) && __has_include("loki_credentials.h")
#include "loki_credentials.h"
#else
#define SALARY_CLOCK_LOKI_USER ""
#define SALARY_CLOCK_LOKI_TOKEN ""
#endif

namespace {
constexpr const char *TAG="crash_upload";
constexpr const char *URL="https://logs-prod-030.grafana.net/loki/api/v1/push";
QueueHandle_t commands;
enum class Command { Next,Confirm };
portMUX_TYPE status_lock=portMUX_INITIALIZER_UNLOCKED;
CrashReportStatus published;
// Only the worker owns these values; no allocations proportional to dump size.
CrashDetails details;
const esp_partition_t *partition;

void publish(const CrashReportStatus &status) {
    // Worker is the sole publisher, including ownership of the modal event bit.
    if (crash_report_visible(status)) xEventGroupSetBits(system_events(),CRASH_REPORT_ACTIVE_BIT);
    else xEventGroupClearBits(system_events(),CRASH_REPORT_ACTIVE_BIT);
    portENTER_CRITICAL(&status_lock); published=status; portEXIT_CRITICAL(&status_lock);
}
bool network_ready() {
    const auto bits=xEventGroupGetBits(system_events());
    constexpr auto needed=WIFI_CONNECTED_BIT|TIME_SYNCED_BIT|CONFIG_READY_BIT;
    return (bits&needed)==needed && !(bits&(SETUP_MODE_BIT|SYSTEM_ERROR_BIT|SLEEP_REQUESTED_BIT|OTA_ACTIVE_BIT));
}
bool read_record() {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_COREDUMP,nullptr);
    uint32_t size=0;
    if (!partition || esp_partition_read(partition,0,&size,sizeof(size))!=ESP_OK ||
        size<28 || size>partition->size || esp_core_dump_image_check()!=ESP_OK) return false;
    esp_core_dump_summary_t summary{};
    if (esp_core_dump_get_summary(&summary)!=ESP_OK) return false;
    uint8_t mac[6]{};
    if (esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return false;
    std::snprintf(details.device,sizeof(details.device),"salary-clock-%02X%02X%02X%02X%02X%02X",
                  mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    esp_core_dump_get_panic_reason(details.reason,sizeof(details.reason));
    if (!details.reason[0]) std::strcpy(details.reason,"panic (reason unavailable)");
    std::snprintf(details.task,sizeof(details.task),"%.16s",summary.exc_task);
    std::snprintf(details.elf_sha256,sizeof(details.elf_sha256),"%.*s",int(sizeof(summary.app_elf_sha256)),
                  reinterpret_cast<const char*>(summary.app_elf_sha256));
    details.pc=summary.exc_pc; details.exception=summary.ex_info.exc_cause; details.dump_bytes=size;
    details.depth=std::min<uint32_t>(summary.exc_bt_info.depth,16);
    std::copy_n(summary.exc_bt_info.bt,details.depth,details.backtrace);
    details.backtrace_corrupted=summary.exc_bt_info.corrupted;
    uint32_t checksum=0;
    if (esp_partition_read(partition,size-4,&checksum,sizeof(checksum))!=ESP_OK) return false;
    std::snprintf(details.report_id,sizeof(details.report_id),"%08lx-%08lx",
                  static_cast<unsigned long>(size),static_cast<unsigned long>(checksum));
    return true;
}

bool upload(int &http_status) {
    if (!network_ready()) return false;
    timeval now{}; gettimeofday(&now,nullptr);
    char *payload=crash_report_payload(details,APP_FIRMWARE_VERSION,int64_t(now.tv_sec)*1000000+now.tv_usec);
    if (!payload) return false;
    esp_http_client_config_t config{};
    config.url=URL; config.username=SALARY_CLOCK_LOKI_USER; config.password=SALARY_CLOCK_LOKI_TOKEN;
    config.auth_type=HTTP_AUTH_TYPE_BASIC; config.method=HTTP_METHOD_POST;
    config.crt_bundle_attach=esp_crt_bundle_attach; config.disable_auto_redirect=true;
    config.timeout_ms=8000; config.buffer_size=512; config.buffer_size_tx=512;
    auto client=esp_http_client_init(&config);
    bool ok=false;
    if (client) {
        const size_t size=std::strlen(payload);
        const int64_t deadline=esp_timer_get_time()+25000000;
        auto ready=[&]() {
            const auto remaining=(deadline-esp_timer_get_time())/1000;
            return remaining>0 && network_ready() &&
                esp_http_client_set_timeout_ms(client,int(std::min<int64_t>(remaining,8000)))==ESP_OK;
        };
        ok=esp_http_client_set_header(client,"Content-Type","application/json")==ESP_OK &&
           ready() && esp_http_client_open(client,int(size))==ESP_OK;
        size_t sent=0;
        while (ok && sent<size) {
            if (!ready()) { ok=false; break; }
            const int count=esp_http_client_write(client,payload+sent,int(size-sent));
            if (count<=0) ok=false;
            else sent+=size_t(count);
        }
        if (ok && ready()) {
            const auto length=esp_http_client_fetch_headers(client);
            http_status=esp_http_client_get_status_code(client);
            ok=crash_report_acknowledged(length>=0,http_status);
        } else ok=false;
        esp_http_client_cleanup(client);
    }
    cJSON_free(payload);
    ESP_LOGI(TAG,"Report %s: HTTP %d, %s",details.report_id,http_status,ok?"accepted":"retained locally");
    return ok;
}

CrashReportState send_and_clear(int &http_status) {
    if (!network_ready() || !system_download_begin(pdMS_TO_TICKS(2000))) return CrashReportState::Failed;
    struct DownloadGuard { ~DownloadGuard() { system_download_end(); } } download_guard;
    if (!app_config_begin_ota()) return CrashReportState::Failed;
    struct FlashGuard { ~FlashGuard() { app_config_end_ota(); } } flash_guard;
    return crash_report_deliver([&]() { return upload(http_status); },[&]() {
        // Clear the single record only after Loki has acknowledged it.
        const auto error=esp_partition_erase_range(partition,0,partition->size);
        if (error!=ESP_OK) ESP_LOGE(TAG,"Report accepted but flash clear failed: %s",esp_err_to_name(error));
        return error==ESP_OK;
    });
}

void worker(void *) {
    if (!read_record()) { vTaskDelete(nullptr); return; }
    CrashReportPolicy policy; policy.begin(esp_timer_get_time());
    // Let boot animation and service startup settle; no cloud request before consent.
    vTaskDelay(pdMS_TO_TICKS(4000));
    for (;;) {
        const auto ota=ota_get_status();
        const bool ui_ready=!ota.busy && !ota.prompt && !ota.foreground && !device_snapshot().brightness_editing;
        policy.poll(esp_timer_get_time(),network_ready(),ui_ready);
        Command command{};
        if (xQueueReceive(commands,&command,0)==pdTRUE && ui_ready) {
            if (command==Command::Next) policy.next();
            else policy.confirm(network_ready());
        }
        publish(policy.status);
        if (policy.status.state==CrashReportState::Hidden) { vTaskDelete(nullptr); return; }
        if (policy.status.state==CrashReportState::Sending) {
            int http_status=0;
            const auto result=send_and_clear(http_status);
            policy.complete(result!=CrashReportState::Failed,result==CrashReportState::Sent,http_status);
            publish(policy.status);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
}

esp_err_t crash_report_start() {
    if (!SALARY_CLOCK_LOKI_USER[0] || !SALARY_CLOCK_LOKI_TOKEN[0]) {
        ESP_LOGI(TAG,"No local Loki credentials; USB crash diagnostics remain available"); return ESP_OK;
    }
    if (commands) return ESP_ERR_INVALID_STATE;
    commands=xQueueCreate(4,sizeof(Command));
    if (!commands) return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker,"crash_report",8192,nullptr,1,nullptr)!=pdPASS) {
        vQueueDelete(commands); commands=nullptr; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
CrashReportStatus crash_report_snapshot() {
    portENTER_CRITICAL(&status_lock); const auto copy=published; portEXIT_CRITICAL(&status_lock); return copy;
}
void crash_report_next() {
    if (commands) { const auto command=Command::Next; xQueueSend(commands,&command,0); }
}
void crash_report_confirm() {
    if (commands) { const auto command=Command::Confirm; xQueueSend(commands,&command,0); }
}

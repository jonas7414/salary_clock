#include "calendar_manager.h"
#include "calendar_download.h"
#include "app_config.h"
#include "app_system.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <ctime>
namespace {
constexpr char TAG[]="calendar";
constexpr int64_t MAX_RESPONSE=160*1024, REQUEST_US=60*1000000;
CalendarCache cache;
bool ready() {
    const auto bits=xEventGroupGetBits(system_events());
    const auto required=WIFI_CONNECTED_BIT|SNTP_SYNCED_BIT|CONFIG_READY_BIT;
    return (bits&required)==required && !(bits&(OTA_ACTIVE_BIT|SLEEP_REQUESTED_BIT|SETUP_MODE_BIT|SYSTEM_ERROR_BIT));
}
void publish(bool updating,bool success) {
    CalendarStatus s{}; s.years[0]=cache.years[0].year; s.years[1]=cache.years[1].year;
    s.last_attempt_day=cache.last_attempt_day; s.updating=updating; s.last_check_success=success;
    calendar_publish(s);
}
void load() {
    nvs_handle_t handle;
    if (nvs_open("calendar",NVS_READONLY,&handle)!=ESP_OK) return;
    CalendarCache stored{}; size_t size=sizeof(stored);
    const auto error=nvs_get_blob(handle,"cache",&stored,&size); nvs_close(handle);
    if (error==ESP_OK && size==sizeof(stored) && calendar_cache_valid(stored)) {
        cache=stored;
        taiwan_calendar_install(cache.years);
        ESP_LOGI(TAG,"Offline calendar cache restored: %ld / %ld",long(cache.years[0].year),long(cache.years[1].year));
    } else if (error!=ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG,"Invalid cache; using built-in calendar");
}
bool save(CalendarCache candidate) {
    if (!ready() || !app_config_begin_ota()) return false;
    calendar_cache_seal(candidate);
    nvs_handle_t handle;
    auto error=nvs_open("calendar",NVS_READWRITE,&handle);
    if (error==ESP_OK) {
        error=nvs_set_blob(handle,"cache",&candidate,sizeof(candidate));
        if (error==ESP_OK) error=nvs_commit(handle);
        nvs_close(handle);
    }
    app_config_end_ota();
    if (error!=ESP_OK) { ESP_LOGW(TAG,"Cache write failed: %s",esp_err_to_name(error)); return false; }
    cache=candidate;
    taiwan_calendar_install(cache.years);
    return true;
}
struct Stream {
    esp_http_client_handle_t client{};
    char buffer[1024]{};
    int used{},position{};
    int64_t received{},deadline{};
    bool bad_encoding{};
    ~Stream() { if (client) esp_http_client_cleanup(client); }
    static esp_err_t event(esp_http_client_event_t *e) {
        if (e->event_id==HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key,"Content-Encoding") &&
            strcasecmp(e->header_value,"identity")) static_cast<Stream*>(e->user_data)->bad_encoding=true;
        return ESP_OK;
    }
    static int read(void *context) {
        auto &s=*static_cast<Stream*>(context);
        if (s.position<s.used) return uint8_t(s.buffer[s.position++]);
        if (!ready() || esp_timer_get_time()>=s.deadline) return -2;
        s.used=esp_http_client_read(s.client,s.buffer,sizeof(s.buffer)); s.position=0;
        if (s.used<0) return -2;
        if (!s.used) return esp_http_client_is_complete_data_received(s.client) ? -1 : -2;
        s.received+=s.used;
        if (s.received>MAX_RESPONSE) return -2;
        // Cooperate with idle/watchdog even on fast Wi-Fi.
        vTaskDelay(1);
        return uint8_t(s.buffer[s.position++]);
    }
};
bool download(int year,CalendarYear &result) {
    if (!ready()) return false;
    char url[100]; std::snprintf(url,sizeof(url),"https://allen0099.github.io/taiwan-calendar/%d/all.json",year);
    Stream stream; stream.deadline=esp_timer_get_time()+REQUEST_US;
    esp_http_client_config_t config{};
    config.url=url; config.timeout_ms=5000; config.disable_auto_redirect=true;
    config.crt_bundle_attach=esp_crt_bundle_attach; config.transport_type=HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size=1024; config.buffer_size_tx=1024;
    config.user_agent="SalaryClock/" APP_FIRMWARE_VERSION;
    config.event_handler=Stream::event; config.user_data=&stream;
    stream.client=esp_http_client_init(&config);
    if (!stream.client) return false;
    esp_http_client_set_header(stream.client,"Accept-Encoding","identity");
    if (esp_http_client_open(stream.client,0)!=ESP_OK) return false;
    const int64_t length=esp_http_client_fetch_headers(stream.client);
    const int status=esp_http_client_get_status_code(stream.client);
    if (length<0 || length>MAX_RESPONSE || status!=200 || stream.bad_encoding) {
        ESP_LOGW(TAG,"Year %d unavailable (HTTP %d); keeping existing data",year,status); return false;
    }
    if (!calendar_parse_year(Stream::read,&stream,year,result) || !ready() ||
        esp_timer_get_time()>=stream.deadline ||
        (!esp_http_client_is_chunked_response(stream.client) && length>0 && stream.received!=length)) {
        ESP_LOGW(TAG,"Year %d incomplete or invalid; keeping existing data",year); return false;
    }
    return true;
}
void task(void *) {
    // Keep network allocation out of boot probation; cached data is already usable.
    vTaskDelay(pdMS_TO_TICKS(60000));
    while (true) {
        if (ready()) {
            const time_t now=time(nullptr); tm date{}; gmtime_r(&now,&date);
            const int year=date.tm_year+1900;
            const int32_t today=int32_t(now/86400);
            if (year>=2026 && year<9999 && calendar_check_due(cache,today) && system_download_begin(0)) {
                CalendarCache attempt=cache; attempt.last_attempt_day=today;
                // Persist the daily limit before requesting either year, including failures.
                if (save(attempt)) {
                    publish(true,false);
                    bool success=true;
                    for (int y=year;y<=year+1;++y) {
                        CalendarYear downloaded{};
                        if (!download(y,downloaded)) { success=false; continue; }
                        CalendarCache candidate=cache; calendar_cache_replace(candidate,downloaded);
                        if (std::memcmp(candidate.years,cache.years,sizeof(cache.years)) && !save(candidate)) success=false;
                        else ESP_LOGI(TAG,"Calendar %d verified and available offline",y);
                    }
                    publish(false,success);
                }
                system_download_end();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
}
esp_err_t calendar_manager_start() {
    load(); publish(false,false);
    return xTaskCreate(task,"calendar",6144,nullptr,1,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

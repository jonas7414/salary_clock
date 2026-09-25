#include "ota_manager.h"
#include "ota_config.h"
#include "ota_policy.h"
#include "ota_transfer.h"
#include "ota_retry.h"
#include "app_config.h"
#include "app_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_image_format.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
constexpr const char *TAG="ota";
constexpr EventBits_t READY=WIFI_CONNECTED_BIT|TIME_SYNCED_BIT|CONFIG_READY_BIT;
SemaphoreHandle_t status_mutex;
QueueHandle_t requests;
OtaStatus status;
OtaCallback callback;
void *callback_context;
enum class Request { Check, Install };
bool ready() {
    const auto bits=xEventGroupGetBits(system_events());
    return (bits&READY)==READY && !(bits&(SETUP_MODE_BIT|SYSTEM_ERROR_BIT));
}
void publish(OtaState state,const char *message,esp_err_t error=ESP_OK,
             OtaEvent event=OtaEvent::PROGRESS,bool notify=false) {
    xSemaphoreTake(status_mutex,portMAX_DELAY);
    status.state=state; status.error=error;
    snprintf(status.message,sizeof(status.message),"%s",message);
    const auto copy=status; const auto fn=callback; void *context=callback_context;
    xSemaphoreGive(status_mutex);
    ESP_LOGI(TAG,"%s: %s",ota_state_name(state),message);
    if (notify && fn) fn(event,copy,context);
}
void progress(uint32_t bytes,uint32_t total) {
    xSemaphoreTake(status_mutex,portMAX_DELAY);
    const unsigned percent=total ? uint64_t(bytes)*100/total : 0;
    const bool changed=percent!=status.percentage;
    status.downloaded_bytes=bytes; status.total_bytes=total; status.percentage=percent;
    const auto copy=status; const auto fn=callback; void *context=callback_context;
    xSemaphoreGive(status_mutex);
    if (changed && fn) fn(OtaEvent::PROGRESS,copy,context);
    if (changed && percent%10==0) ESP_LOGI(TAG,"Download %u%%",percent);
}
// Every redirect is reopened with a fresh verified TLS client. Never log signed URLs.
class HttpStream {
public:
    esp_http_client_handle_t client{};
    int64_t deadline{},length{-1};
    char location[ota_config::MAX_REDIRECT_URL]{};
    bool bad_header{};
    bool retryable_failure{};
    uint32_t received{};
    ~HttpStream() { close(); }
    void close() { if (client) { esp_http_client_cleanup(client); client=nullptr; } }
    static esp_err_t event(esp_http_client_event_t *e) {
        auto &self=*static_cast<HttpStream*>(e->user_data);
        if (e->event_id==HTTP_EVENT_ON_HEADER) {
            if (!strcasecmp(e->header_key,"Location")) {
                if (self.location[0] || strlen(e->header_value)>=sizeof(self.location)) self.bad_header=true;
                else strcpy(self.location,e->header_value);
            }
            if (!strcasecmp(e->header_key,"Content-Encoding") && strcasecmp(e->header_value,"identity")) self.bad_header=true;
        }
        return ESP_OK;
    }
    bool open(const char *initial,int64_t timeout,int socket_timeout=ota_config::HTTP_TIMEOUT_MS,
              int64_t operation_deadline=INT64_MAX) {
        deadline=std::min(esp_timer_get_time()+timeout,operation_deadline);
        retryable_failure=false; received=0;
        // Heap storage avoids large redirect URLs on the worker stack.
        std::unique_ptr<char[]> url(new(std::nothrow) char[ota_config::MAX_REDIRECT_URL]);
        if (!url || strlen(initial)>=ota_config::MAX_REDIRECT_URL) return false;
        strcpy(url.get(),initial);
        for (unsigned hop=0;hop<=ota_config::MAX_REDIRECTS;++hop) {
            if (!ready() || esp_timer_get_time()>=deadline || !ota::https_url_allowed(url.get())) return false;
            location[0]=0; bad_header=false;
            esp_http_client_config_t config{};
            config.url=url.get(); config.user_agent="SalaryClock/" APP_FIRMWARE_VERSION;
            config.timeout_ms=socket_timeout; config.disable_auto_redirect=true;
            config.transport_type=HTTP_TRANSPORT_OVER_SSL; config.crt_bundle_attach=esp_crt_bundle_attach;
            config.buffer_size=4096; config.buffer_size_tx=4096;
            config.event_handler=event; config.user_data=this;
            client=esp_http_client_init(&config);
            if (!client) return false;
            esp_http_client_set_header(client,"Accept","application/vnd.github+json, application/octet-stream");
            esp_http_client_set_header(client,"Accept-Encoding","identity");
            esp_http_client_set_header(client,"X-GitHub-Api-Version","2022-11-28");
            const auto opened=esp_http_client_open(client,0);
            if (opened!=ESP_OK) {
                retryable_failure=opened==ESP_ERR_HTTP_CONNECT || opened==ESP_ERR_HTTP_WRITE_DATA;
                ESP_LOGW(TAG,"HTTPS open failed: %s",esp_err_to_name(opened));
                return false;
            }
            length=esp_http_client_fetch_headers(client);
            const int code=esp_http_client_get_status_code(client);
            xSemaphoreTake(status_mutex,portMAX_DELAY); status.http_status=code; xSemaphoreGive(status_mutex);
            if (length<0) { retryable_failure=true; return false; }
            if (bad_header) return false;
            if (esp_http_client_is_chunked_response(client)) length=-1;
            if (code==200) return true;
            if (code!=301 && code!=302 && code!=303 && code!=307 && code!=308) {
                ESP_LOGW(TAG,"HTTP %d; retry on next boot or manual request",code);
                return false;
            }
            if (!ota::https_url_allowed(location)) return false;
            strcpy(url.get(),location); close();
        }
        return false;
    }
    int read(void *buffer,size_t size) {
        const auto allowed=[&]() { return ready() && esp_timer_get_time()<deadline; };
        retryable_failure=false;
        const int count=ota::read_with_retry([&]() {
            // Capture errno immediately: other IDF calls or logging may change it.
            errno=0;
            const int n=esp_http_client_read(client,static_cast<char*>(buffer),size);
            const int socket_error=errno;
            const bool complete=esp_http_client_is_complete_data_received(client);
            const bool transient=n==-ESP_ERR_HTTP_EAGAIN ||
                (n<=0 && (socket_error==EAGAIN || socket_error==EWOULDBLOCK || socket_error==ETIMEDOUT));
            retryable_failure=n<=0 && !complete;
            if (n<=0 && !complete) ESP_LOGW(TAG,"Read paused at %lu bytes: result=%d errno=%d transient=%d",
                (unsigned long)received,n,socket_error,transient);
            return ota::ReadAttempt{n,complete,transient};
        },allowed,[&](unsigned attempt) {
            ESP_LOGW(TAG,"Waiting for HTTPS data; retry %u/%u",attempt,ota_config::READ_TIMEOUT_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(ota_config::READ_RETRY_DELAY_MS*attempt));
        },ota_config::READ_TIMEOUT_RETRIES);
        if (count>0) received+=count;
        if (!allowed()) {
            retryable_failure=false;
            ESP_LOGW(TAG,"Read cancelled: network unavailable or operation deadline reached");
        }
        return count;
    }
};
bool fetch_text(const char *url,char *output,size_t capacity,size_t &length) {
    std::unique_ptr<HttpStream> stream(new(std::nothrow) HttpStream);
    if (!stream || !stream->open(url,ota_config::REQUEST_DEADLINE_US) || stream->length>int64_t(capacity)) return false;
    length=0;
    while (length<capacity) {
        const int n=stream->read(output+length,capacity-length);
        if (n<0) return false;
        if (!n) { output[length]=0; return stream->length<0 || int64_t(length)==stream->length; }
        length+=n;
        vTaskDelay(1);
    }
    char extra;
    if (stream->read(&extra,1)!=0) return false;
    output[length]=0;
    return stream->length<0 || int64_t(length)==stream->length;
}
struct Maintenance {
    bool held{app_config_begin_ota()};
    Maintenance() { if (held) xEventGroupSetBits(system_events(),OTA_ACTIVE_BIT); }
    ~Maintenance() { if (held) { xEventGroupClearBits(system_events(),OTA_ACTIVE_BIT); app_config_end_ota(); } }
};
struct ImageWriter {
    esp_ota_handle_t handle{};
    mbedtls_sha256_context sha;
    ImageWriter() { mbedtls_sha256_init(&sha); }
    ~ImageWriter() { if (handle) esp_ota_abort(handle); mbedtls_sha256_free(&sha); }
    bool finish() { const auto h=handle; handle=0; return esp_ota_end(h)==ESP_OK; }
};
bool install_attempt(const ota::Release &release,const char *&reason,bool &retryable,int64_t deadline) {
    retryable=false;
    reason="Configuration is being saved";
    Maintenance maintenance;
    if (!maintenance.held) return false;
    const auto *running=esp_ota_get_running_partition();
    const auto *target=esp_ota_get_next_update_partition(nullptr);
    reason="No suitable inactive OTA partition";
    if (!target || !running || target->address==running->address || release.size>target->size ||
        (target->subtype!=ESP_PARTITION_SUBTYPE_APP_OTA_0 && target->subtype!=ESP_PARTITION_SUBTYPE_APP_OTA_1)) return false;
    const auto *invalid=esp_ota_get_last_invalid_partition();
    esp_app_desc_t bad{};
    if (invalid && esp_ota_get_partition_description(invalid,&bad)==ESP_OK &&
        !strncmp(bad.version,release.version,sizeof(bad.version))) {
        reason="Release previously failed boot validation; publish a newer version"; return false;
    }
    std::unique_ptr<HttpStream> stream(new(std::nothrow) HttpStream);
    std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[ota_config::STREAM_BUFFER_SIZE]);
    reason="Insufficient memory for OTA";
    if (!stream || !buffer) return false;
    publish(OtaState::DOWNLOADING,"Downloading inactive partition",ESP_OK,OtaEvent::START,true);
    progress(0,release.size);
    reason="Firmware HTTPS connection or length failed";
    if (!stream->open(release.firmware_url,ota_config::DOWNLOAD_DEADLINE_US,
                      ota_config::FIRMWARE_HTTP_TIMEOUT_MS,deadline)) {
        retryable=stream->retryable_failure; return false;
    }
    if (stream->length>=0 && stream->length!=release.size) return false;
    constexpr size_t prefix_size=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t);
    size_t prefix=0;
    while (prefix<prefix_size) {
        const int n=stream->read(buffer.get()+prefix,prefix_size-prefix);
        if (n<=0) { retryable=stream->retryable_failure; return false; }
        prefix+=n;
    }
    esp_image_header_t header{}; esp_app_desc_t description{};
    memcpy(&header,buffer.get(),sizeof(header));
    memcpy(&description,buffer.get()+sizeof(header)+sizeof(esp_image_segment_header_t),sizeof(description));
    reason="Firmware target, project or version mismatch";
    if (header.magic!=ESP_IMAGE_HEADER_MAGIC || header.chip_id!=ESP_CHIP_ID_ESP32S3 ||
        description.magic_word!=ESP_APP_DESC_MAGIC_WORD ||
        strncmp(description.version,release.version,sizeof(description.version)) ||
        strncmp(description.project_name,esp_app_get_description()->project_name,sizeof(description.project_name))) return false;
    ImageWriter writer;
    reason="Could not begin inactive partition write";
    if (esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&writer.handle)!=ESP_OK || mbedtls_sha256_starts(&writer.sha,0)!=0) return false;
    const auto result=ota::transfer_image(buffer.get(),ota_config::STREAM_BUFFER_SIZE,prefix,release.size,
        [&](uint8_t *data,size_t size) { return stream->read(data,size); },
        [&](const uint8_t *data,size_t size) {
            return esp_ota_write(writer.handle,data,size)==ESP_OK && mbedtls_sha256_update(&writer.sha,data,size)==0;
        },
        [&]() {
            stream.reset();
            publish(OtaState::VERIFYING,"Checking SHA-256 and ESP image",ESP_OK,OtaEvent::VERIFY,true);
            uint8_t digest[32];
            return mbedtls_sha256_finish(&writer.sha,digest)==0 && !memcmp(digest,release.sha256,32);
        },
        [&]() {
            if (!writer.finish() || mbedtls_sha256_starts(&writer.sha,0)!=0) return false;
            // Read back actual flash bytes before changing boot metadata.
            for (size_t offset=0;offset<release.size;) {
                const size_t bytes=std::min(size_t(release.size)-offset,ota_config::STREAM_BUFFER_SIZE);
                if (esp_partition_read(target,offset,buffer.get(),bytes)!=ESP_OK ||
                    mbedtls_sha256_update(&writer.sha,buffer.get(),bytes)!=0) return false;
                offset+=bytes; vTaskDelay(1);
            }
            uint8_t digest[32];
            return mbedtls_sha256_finish(&writer.sha,digest)==0 && !memcmp(digest,release.sha256,32);
        },
        [&]() { return esp_ota_set_boot_partition(target)==ESP_OK; },
        [&](uint32_t received,uint32_t total) { progress(received,total); vTaskDelay(1); });
    switch(result) {
        case ota::TransferResult::Success:return true;
        case ota::TransferResult::Incomplete:
            reason="Firmware download interrupted or incomplete";
            retryable=stream && stream->retryable_failure;
            break;
        case ota::TransferResult::Oversize:reason="Firmware exceeds declared size";break;
        case ota::TransferResult::WriteFailed:reason="Flash write or hash operation failed";break;
        case ota::TransferResult::DigestFailed:reason="Firmware SHA-256 mismatch";break;
        case ota::TransferResult::ImageFailed:reason="ESP image or flash readback validation failed";break;
        case ota::TransferResult::ActivateFailed:reason="Boot partition switch failed";break;
    }
    return false;
}
bool install(const ota::Release &release,const char *&reason) {
    const int64_t deadline=esp_timer_get_time()+ota_config::DOWNLOAD_DEADLINE_US;
    unsigned attempt=0;
    return ota::download_with_retry([&]() {
        ESP_LOGI(TAG,"Firmware download attempt %u/%u",++attempt,ota_config::DOWNLOAD_ATTEMPTS);
        bool retryable=false;
        if (install_attempt(release,reason,retryable,deadline)) return ota::DownloadAttempt::Success;
        return retryable ? ota::DownloadAttempt::Retryable : ota::DownloadAttempt::Failed;
    },[&]() {
        const bool available=ready() && esp_timer_get_time()<deadline;
        if (!available) reason="Network unavailable or firmware download deadline reached";
        return available;
    },[&](unsigned retry) {
        ESP_LOGW(TAG,"%s; restarting download from byte zero",reason);
        vTaskDelay(pdMS_TO_TICKS(ota_config::DOWNLOAD_RETRY_DELAY_MS*retry));
    },ota_config::DOWNLOAD_ATTEMPTS);
}
void check(bool auto_install) {
    if (!system_download_begin(portMAX_DELAY)) return;
    struct DownloadGuard { ~DownloadGuard() { system_download_end(); } } download_guard;
    xSemaphoreTake(status_mutex,portMAX_DELAY);
    status.downloaded_bytes=0; status.total_bytes=0; status.percentage=0; status.http_status=0;
    status.latest_version[0]=0;
    xSemaphoreGive(status_mutex);
    ESP_LOGI(TAG,"Current %s; heap free=%lu minimum=%lu",APP_FIRMWARE_VERSION,
        (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size());
    publish(OtaState::CHECKING,"Checking latest stable release");
    const char *reason="Release HTTPS request failed";
    std::unique_ptr<char[]> json(new(std::nothrow) char[ota::MAX_RELEASE_JSON+1]);
    size_t length=0; ota::Release release{};
    char url[160]; snprintf(url,sizeof(url),"https://api.github.com/repos/%s/releases/latest",ota_config::REPOSITORY);
    bool ok=json && fetch_text(url,json.get(),ota::MAX_RELEASE_JSON,length);
    if (ok) ok=ota::parse_release(json.get(),length,ota_config::REPOSITORY,ota_config::ASSET,release,&reason);
    json.reset();
    ota::Version current{},latest{};
    if (ok) {
        xSemaphoreTake(status_mutex,portMAX_DELAY);
        snprintf(status.latest_version,sizeof(status.latest_version),"%s",release.version);
        xSemaphoreGive(status_mutex);
        ESP_LOGI(TAG,"Latest %s",release.version);
        ok=ota::parse_version(APP_FIRMWARE_VERSION,current) && ota::parse_version(release.version,latest);
        reason="Invalid firmware version";
    }
    if (ok && ota::compare_versions(latest,current)<=0) {
        publish(OtaState::IDLE,"Already running this version or newer"); return;
    }
    if (ok) {
        publish(OtaState::UPDATE_AVAILABLE,"New stable firmware available");
        if (!auto_install) return;
        if (release.checksum_url[0]) {
            char checksum[257]; uint8_t expected[32]; size_t count=0;
            reason="Release checksum file missing, malformed or disagrees with GitHub digest";
            ok=fetch_text(release.checksum_url,checksum,256,count) &&
                ota::parse_checksum_file(checksum,count,ota_config::ASSET,expected);
            if (ok && release.has_sha256) ok=!memcmp(expected,release.sha256,32);
            if (ok) memcpy(release.sha256,expected,32);
        }
        if (ok) ok=install(release,reason);
        if (ok) {
            publish(OtaState::READY_TO_REBOOT,"Verified; rebooting into pending firmware",ESP_OK,OtaEvent::SUCCESS,true);
            vTaskDelay(pdMS_TO_TICKS(300)); esp_restart();
        }
    }
    publish(OtaState::ERROR,reason,ESP_FAIL,OtaEvent::FAILED,true);
}
bool pending_boot() {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
}
void rollback() {
    ESP_LOGE(TAG,"Boot health failed; rolling back");
    const esp_err_t err=esp_ota_mark_app_invalid_rollback_and_reboot();
    // If no fallback exists, leave pending state intact and let the bootloader decide.
    ESP_LOGE(TAG,"Rollback returned: %s",esp_err_to_name(err));
    esp_restart();
}
void probation() {
    if (!pending_boot()) return;
    if (esp_task_wdt_add(nullptr)!=ESP_OK) { rollback(); return; }
    ESP_LOGI(TAG,"Pending firmware: requiring 30 seconds of healthy critical tasks");
    BootProbation health(esp_timer_get_time(),ota_config::PROBATION_US,ota_config::BOOT_DEADLINE_US);
    const bool nvs_ok=app_config_verify()==ESP_OK;
    for (;;) {
        esp_task_wdt_reset();
        const auto bits=xEventGroupGetBits(system_events());
        const int64_t now=esp_timer_get_time();
        const bool healthy=nvs_ok && (bits&CONFIG_READY_BIT) && (bits&WIFI_STARTED_BIT) &&
            system_tasks_healthy(now,ota_config::HEARTBEAT_MAX_AGE_US);
        const auto decision=health.update(now,healthy,(bits&SYSTEM_ERROR_BIT) || !nvs_ok);
        if (decision==BootDecision::Rollback) { rollback(); return; }
        if (decision==BootDecision::Valid) {
            if (app_config_verify()!=ESP_OK || esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) { rollback(); return; }
            esp_task_wdt_delete(nullptr);
            ESP_LOGI(TAG,"Firmware healthy; rollback cancelled"); return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void wait_ready() {
    for (;;) {
        xEventGroupWaitBits(system_events(),READY,pdFALSE,pdTRUE,portMAX_DELAY);
        if (ready()) return;
        // Setup/system errors may coexist briefly with old readiness bits.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void worker(void *) {
    probation();
    int64_t first_check=0;
    bool checked_on_boot=false;
    for (;;) {
        wait_ready();
        const int64_t now=esp_timer_get_time();
        if (!first_check) first_check=now+ota_config::INITIAL_DELAY_US;
        const int64_t delay=std::max(int64_t(0),first_check-now);
        Request request{};
        const bool manual=xQueueReceive(requests,&request,checked_on_boot ? portMAX_DELAY : pdMS_TO_TICKS((delay+999)/1000))==pdTRUE;
        if (checked_on_boot && !manual) continue;
        wait_ready(); // Preserve an accepted manual request across a disconnect.
        check(manual ? request==Request::Install : ota_config::AUTO_INSTALL);
        checked_on_boot=true;
        ESP_LOGI(TAG,"Heap free=%lu minimum=%lu; stack remaining=%u bytes",
            (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size(),
            (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
}
}
esp_err_t ota_init() {
    if (requests) return ESP_ERR_INVALID_STATE;
    status_mutex=xSemaphoreCreateMutex(); requests=xQueueCreate(4,sizeof(Request));
    if (!status_mutex || !requests || xTaskCreate(worker,"ota",TASK_STACK_OTA,nullptr,TASK_PRIORITY_OTA,nullptr)!=pdPASS) {
        if (requests) vQueueDelete(requests);
        if (status_mutex) vSemaphoreDelete(status_mutex);
        requests=nullptr; status_mutex=nullptr;
        if (pending_boot()) rollback();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t ota_check_update() {
    Request r=Request::Check; return requests && xQueueSend(requests,&r,0)==pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t ota_start_update() {
    Request r=Request::Install; return requests && xQueueSend(requests,&r,0)==pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
const char *ota_get_current_version() { return APP_FIRMWARE_VERSION; }
OtaStatus ota_get_status() {
    if (!status_mutex) return {};
    xSemaphoreTake(status_mutex,portMAX_DELAY); const auto copy=status; xSemaphoreGive(status_mutex); return copy;
}
OtaState ota_get_state() { return ota_get_status().state; }
bool ota_is_update_available() { return ota_get_state()==OtaState::UPDATE_AVAILABLE; }
void ota_set_callback(OtaCallback fn,void *context) {
    if (!status_mutex) return;
    xSemaphoreTake(status_mutex,portMAX_DELAY); callback=fn; callback_context=context; xSemaphoreGive(status_mutex);
}
const char *ota_state_name(OtaState state) {
    switch(state) {
        case OtaState::IDLE:return "IDLE";
        case OtaState::CHECKING:return "CHECKING";
        case OtaState::UPDATE_AVAILABLE:return "UPDATE_AVAILABLE";
        case OtaState::DOWNLOADING:return "DOWNLOADING";
        case OtaState::VERIFYING:return "VERIFYING";
        case OtaState::READY_TO_REBOOT:return "READY_TO_REBOOT";
        case OtaState::ERROR:return "ERROR";
    }
    return "ERROR";
}

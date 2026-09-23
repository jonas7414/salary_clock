#include "setup_portal.h"
#include "app_config.h"
#include "config_json.h"
#include "app_system.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace {
constexpr size_t MAX_BODY=2048;
constexpr uint16_t MAX_SCAN_AP=20;
httpd_handle_t server;
extern const char html_start[] asm("_binary_index_html_start");
extern const char html_end[] asm("_binary_index_html_end");
esp_err_t send_json(httpd_req_t *r,cJSON *json,const char *status="200 OK") {
    if (!json) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    char *body=cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    httpd_resp_set_status(r,status); httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store"); httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    const auto err=httpd_resp_send(r,body,HTTPD_RESP_USE_STRLEN); cJSON_free(body); return err;
}
esp_err_t error(httpd_req_t *r,const char *message,const char *status="400 Bad Request") {
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"error",message); return send_json(r,j,status);
}
esp_err_t root(httpd_req_t *r) {
    httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; object-src 'none'; frame-ancestors 'none'");
    return httpd_resp_send(r,html_start,html_end-html_start-1);
}
void add_time(cJSON *j,const char *key,uint16_t minutes) {
    const unsigned hour=(minutes/60U)%24U, minute=minutes%60U;
    char value[6]; std::snprintf(value,sizeof(value),"%02u:%02u",hour,minute); cJSON_AddStringToObject(j,key,value);
}
esp_err_t get_config(httpd_req_t *r) {
    const auto c=app_config_snapshot(); cJSON *j=cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"config_version",c.version);
    cJSON_AddStringToObject(j,"wifi_ssid",c.wifi_ssid);
    cJSON_AddBoolToObject(j,"has_password",c.wifi_password[0]!=0);
    cJSON_AddNumberToObject(j,"monthly_salary",c.monthly_salary);
    cJSON_AddNumberToObject(j,"work_days",c.work_days);
    cJSON_AddStringToObject(j,"timezone",c.timezone);
    add_time(j,"work_start",c.work_start); add_time(j,"lunch_start",c.lunch_start);
    add_time(j,"lunch_end",c.lunch_end); add_time(j,"work_end",c.work_end);
    return send_json(r,j);
}
const char *security(wifi_auth_mode_t auth) {
    switch(auth) {
        case WIFI_AUTH_OPEN:return "Open"; case WIFI_AUTH_WEP:return "WEP";
        case WIFI_AUTH_WPA_PSK:return "WPA"; case WIFI_AUTH_WPA2_PSK:return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:return "WPA/WPA2"; case WIFI_AUTH_WPA3_PSK:return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/WPA3"; case WIFI_AUTH_WPA2_ENTERPRISE:return "Enterprise";
        default:return "Other";
    }
}
esp_err_t scan(httpd_req_t *r) {
    wifi_scan_config_t cfg{}; cfg.show_hidden=true; cfg.scan_type=WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min=80; cfg.scan_time.active.max=160;
    const auto err=esp_wifi_scan_start(&cfg,true);
    if (err!=ESP_OK) { esp_wifi_clear_ap_list(); return error(r,"Scan unavailable; try again","503 Service Unavailable"); }
    uint16_t total=0; esp_wifi_scan_get_ap_num(&total);
    uint16_t count=total < MAX_SCAN_AP ? total : MAX_SCAN_AP;
    wifi_ap_record_t records[MAX_SCAN_AP]{};
    if (count && esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK) {
        esp_wifi_clear_ap_list(); return error(r,"Cannot read scan","503 Service Unavailable");
    }
    esp_wifi_clear_ap_list();
    cJSON *j=cJSON_CreateObject(); cJSON *list=cJSON_AddArrayToObject(j,"networks");
    cJSON_AddNumberToObject(j,"total",total);
    for (unsigned i=0;i<count;++i) {
        char ssid[33]{}; std::memcpy(ssid,records[i].ssid,32);
        cJSON *n=cJSON_CreateObject(); cJSON_AddStringToObject(n,"ssid",ssid);
        cJSON_AddNumberToObject(n,"rssi",records[i].rssi);
        cJSON_AddStringToObject(n,"security",security(records[i].authmode)); cJSON_AddItemToArray(list,n);
    }
    return send_json(r,j);
}
esp_err_t status(httpd_req_t *r) {
    const auto d=device_snapshot(); const auto s=salary_snapshot(); const auto c=app_config_snapshot();
    const auto bits=xEventGroupGetBits(system_events());
    cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"system_state",system_state_name(system_state()));
    cJSON_AddBoolToObject(j,"wifi_connected",bits&WIFI_CONNECTED_BIT);
    cJSON_AddBoolToObject(j,"wifi_associated",bits&WIFI_ASSOCIATED_BIT);
    cJSON_AddBoolToObject(j,"wifi_connecting",bits&WIFI_CONNECTING_BIT);
    cJSON_AddBoolToObject(j,"time_synced",bits&TIME_SYNCED_BIT);
    cJSON_AddBoolToObject(j,"sntp_wait_expired",d.sntp_wait_expired);
    cJSON_AddStringToObject(j,"ssid",d.network.ssid); cJSON_AddStringToObject(j,"ip",d.network.ip);
    cJSON_AddStringToObject(j,"ap_ssid",d.network.ap_ssid); cJSON_AddNumberToObject(j,"rssi",d.network.rssi);
    cJSON_AddStringToObject(j,"firmware_version",APP_FIRMWARE_VERSION);
    cJSON_AddStringToObject(j,"idf_version",esp_get_idf_version());
    cJSON_AddNumberToObject(j,"config_version",c.version);
    cJSON_AddNumberToObject(j,"free_heap",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j,"free_psram",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(j,"uptime_seconds",esp_timer_get_time()/1000000);
    cJSON_AddNumberToObject(j,"earned_money",s.earned_money);
    cJSON_AddNumberToObject(j,"frame_us",d.frame_us); cJSON_AddNumberToObject(j,"dropped_frames",d.dropped_frames);
    cJSON_AddBoolToObject(j,"partial_rendering",d.partial_rendering);
    return send_json(r,j);
}
bool write_allowed(httpd_req_t *r) {
    // A custom request header blocks cross-origin HTML forms. No CORS/preflight is enabled.
    char header[32]{};
    return (xEventGroupGetBits(system_events()) & SETUP_MODE_BIT) &&
        httpd_req_get_hdr_value_str(r,"X-SalaryThief-Request",header,sizeof(header))==ESP_OK &&
        std::strcmp(header,"setup")==0;
}
esp_err_t post_config(httpd_req_t *r) {
    if (!write_allowed(r)) return error(r,"Setup request header required","403 Forbidden");
    if (r->content_len==0 || r->content_len>MAX_BODY) return error(r,"Request body must be 1..2048 bytes","413 Payload Too Large");
    char content_type[48]{};
    if (httpd_req_get_hdr_value_str(r,"Content-Type",content_type,sizeof(content_type))!=ESP_OK ||
        std::strncmp(content_type,"application/json",16)!=0) return error(r,"Expected application/json");
    char body[MAX_BODY+1]{}; size_t used=0;
    const int64_t receive_started=esp_timer_get_time();
    while (used<r->content_len) {
        if (esp_timer_get_time()-receive_started>8000000) return error(r,"Request deadline exceeded","408 Request Timeout");
        const int read=httpd_req_recv(r,body+used,r->content_len-used);
        if (read<=0) return error(r,"Incomplete request or receive timeout","408 Request Timeout");
        used+=read;
    }
    AppConfig c{}; const char *reason=nullptr;
    if (!config_parse_json(body,used,app_config_snapshot(),c,&reason)) return error(r,reason);
    if (app_config_save(c)!=ESP_OK) return error(r,"Failed to save NVS","500 Internal Server Error");
    cJSON *reply=cJSON_CreateObject(); cJSON_AddBoolToObject(reply,"saved",true); cJSON_AddBoolToObject(reply,"reboot_required",true);
    return send_json(r,reply);
}
esp_err_t command(httpd_req_t *r) {
    if (!write_allowed(r)) return error(r,"Setup request header required","403 Forbidden");
    const auto c=std::strcmp(r->uri,"/api/reset")==0 ? SystemCommand::FactoryReset : SystemCommand::Reboot;
    if (system_request(c)!=ESP_OK) return error(r,"System busy","503 Service Unavailable");
    cJSON *j=cJSON_CreateObject(); cJSON_AddBoolToObject(j,"accepted",true); return send_json(r,j,"202 Accepted");
}
}
esp_err_t setup_portal_start() {
    if (server) return ESP_OK;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.max_uri_handlers=7; cfg.stack_size=8192;
    cfg.max_open_sockets=4; cfg.lru_purge_enable=true; cfg.recv_wait_timeout=5; cfg.send_wait_timeout=5;
    esp_err_t err=httpd_start(&server,&cfg); if (err!=ESP_OK) return err;
    const httpd_uri_t routes[]={{"/",HTTP_GET,root,nullptr},{"/api/scan",HTTP_GET,scan,nullptr},
        {"/api/config",HTTP_GET,get_config,nullptr},{"/api/status",HTTP_GET,status,nullptr},
        {"/api/config",HTTP_POST,post_config,nullptr},{"/api/reboot",HTTP_POST,command,nullptr},
        {"/api/reset",HTTP_POST,command,nullptr}};
    for (const auto &route : routes) { err=httpd_register_uri_handler(server,&route); if (err!=ESP_OK) return err; }
    ESP_LOGI("portal","Setup portal ready"); return ESP_OK;
}

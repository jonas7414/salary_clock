#include "wifi_manager.h"
#include "app_config.h"
#include "app_system.h"
#include "setup_portal.h"
#include "wifi_policy.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
namespace {
constexpr const char *TAG="wifi";
esp_netif_t *ap_netif, *sta_netif;
bool have_config;
constexpr EventBits_t STATION_LINK_BITS=WIFI_CONNECTED_BIT|WIFI_ASSOCIATED_BIT|WIFI_CONNECTING_BIT;
void wifi_event(void *,esp_event_base_t base,int32_t id,void *data) {
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_START)
        xEventGroupSetBits(system_events(),WIFI_STARTED_BIT);
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP)
        xEventGroupClearBits(system_events(),STATION_LINK_BITS|WIFI_STARTED_BIT);
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED) {
        // The default netif handler starts DHCP; association is not IP readiness.
        xEventGroupSetBits(system_events(),WIFI_ASSOCIATED_BIT);
        xEventGroupClearBits(system_events(),WIFI_CONNECTING_BIT);
        ESP_LOGI(TAG,"Station associated; waiting for DHCP (up to 60s)");
    }
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(system_events(),STATION_LINK_BITS);
        if (!(xEventGroupGetBits(system_events())&SETUP_MODE_BIT)) {
            const auto *event=static_cast<const wifi_event_sta_disconnected_t *>(data);
            ESP_LOGW(TAG,"Station disconnected (reason=%u); retry after 5s",unsigned(event->reason));
        }
    }
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        if (xEventGroupGetBits(system_events())&SETUP_MODE_BIT) return;
        xEventGroupSetBits(system_events(),WIFI_ASSOCIATED_BIT|WIFI_CONNECTED_BIT);
        xEventGroupClearBits(system_events(),WIFI_CONNECTING_BIT);
        const auto *event=static_cast<const ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG,"Station obtained IP " IPSTR ", gateway " IPSTR,
                 IP2STR(&event->ip_info.ip),IP2STR(&event->ip_info.gw));
    }
    if (base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(system_events(),WIFI_CONNECTED_BIT);
        if (xEventGroupGetBits(system_events())&WIFI_ASSOCIATED_BIT)
            ESP_LOGW(TAG,"Station lost IP; waiting for DHCP");
    }
}
esp_err_t enter_setup() {
    if (xEventGroupGetBits(system_events()) & SETUP_MODE_BIT) return ESP_OK;
    // No reconnect attempts in setup: STA remains free to scan while AP serves the form.
    xEventGroupSetBits(system_events(),SETUP_MODE_BIT);
    esp_wifi_disconnect();
    xEventGroupClearBits(system_events(),STATION_LINK_BITS);
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP));
    wifi_config_t ap{};
    std::snprintf(reinterpret_cast<char *>(ap.ap.ssid),sizeof(ap.ap.ssid),"SalaryThief-%02X%02X",mac[4],mac[5]);
    ap.ap.ssid_len=std::strlen(reinterpret_cast<char *>(ap.ap.ssid));
    ap.ap.channel=1; ap.ap.max_connection=4; ap.ap.authmode=WIFI_AUTH_OPEN;
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,&ap);
    if (err!=ESP_OK) return err;
    auto network=device_snapshot().network;
    std::snprintf(network.ap_ssid,sizeof(network.ap_ssid),"%s",ap.ap.ssid);
    network_publish(network);
    ESP_LOGI(TAG,"Setup AP %s at 192.168.4.1",network.ap_ssid);
    return setup_portal_start();
}
void task(void *) {
    if (!have_config) ESP_ERROR_CHECK(enter_setup());
    WifiPolicy policy(have_config,esp_timer_get_time());
    TickType_t wake=xTaskGetTickCount();
    int64_t next_rssi_at=0;
    while (true) {
        SystemCommand command;
        if (xQueueReceive(system_commands(),&command,0)==pdTRUE) {
            if (command==SystemCommand::Setup) ESP_ERROR_CHECK(enter_setup());
            else {
                // Allow the HTTP response to leave the socket before rebooting.
                vTaskDelay(pdMS_TO_TICKS(600));
                if (command==SystemCommand::FactoryReset) ESP_ERROR_CHECK(app_config_reset());
                esp_restart();
            }
        }
        auto bits=xEventGroupGetBits(system_events());
        const int64_t now=esp_timer_get_time();
        const auto link=(bits&WIFI_CONNECTED_BIT) ? WifiLinkState::Online :
                        (bits&WIFI_ASSOCIATED_BIT) ? WifiLinkState::WaitingForIp :
                        (bits&WIFI_CONNECTING_BIT) ? WifiLinkState::Connecting : WifiLinkState::Disconnected;
        const auto action=(bits&WIFI_STARTED_BIT) ? policy.update(link,bits&SETUP_MODE_BIT,now) : WifiAction::None;
        if (action==WifiAction::Setup) {
            ESP_LOGW(TAG,"%s; opening setup",link==WifiLinkState::WaitingForIp ?
                     "DHCP timeout: associated but no IP after 60s" : "AP connection timeout after 20s");
            ESP_ERROR_CHECK(enter_setup());
        } else if (action==WifiAction::Connect) {
            xEventGroupSetBits(system_events(),WIFI_CONNECTING_BIT);
            const esp_err_t err=esp_wifi_connect();
            if (err!=ESP_OK) {
                xEventGroupClearBits(system_events(),WIFI_CONNECTING_BIT);
                ESP_LOGW(TAG,"Connect: %s",esp_err_to_name(err));
            }
        } else if (action==WifiAction::Disconnect) {
            ESP_LOGW(TAG,"%s; reconnecting",link==WifiLinkState::WaitingForIp ?
                     "DHCP timeout after 60s" : "Connection attempt timeout after 20s");
            esp_wifi_disconnect();
            xEventGroupClearBits(system_events(),STATION_LINK_BITS);
        }
        bits=xEventGroupGetBits(system_events());
        NetworkStatus network=device_snapshot().network;
        const auto config=app_config_snapshot();
        std::snprintf(network.ssid,sizeof(network.ssid),"%s",config.wifi_ssid);
        if ((bits&WIFI_ASSOCIATED_BIT) && !(bits&SETUP_MODE_BIT)) {
            if (now>=next_rssi_at) {
                wifi_ap_record_t connected{};
                if (esp_wifi_sta_get_ap_info(&connected)==ESP_OK) network.rssi=connected.rssi;
                else network.rssi=0;
                next_rssi_at=now+1000000;
            }
        } else { network.rssi=0; next_rssi_at=0; }
        esp_netif_ip_info_t ip{};
        if (bits & SETUP_MODE_BIT) {
            esp_netif_get_ip_info(ap_netif,&ip);
        } else if (bits & WIFI_CONNECTED_BIT) {
            esp_netif_get_ip_info(sta_netif,&ip);
        }
        std::snprintf(network.ip,sizeof(network.ip),IPSTR,IP2STR(&ip.ip));
        network_publish(network);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(250));
    }
}
}
esp_err_t wifi_manager_start(bool configured) {
    have_config=configured;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif=esp_netif_create_default_wifi_sta(); if (!sta_netif) return ESP_ERR_NO_MEM;
    ap_netif=esp_netif_create_default_wifi_ap(); if (!ap_netif) return ESP_ERR_NO_MEM;
    // Explicit AP address; ESP-IDF starts its DHCP server with this netif.
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_ip_info_t ip{};
    ip.ip.addr=ESP_IP4TOADDR(192,168,4,1);
    ip.gw.addr=ESP_IP4TOADDR(192,168,4,1);
    ip.netmask.addr=ESP_IP4TOADDR(255,255,255,0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif,&ip)); ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    const auto config=app_config_snapshot(); wifi_config_t sta{};
    std::memcpy(sta.sta.ssid,config.wifi_ssid,std::strlen(config.wifi_ssid));
    std::memcpy(sta.sta.password,config.wifi_password,std::strlen(config.wifi_password));
    sta.sta.threshold.authmode=WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable=true; sta.sta.pmf_cfg.required=false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return xTaskCreate(task,"wifi",TASK_STACK_WIFI,nullptr,TASK_PRIORITY_WIFI,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

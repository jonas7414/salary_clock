#pragma once
#include "app_types.h"
#include "battery_status.h"
#include "task_health.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0, TIME_SYNCED_BIT = BIT1,
    CONFIG_READY_BIT = BIT2, SETUP_MODE_BIT = BIT3, SYSTEM_ERROR_BIT = BIT4,
    WIFI_ASSOCIATED_BIT = BIT5, WIFI_CONNECTING_BIT = BIT6, WIFI_STARTED_BIT = BIT7,
    OTA_ACTIVE_BIT = BIT8, SNTP_SYNCED_BIT = BIT9, RTC_READY_BIT = BIT10,
    SLEEP_REQUESTED_BIT = BIT11;
constexpr unsigned TASK_PRIORITY_WIFI = 4, TASK_PRIORITY_TIME = 2, TASK_PRIORITY_SALARY = 2,
    TASK_PRIORITY_DISPLAY = 5, TASK_PRIORITY_BUTTON = 3, TASK_PRIORITY_OTA = 1;
constexpr unsigned TASK_STACK_WIFI = 5120, TASK_STACK_TIME = 3072, TASK_STACK_SALARY = 4096,
    TASK_STACK_DISPLAY = 8192, TASK_STACK_BUTTON = 3072, TASK_STACK_OTA = 12288;
enum class SystemCommand { Setup, Reboot, FactoryReset };
struct NetworkStatus {
    char ssid[33]{};
    char ip[16]{};
    char ap_ssid[33]{};
    int rssi{};
};
struct DeviceStatus {
    NetworkStatus network{};
    RtcStatus rtc{};
    BatteryStatus battery{};
    uint32_t button_held_ms{};
    bool power_button_held{};
    uint32_t button_presses{};
    uint32_t frame_us{};
    uint32_t dropped_frames{};
    bool partial_rendering{};
    bool sntp_wait_expired{};
};
esp_err_t app_system_init();
EventGroupHandle_t system_events();
QueueHandle_t system_commands();
QueueHandle_t page_events();
esp_err_t system_request(SystemCommand command);
SystemState system_state();
const char *system_state_name(SystemState state);
void salary_publish(const SalaryStatus &status);
SalaryStatus salary_snapshot();
DeviceStatus device_snapshot();
void network_publish(const NetworkStatus &status);
void button_publish(uint32_t held_ms,uint32_t presses,bool power_held=false);
void display_publish(uint32_t frame_us, uint32_t dropped, bool partial);
void time_wait_publish(bool expired);
void rtc_publish(const RtcStatus &status);
void battery_publish(const BatteryStatus &status);
void system_heartbeat(CriticalTask task);
bool system_tasks_healthy(int64_t now_us,int64_t max_age_us);

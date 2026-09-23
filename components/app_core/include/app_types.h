#pragma once
#include <cstdint>
#include <ctime>

constexpr uint32_t CONFIG_VERSION = 1;
enum SystemState { SYSTEM_BOOTING, SYSTEM_SETUP_MODE, SYSTEM_CONNECTING_WIFI,
                   SYSTEM_SYNCING_TIME, SYSTEM_RUNNING, SYSTEM_ERROR };
enum WorkState { WORK_STATE_NO_TIME, WORK_STATE_DAY_OFF, WORK_STATE_BEFORE_WORK,
                 WORK_STATE_WORKING_MORNING, WORK_STATE_LUNCH,
                 WORK_STATE_WORKING_AFTERNOON, WORK_STATE_AFTER_WORK };
// Monday is bit 0; Sunday is bit 6. Times are minutes since midnight.
struct AppConfig {
    uint32_t version;
    char wifi_ssid[33];
    char wifi_password[65];
    uint32_t monthly_salary;
    uint8_t work_days;
    uint16_t work_start, lunch_start, lunch_end, work_end;
    char timezone[40];
};
struct SalaryStatus {
    time_t timestamp{};
    double earned_money{}, remaining_money{}, daily_salary{}, salary_per_second{};
    uint32_t worked_seconds{}, remaining_work_seconds{}, daily_work_seconds{};
    uint32_t seconds_before_work{};
    double progress{};
    int monthly_work_days{}, work_day_index{}, date_key{};
    WorkState work_state{WORK_STATE_NO_TIME};
};
AppConfig config_defaults();
bool config_validate(const AppConfig &config, bool require_ssid, const char **reason = nullptr);
const char *timezone_posix(const char *name);
uint32_t hm_to_seconds(uint8_t hour, uint8_t minute);
int calculate_work_days_in_month(int year, int month, uint8_t mask);
SalaryStatus calculate_salary(const AppConfig &config, const tm &local, time_t now, bool synced);
uint32_t config_checksum(const AppConfig &config);

// RAM-only schema upgrade hook. Never rewrites persistent data during boot probation.
bool config_migrate(AppConfig &config);

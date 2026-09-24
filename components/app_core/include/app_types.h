#pragma once
#include <cstdint>
#include <ctime>

constexpr uint32_t CONFIG_VERSION = 1;
// Stored separately from AppConfig so older firmware can still read its NVS blob.
enum class DisplayTheme : uint8_t { Classic=0, Amber=1, Handheld=2 };
constexpr bool display_theme_valid(uint32_t value) { return value<=2; }
enum SystemState { SYSTEM_BOOTING, SYSTEM_SETUP_MODE, SYSTEM_CONNECTING_WIFI,
                   SYSTEM_SYNCING_TIME, SYSTEM_RUNNING, SYSTEM_ERROR };
enum WorkState { WORK_STATE_NO_TIME, WORK_STATE_DAY_OFF, WORK_STATE_BEFORE_WORK,
                 WORK_STATE_WORKING_MORNING, WORK_STATE_LUNCH,
                 WORK_STATE_WORKING_AFTERNOON, WORK_STATE_AFTER_WORK, WORK_STATE_NO_CALENDAR };
enum class RtcOperation : uint8_t { None, Read, Write };
enum class RtcResult : uint8_t { Pending, Success, Failed };
struct RtcStatus {
    bool present{}, valid{};
    RtcOperation operation{RtcOperation::None};
    RtcResult result{RtcResult::Pending};
    int64_t activity_started_us{};
};
// Times are minutes since midnight. work_days is retained for NVS compatibility;
// the Taiwan government calendar now determines the actual work/rest dates.
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
    uint32_t monthly_work_seconds{};
    uint32_t seconds_before_work{};
    double progress{};
    int monthly_work_days{}, work_day_index{}, date_key{};
    WorkState work_state{WORK_STATE_NO_TIME};
};
AppConfig config_defaults();
bool config_validate(const AppConfig &config, bool require_ssid, const char **reason = nullptr);
const char *timezone_posix(const char *name);
uint32_t hm_to_seconds(uint8_t hour, uint8_t minute);
// Returns -1 if the requested government calendar month is not bundled.
int calculate_work_days_in_month(int year, int month);
SalaryStatus calculate_salary(const AppConfig &config, const tm &local, time_t now, bool synced);
uint32_t config_checksum(const AppConfig &config);

// RAM-only schema upgrade hook. Never rewrites persistent data during boot probation.
bool config_migrate(AppConfig &config);

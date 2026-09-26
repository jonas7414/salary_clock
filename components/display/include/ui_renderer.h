#pragma once
#include "app_types.h"
#include "battery_status.h"
#include "coin_physics.h"
#include "holiday_countdown.h"
#include "display_preferences.h"
#include "ota_status.h"
#include <cstddef>
#include <cstdint>
constexpr int SCREEN_WIDTH=320, SCREEN_HEIGHT=170;
constexpr unsigned UI_PAGE_COUNT=DISPLAY_PAGE_COUNT;
constexpr unsigned ui_next_page(unsigned page,bool previous) {
    return (page%UI_PAGE_COUNT+(previous ? UI_PAGE_COUNT-1 : 1))%UI_PAGE_COUNT;
}
struct UiModel {
    AppConfig config{};
    DisplayTheme theme{DisplayTheme::Classic};
    SalaryStatus salary{};
    RtcStatus rtc{};
    BatteryStatus battery{};
    HolidayCountdown holiday{};
    DisplayPreferences preferences{};
    OtaStatus ota{};
    int anniversary_days{};
    unsigned hundredths{},weekday{};
    SystemState system{SYSTEM_BOOTING};
    unsigned page{};
    unsigned page_position{};
    char date[16]{"----/--/--"};
    char clock[16]{"--:--:--"};
    char ssid[33]{}, ip[16]{}, ap_ssid[33]{};
    char idf[32]{}, firmware[16]{};
    int rssi{};
    uint32_t held_ms{}, free_heap{}, free_psram{}, uptime{}, dropped_frames{}, frame_us{};
    bool power_button_held{};
    uint32_t animation_ms{};
    bool synced{}, sntp_synced{}, connected{}, associated{}, sntp_wait_expired{}, partial{};
    float rtc_progress{1.f};
    float boot_progress{1.f};
    float pulse{};
    double gain_money{};
    float gain_progress{1.f};
    WorkState transition_state{WORK_STATE_NO_TIME};
    float transition_progress{1.f};
    const CoinPhysicsEngine *physics{};
};
// Renders either a complete frame or a horizontal strip in screen coordinates.
void ui_render(uint16_t *pixels,int y_offset,int rows,const UiModel &model);

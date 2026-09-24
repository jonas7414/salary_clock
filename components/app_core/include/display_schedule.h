#pragma once
#include "app_types.h"

constexpr uint32_t DISPLAY_WAKE_MS=5*60*1000;
constexpr bool display_scheduled_on(const DisplaySchedule &s,uint16_t minute) {
    // Equal times keep the display on all day; the interval may cross midnight.
    if (s.on_minute==s.off_minute) return true;
    return s.on_minute<s.off_minute ? minute>=s.on_minute && minute<s.off_minute
                                  : minute>=s.on_minute || minute<s.off_minute;
}

class DisplaySchedulePolicy {
public:
    bool update(const DisplaySchedule &schedule,uint16_t minute,bool time_valid,
                bool force_on,bool button_pressed,uint32_t now_ms) {
        if (!time_valid || force_on || display_scheduled_on(schedule,minute)) {
            temporary_=false;
            return true;
        }
        if (button_pressed) { temporary_=true; wake_at_=now_ms; }
        // Monotonic elapsed time keeps a clock correction from changing five minutes.
        if (temporary_ && uint32_t(now_ms-wake_at_)>=DISPLAY_WAKE_MS) temporary_=false;
        return temporary_;
    }
private:
    bool temporary_{};
    uint32_t wake_at_{};
};

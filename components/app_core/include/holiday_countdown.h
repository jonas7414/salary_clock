#pragma once
#include <cstdint>
#include <ctime>
struct HolidayCountdown {
    bool available{},on_holiday{},progress_known{};
    uint32_t remaining_seconds{};
    int target_date{};
    unsigned holiday_days{}; // Zero when adjacent dates are not yet available.
    float progress{};
};
// Holidays begin at 00:00 in the configured local timezone (supported zones have no DST).
HolidayCountdown holiday_countdown(const tm &local);

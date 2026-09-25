#pragma once
#include <cstdint>
#include <cstddef>

struct CalendarYear { int32_t year{}; uint32_t workdays[12]{}; };
int calendar_days_in_month(int year,int month);
bool calendar_year_valid(const CalendarYear &year);
// Replace the complete two-year RAM snapshot; readers see either old or new data.
bool taiwan_calendar_install(const CalendarYear (&years)[2]);

// Downloaded cache takes precedence over the built-in fallback.
bool taiwan_calendar_month(int year,int month,uint32_t &workdays);

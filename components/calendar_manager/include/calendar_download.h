#pragma once
#include "taiwan_calendar.h"
#include <cstddef>
#include <cstdint>

// Read one byte (0..255), -1 for clean EOF, -2 for transport failure.
using CalendarRead=int (*)(void *);
bool calendar_parse_year(CalendarRead read,void *context,int expected_year,CalendarYear &output);

struct CalendarCache {
    uint32_t format{0x43414c01};
    int32_t last_attempt_day{-1}; // UTC day, persisted before starting network I/O.
    CalendarYear years[2]{};
    uint32_t checksum{};
};
static_assert(sizeof(CalendarCache)==116,"Calendar cache storage changed");
void calendar_cache_seal(CalendarCache &cache);
bool calendar_cache_valid(const CalendarCache &cache);
bool calendar_check_due(const CalendarCache &cache,int32_t utc_day);
void calendar_cache_replace(CalendarCache &cache,const CalendarYear &year);

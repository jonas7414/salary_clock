#include "calendar_download.h"
#include <cstring>
namespace {
uint32_t checksum(const CalendarCache &cache) {
    const auto *bytes=reinterpret_cast<const uint8_t*>(&cache);
    uint32_t hash=2166136261U;
    for (size_t i=0;i<offsetof(CalendarCache,checksum);++i) hash=(hash^bytes[i])*16777619U;
    return hash;
}
}
void calendar_cache_seal(CalendarCache &cache) { cache.checksum=checksum(cache); }
bool calendar_cache_valid(const CalendarCache &cache) {
    if (cache.format!=0x43414c01 || cache.last_attempt_day < -1 || cache.checksum!=checksum(cache)) return false;
    for (const auto &entry:cache.years) {
        if (entry.year) { if (!calendar_year_valid(entry)) return false; }
        else for (auto days:entry.workdays) if (days) return false;
    }
    return !cache.years[0].year || cache.years[0].year!=cache.years[1].year;
}
bool calendar_check_due(const CalendarCache &cache,int32_t day) {
    // A corrected network clock must not leave an old future-dated marker stuck forever.
    return day>=0 && day!=cache.last_attempt_day;
}
void calendar_cache_replace(CalendarCache &cache,const CalendarYear &year) {
    if (!calendar_year_valid(year)) return;
    for (auto &entry:cache.years) if (entry.year==year.year) { entry=year; return; }
    for (auto &entry:cache.years) if (!entry.year) { entry=year; return; }
    cache.years[cache.years[0].year<cache.years[1].year ? 0 : 1]=year;
}

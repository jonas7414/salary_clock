#include "taiwan_calendar.h"
#include "taiwan_calendar_data.h"
#include <mutex>
#include <cstring>
namespace {
std::mutex calendar_mutex;
CalendarYear downloaded[2]{};
}
int calendar_days_in_month(int y,int m) {
    static constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m<1 || m>12 ? 0 : days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
bool calendar_year_valid(const CalendarYear &entry) {
    if (entry.year<2026 || entry.year>9999) return false;
    for (int m=1;m<=12;++m)
        if (entry.workdays[m-1] & ~((1U<<calendar_days_in_month(entry.year,m))-1)) return false;
    return true;
}
bool taiwan_calendar_install(const CalendarYear (&years)[2]) {
    for (const auto &year:years) if (year.year && !calendar_year_valid(year)) return false;
    if (years[0].year && years[0].year==years[1].year) return false;
    std::lock_guard<std::mutex> lock(calendar_mutex);
    std::memcpy(downloaded,years,sizeof(downloaded));
    return true;
}

bool taiwan_calendar_month(int year,int month,uint32_t &workdays) {
    workdays=0;
    if (month<1 || month>12) return false;
    {
        std::lock_guard<std::mutex> lock(calendar_mutex);
        for (const auto &entry:downloaded)
            if (entry.year && entry.year==year) { workdays=entry.workdays[month-1]; return true; }
    }
    for (const auto &entry:TAIWAN_CALENDAR) {
        if (entry.year==year) { workdays=entry.workdays[month-1]; return true; }
    }
    return false;
}

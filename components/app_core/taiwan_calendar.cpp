#include "taiwan_calendar.h"
#include "taiwan_calendar_data.h"

bool taiwan_calendar_month(int year,int month,uint32_t &workdays) {
    workdays=0;
    if (month<1 || month>12) return false;
    for (const auto &entry:TAIWAN_CALENDAR) {
        if (entry.year==year) { workdays=entry.workdays[month-1]; return true; }
    }
    return false;
}

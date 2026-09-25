#include "holiday_countdown.h"
#include "taiwan_calendar.h"
namespace {
struct Date { int y,m,d; };
void step(Date &date,int direction) {
    date.d+=direction;
    if (date.d>calendar_days_in_month(date.y,date.m)) {
        date.d=1; if (++date.m>12) { date.m=1; ++date.y; }
    } else if (date.d<1) {
        if (--date.m<1) { date.m=12; --date.y; }
        date.d=calendar_days_in_month(date.y,date.m);
    }
}
bool working(Date date,bool &work) {
    uint32_t days=0;
    if (!taiwan_calendar_month(date.y,date.m,days)) return false;
    work=(days&(1U<<(date.d-1)))!=0; return true;
}
unsigned break_length(Date first,bool include_before) {
    unsigned count=0; bool work=false; Date date=first;
    for (unsigned i=0;i<366;++i) {
        if (!working(date,work)) return 0;
        if (work) break;
        ++count; step(date,1);
        if (i==365) return 0;
    }
    if (include_before) {
        date=first;
        for (unsigned i=0;i<366;++i) {
            step(date,-1);
            if (!working(date,work)) return 0;
            if (work) break;
            ++count;
            if (i==365) return 0;
        }
    }
    return count;
}
}
HolidayCountdown holiday_countdown(const tm &local) {
    HolidayCountdown result{};
    const Date today{local.tm_year+1900,local.tm_mon+1,local.tm_mday};
    if (today.y<2026 || today.y>9999 || today.m<1 || today.m>12 || today.d<1 ||
        today.d>calendar_days_in_month(today.y,today.m) || local.tm_hour<0 || local.tm_hour>23 ||
        local.tm_min<0 || local.tm_min>59 || local.tm_sec<0 || local.tm_sec>60) return result;
    bool work=false;
    if (!working(today,work)) return result;
    if (!work) {
        result.available=result.on_holiday=result.progress_known=true;
        result.target_date=today.y*10000+today.m*100+today.d; result.progress=1;
        result.holiday_days=break_length(today,true); return result;
    }
    const uint32_t seconds=uint32_t(local.tm_hour*3600+local.tm_min*60+(local.tm_sec<60 ? local.tm_sec : 59));
    Date target=today;
    for (unsigned ahead=1;ahead<=366;++ahead) {
        step(target,1);
        if (!working(target,work)) return result;
        if (work) continue;
        result.available=true; result.target_date=target.y*10000+target.m*100+target.d;
        result.holiday_days=break_length(target,false);
        result.remaining_seconds=ahead*86400-seconds;
        Date previous=today;
        for (unsigned behind=1;behind<=366;++behind) {
            step(previous,-1);
            if (!working(previous,work)) break;
            if (work) continue;
            const uint32_t elapsed=(behind-1)*86400+seconds;
            result.progress_known=true;
            result.progress=float(elapsed)/float(elapsed+result.remaining_seconds);
            break;
        }
        return result;
    }
    return result;
}

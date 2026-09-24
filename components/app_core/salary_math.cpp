#include "app_types.h"
#include "taiwan_calendar.h"
#include <algorithm>

static int days_in_month(int y, int m) {
    static constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 0;
    return days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
static int count_workdays(uint32_t days) {
    int count = 0;
    while (days) { count+=days&1U; days>>=1; }
    return count;
}
int calculate_work_days_in_month(int y, int m) {
    uint32_t days=0;
    return taiwan_calendar_month(y,m,days) ? count_workdays(days) : -1;
}
SalaryStatus calculate_salary(const AppConfig &c, const tm &t, time_t now, bool synced) {
    SalaryStatus s{};
    s.timestamp = now;
    if (!synced || !config_validate(c,false)) return s;
    const int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
    if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 || d > days_in_month(y,m) ||
        t.tm_hour < 0 || t.tm_hour > 23 || t.tm_min < 0 || t.tm_min > 59 ||
        t.tm_sec < 0 || t.tm_sec > 60) return s;
    s.date_key = y * 10000 + m * 100 + d;
    uint32_t workdays=0;
    if (!taiwan_calendar_month(y,m,workdays)) {
        s.work_state=WORK_STATE_NO_CALENDAR;
        return s;
    }
    s.monthly_work_days=count_workdays(workdays);
    for (int i=1;i<=d;++i) s.work_day_index+=(workdays>>(i-1))&1U;
    const uint32_t start = c.work_start * 60U, lunch = c.lunch_start * 60U;
    const uint32_t resume = c.lunch_end * 60U, end = c.work_end * 60U;
    s.daily_work_seconds = (lunch - start) + (end - resume);
    s.monthly_work_seconds=s.daily_work_seconds*uint32_t(s.monthly_work_days);
    if (!s.monthly_work_days) { s.work_state=WORK_STATE_DAY_OFF; return s; }
    s.daily_salary = double(c.monthly_salary) / double(s.monthly_work_days);
    s.salary_per_second = double(c.monthly_salary) / double(s.monthly_work_seconds);
    if (!(workdays & (1U<<(d-1)))) { s.work_state = WORK_STATE_DAY_OFF; return s; }
    const uint32_t sec = hm_to_seconds(t.tm_hour,t.tm_min) + t.tm_sec;
    if (sec < start) { s.work_state = WORK_STATE_BEFORE_WORK; s.seconds_before_work = start - sec; }
    else if (sec < lunch) { s.work_state = WORK_STATE_WORKING_MORNING; s.worked_seconds = sec - start; }
    else if (sec < resume) { s.work_state = WORK_STATE_LUNCH; s.worked_seconds = lunch - start; }
    else if (sec < end) { s.work_state = WORK_STATE_WORKING_AFTERNOON; s.worked_seconds = lunch - start + sec - resume; }
    else { s.work_state = WORK_STATE_AFTER_WORK; s.worked_seconds = s.daily_work_seconds; }
    s.earned_money = double(s.worked_seconds) * s.salary_per_second;
    s.remaining_money = std::max(0.0, s.daily_salary - s.earned_money);
    s.remaining_work_seconds = s.daily_work_seconds - s.worked_seconds;
    s.progress = double(s.worked_seconds) / double(s.daily_work_seconds);
    return s;
}

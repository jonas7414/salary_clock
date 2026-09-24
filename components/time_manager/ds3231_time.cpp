#include "ds3231_time.h"
namespace {
constexpr int64_t EPOCH_2000=946684800LL, EPOCH_2100=4102444800LL;
constexpr int MONTH_DAYS[]={31,28,31,30,31,30,31,31,30,31,30,31};
int month_days(int year,int month) { return MONTH_DAYS[month-1]+(month==2 && year%4==0); }
int bcd(uint8_t value) { return (value&15)>9 || (value>>4)>9 ? -1 : (value>>4)*10+(value&15); }
uint8_t packed(int value) { return uint8_t((value/10)*16+value%10); }
}
bool ds3231_decode_time(const uint8_t r[7],uint8_t status,time_t &utc) {
    // Reject stopped/uninitialized clocks, reserved bits and unsupported centuries.
    if ((status&0x80) || (r[0]&0x80) || (r[1]&0x80) || (r[2]&0x80) ||
        r[3]<1 || r[3]>7 || (r[4]&0xc0) || (r[5]&0xe0)) return false;
    const int second=bcd(r[0]),minute=bcd(r[1]),day=bcd(r[4]),month=bcd(r[5]),year=bcd(r[6]);
    int hour;
    if (r[2]&0x40) {
        hour=bcd(r[2]&0x1f);
        if (hour<1 || hour>12) return false;
        hour=hour%12+((r[2]&0x20)?12:0);
    } else hour=bcd(r[2]);
    if (second<0 || second>59 || minute<0 || minute>59 || hour<0 || hour>23 ||
        year<0 || month<1 || month>12 || day<1 || day>month_days(2000+year,month)) return false;
    int days=0;
    for (int y=2000;y<2000+year;++y) days+=y%4==0?366:365;
    for (int m=1;m<month;++m) days+=month_days(2000+year,m);
    const int64_t value=EPOCH_2000+int64_t(days+day-1)*86400+hour*3600+minute*60+second;
    const auto converted=static_cast<time_t>(value);
    if (int64_t(converted)!=value) return false;
    utc=converted;
    return true;
}
bool ds3231_encode_time(time_t utc,uint8_t r[7]) {
    const int64_t value=static_cast<int64_t>(utc);
    if (value<EPOCH_2000 || value>=EPOCH_2100) return false;
    const int64_t elapsed=value-EPOCH_2000;
    int days=int(elapsed/86400),year=2000,month=1;
    const int weekday=(days+6)%7+1; // Sunday=1; 2000-01-01 was Saturday.
    while (days>=(year%4==0?366:365)) { days-=year%4==0?366:365; ++year; }
    while (days>=month_days(year,month)) { days-=month_days(year,month); ++month; }
    r[0]=packed(int(elapsed%60));r[1]=packed(int(elapsed/60%60));r[2]=packed(int(elapsed/3600%24));
    r[3]=uint8_t(weekday);r[4]=packed(days+1);r[5]=packed(month);r[6]=packed(year-2000);
    return true;
}

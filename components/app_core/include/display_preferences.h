#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>

constexpr unsigned DISPLAY_PAGE_COUNT=6;
struct DisplayPreferences {
    uint8_t version{1};
    char order[7]{"012453"}; // stable page IDs; system information is last by default
    char anniversary_name[73]{};
    char anniversary_date[11]{};
    uint8_t anniversary_annual{1};
};
inline bool leap_year(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
inline int month_days(int y,int m) {
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m>=1 && m<=12 ? days[m-1]+(m==2 && leap_year(y)) : 0;
}
inline bool anniversary_date_parts(const char *s,int &y,int &m,int &d) {
    if (std::strlen(s)!=10 || s[4]!='-' || s[7]!='-') return false;
    y=m=d=0;
    for (int i=0;i<10;++i) {
        if (i==4 || i==7) continue;
        if (s[i]<'0' || s[i]>'9') return false;
        int &v=i<4?y:i<7?m:d; v=v*10+s[i]-'0';
    }
    return y>=1900 && y<=2199 && d>=1 && d<=month_days(y,m);
}
// Match the small display's embedded custom-name font. Reject malformed UTF-8,
// controls and unsupported symbols instead of silently displaying question marks.
inline bool anniversary_name_valid(const char *s) {
    unsigned count=0; bool visible=false;
    while (*s) {
        const auto a=uint8_t(*s++); uint32_t cp=a; int n=0;
        if (a>=0xc2 && a<=0xdf) { cp=a&31; n=1; }
        else if (a>=0xe0 && a<=0xef) { cp=a&15; n=2; }
        else if (a>=128) return false;
        for (int i=0;i<n;++i) {
            const auto b=uint8_t(*s); if ((b&0xc0)!=0x80) return false;
            ++s; cp=(cp<<6)|(b&63);
        }
        if ((n==1 && cp<128) || (n==2 && cp<2048)) return false;
        if (!((cp>=32 && cp<=126) || (cp>=0x3000 && cp<=0x30ff) ||
              (cp>=0x4e00 && cp<=0x9fff) || (cp>=0xff01 && cp<=0xff60))) return false;
        visible|=cp!=32 && cp!=0x3000;
        if (++count>24) return false;
    }
    return count==0 || visible;
}
inline bool display_preferences_valid(const DisplayPreferences &p) {
    if (p.version!=1 || p.order[6] || p.anniversary_annual>1 ||
        !std::memchr(p.anniversary_name,0,sizeof(p.anniversary_name)) ||
        !std::memchr(p.anniversary_date,0,sizeof(p.anniversary_date))) return false;
    unsigned mask=0;
    for (unsigned i=0;i<DISPLAY_PAGE_COUNT;++i) {
        if (p.order[i]<'0' || p.order[i]>'5') return false;
        mask|=1U<<(p.order[i]-'0');
    }
    if (mask!=63 || !anniversary_name_valid(p.anniversary_name)) return false;
    if (!p.anniversary_name[0]) return !p.anniversary_date[0];
    int y,m,d; return anniversary_date_parts(p.anniversary_date,y,m,d);
}
inline int civil_day(int y,int m,int d) {
    y-=m<=2; const int era=y/400,yoe=y-era*400;
    return era*146097+yoe*365+yoe/4-yoe/100+(153*(m+(m>2?-3:9))+2)/5+d-1;
}
// Positive: days to go. Negative: days elapsed. Feb 29 repeats on Feb 28.
inline int anniversary_days(const DisplayPreferences &p,const tm &today) {
    int y,m,d; if (!anniversary_date_parts(p.anniversary_date,y,m,d)) return 0;
    const int now=civil_day(today.tm_year+1900,today.tm_mon+1,today.tm_mday);
    if (p.anniversary_annual) {
        if (y<today.tm_year+1900) y=today.tm_year+1900;
        int day=d>month_days(y,m)?month_days(y,m):d;
        if (civil_day(y,m,day)<now) ++y;
        d=d>month_days(y,m)?month_days(y,m):d;
    }
    return civil_day(y,m,d)-now;
}

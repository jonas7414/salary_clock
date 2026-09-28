#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>

constexpr unsigned DISPLAY_PAGE_COUNT=7;
struct LegacyDisplayPreferences {
    uint8_t version{1};
    char order[7]{"012453"};
    char anniversary_name[73]{};
    char anniversary_date[11]{};
    uint8_t anniversary_annual{1};
};
struct DisplayPreferencesV2 {
    uint8_t version{1};
    char order[8]{"0126453"}; // stable page IDs; system information is last by default
    char anniversary_name[73]{};
    char anniversary_date[11]{};
    uint8_t anniversary_annual{1};
    char job_start_date[11]{"2026-08-01"};
};
constexpr unsigned MAX_ANNIVERSARIES=5;
struct Anniversary {
    char name[73]{};
    char date[11]{};
    uint8_t annual{1};
};
struct DisplayPreferences : DisplayPreferencesV2 {
    Anniversary extra_anniversaries[MAX_ANNIVERSARIES-1]{};
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
    for (const auto &a:p.extra_anniversaries) {
        if (!std::memchr(a.name,0,sizeof(a.name)) || !std::memchr(a.date,0,sizeof(a.date)) ||
            a.annual>1 || !anniversary_name_valid(a.name)) return false;
        int y,m,d;
        if (a.name[0] ? !anniversary_date_parts(a.date,y,m,d) : a.date[0]!=0) return false;
    }
    if (p.version!=1 || p.order[7] || p.anniversary_annual>1 ||
        !std::memchr(p.job_start_date,0,sizeof(p.job_start_date)) ||
        !std::memchr(p.anniversary_name,0,sizeof(p.anniversary_name)) ||
        !std::memchr(p.anniversary_date,0,sizeof(p.anniversary_date))) return false;
    unsigned mask=0;
    for (unsigned i=0;i<DISPLAY_PAGE_COUNT;++i) {
        if (p.order[i]<'0' || p.order[i]>'6') return false;
        mask|=1U<<(p.order[i]-'0');
    }
    int sy,sm,sd;
    if (mask!=127 || !anniversary_name_valid(p.anniversary_name) ||
        !anniversary_date_parts(p.job_start_date,sy,sm,sd)) return false;
    if (!p.anniversary_name[0]) return !p.anniversary_date[0];
    int y,m,d; return anniversary_date_parts(p.anniversary_date,y,m,d);
}
// Keep slot one compatible with older clients; skip empty slots during rotation.
inline DisplayPreferences anniversary_for_tick(const DisplayPreferences &p,uint32_t seconds) {
    unsigned slots[MAX_ANNIVERSARIES]{},count=0;
    if (p.anniversary_name[0]) slots[count++]=0;
    for (unsigned i=0;i<MAX_ANNIVERSARIES-1;++i) if(p.extra_anniversaries[i].name[0]) slots[count++]=i+1;
    auto selected=p;
    const unsigned slot=count?slots[(seconds/5)%count]:0;
    if (slot) {
        const auto &a=p.extra_anniversaries[slot-1];
        std::memcpy(selected.anniversary_name,a.name,sizeof(a.name));
        std::memcpy(selected.anniversary_date,a.date,sizeof(a.date));
        selected.anniversary_annual=a.annual;
    }
    return selected;
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

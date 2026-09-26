#include "ui_renderer.h"
#include "taiwan_calendar.h"
#include "button_logic.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } ++checks; } while(0)
tm local(int y,int m,int d,int h=0,int minute=0,int second=0) {
    tm t{};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=minute;t.tm_sec=second;return t;
}
void off(CalendarYear &year,int month,int day) { year.workdays[month-1]&=~(1U<<(day-1)); }
void ppm(const std::string &name,const std::vector<uint16_t> &pixels) {
    FILE *file=std::fopen(name.c_str(),"wb"); CHECK(file!=nullptr);
    std::fprintf(file,"P6\n320 170\n255\n");
    for (auto pixel:pixels) { const unsigned char rgb[]={uint8_t((pixel>>11)*255/31),uint8_t(((pixel>>5)&63)*255/63),uint8_t((pixel&31)*255/31)}; std::fwrite(rgb,1,3,file); }
    std::fclose(file);
}
int main(int argc,char **argv) {
    DisplayPreferences prefs{};CHECK(display_preferences_valid(prefs));CHECK(prefs.order[5]=='3');
    std::strcpy(prefs.anniversary_name,"Our day");std::strcpy(prefs.anniversary_date,"2024-02-29");
    CHECK(display_preferences_valid(prefs));
    CHECK(anniversary_days(prefs,local(2025,2,27))==1);
    CHECK(anniversary_days(prefs,local(2025,2,28))==0);
    CHECK(anniversary_days(prefs,local(2027,3,1))==365);
    CHECK(anniversary_days(prefs,local(2028,2,28))==1);
    CHECK(anniversary_days(prefs,local(2028,2,29))==0);
    prefs.anniversary_annual=0;CHECK(anniversary_days(prefs,local(2024,3,1))==-1);
    CHECK(anniversary_days(prefs,local(2024,2,28))==1);
    std::strcpy(prefs.anniversary_date,"2027-01-01");prefs.anniversary_annual=1;
    CHECK(anniversary_days(prefs,local(2026,12,31))==1);
    CHECK(anniversary_days(prefs,local(2026,1,1))==365);
    for (const char *name:{"\xc0\xaf","\xed\xa0\x80","\xe4\xb8","bad\nname","   ","\xf0\x9f\x98\x80"}) CHECK(!anniversary_name_valid(name));
    CHECK(ota_should_prompt(false,"1.5.0","1.4.9"));CHECK(!ota_should_prompt(false,"1.5.0","1.5.0"));
    CHECK(ota_should_prompt(true,"1.5.0","1.5.0"));CHECK(!ota_version_approved("","1.5.0"));
    CHECK(!ota_version_approved("1.5.0","1.5.1"));CHECK(ota_version_approved("1.5.0","1.5.0"));
    BootUpdateCheck boot_check(20000000);
    CHECK(!boot_check.due(0,false));CHECK(!boot_check.due(90000000,false));
    CHECK(!boot_check.due(100000000,true));CHECK(!boot_check.due(119999999,true));
    CHECK(!boot_check.due(120000000,false));CHECK(boot_check.due(130000000,true));
    boot_check.complete();CHECK(boot_check.checked());CHECK(!boot_check.due(999999999,true));
    CHECK(!boot_check.due(1000000000,false));CHECK(!boot_check.due(1000000001,true));
    BootUpdateCheck manual_first(20000000);manual_first.complete();CHECK(!manual_first.due(1000000000,true));
    OtaStatus consent{};CHECK(ota_request_allowed(consent,false));CHECK(!ota_request_allowed(consent,true));
    consent.state=OtaState::UPDATE_AVAILABLE;CHECK(!ota_request_allowed(consent,true));
    consent.prompt=true;CHECK(consent.choice==1 && ota_request_allowed(consent,true));
    consent.busy=true;CHECK(!ota_request_allowed(consent,true) && !ota_request_allowed(consent,false));
    CalendarYear years[2]{};
    for (int i=0;i<2;++i) { years[i].year=2028+i;
        for (int m=1;m<=12;++m) years[i].workdays[m-1]=(1U<<calendar_days_in_month(years[i].year,m))-1; }
    off(years[0],2,20);off(years[0],2,28);off(years[0],2,29);off(years[0],3,1);
    off(years[0],3,4);off(years[0],3,5);off(years[0],12,31);off(years[1],1,1);
    CHECK(taiwan_calendar_install(years));
    auto h=holiday_countdown(local(2028,2,27,12));
    CHECK(h.available && !h.on_holiday && h.remaining_seconds==43200 && h.holiday_days==3);
    CHECK(h.target_date==20280228 && h.progress_known && h.progress>.92f && h.progress<.94f);
    h=holiday_countdown(local(2028,2,27,23,59,59)); CHECK(h.remaining_seconds==1 && h.holiday_days==3);
    h=holiday_countdown(local(2028,2,29,12)); CHECK(h.on_holiday && h.holiday_days==3 && h.progress==1 && !h.remaining_seconds);
    h=holiday_countdown(local(2028,3,3,12)); CHECK(h.holiday_days==2 && h.target_date==20280304);
    h=holiday_countdown(local(2028,12,30,12)); CHECK(h.holiday_days==2 && h.remaining_seconds==43200);
    h=holiday_countdown(local(2029,1,1)); CHECK(h.on_holiday && h.holiday_days==2);
    CHECK(!holiday_countdown(local(2030,1,1)).available);
    CHECK(!holiday_countdown(local(2028,2,30)).available);
    off(years[1],12,31);CHECK(taiwan_calendar_install(years));
    h=holiday_countdown(local(2029,12,30));CHECK(h.available && !h.holiday_days); // Missing next-year boundary.
    CHECK(ui_next_page(0,true)==5 && ui_next_page(5,false)==0);
    for (unsigned page=0;page<6;++page) CHECK(ui_next_page(ui_next_page(page,false),true)==page);
    ButtonLogic previous(true);
    previous.update(true,0); previous.update(true,30);previous.update(false,100);
    CHECK(previous.update(false,130)==ButtonAction::Page);
    previous.update(true,1000);previous.update(true,1030);
    CHECK(previous.update(true,6030)==ButtonAction::None);
    previous.update(false,7000);CHECK(previous.update(false,7030)==ButtonAction::None);
    const std::string folder=argc>1 ? argv[1] : ".";
    UiModel m{};m.synced=true;m.system=SYSTEM_RUNNING;m.salary.work_state=WORK_STATE_WORKING_MORNING;
    std::strcpy(m.clock,"16:32:08");std::strcpy(m.date,"2028/02/26");m.weekday=6;m.hundredths=36;
    std::strcpy(m.firmware,"1.4.3");m.holiday=holiday_countdown(local(2028,2,26,16,32));
    for (unsigned theme=0;theme<3;++theme) for (unsigned page=4;page<6;++page) {
        m.theme=static_cast<DisplayTheme>(theme);m.page=page;
        for (unsigned state=0;state<4;++state) {
            std::strcpy(m.date,state==1 ? "2028/02/29" : "2028/02/26");m.weekday=state==1 ? 2 : 6;
            m.holiday=state==1 ? holiday_countdown(local(2028,2,29,12)) :
                state==2 ? HolidayCountdown{} : holiday_countdown(local(2028,2,26,16,32));
            m.salary.work_state=state==3 ? WORK_STATE_NO_CALENDAR : WORK_STATE_WORKING_MORNING;
            m.animation_ms=state*1700;
            std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0xabcd);
            ui_render(full.data(),0,170,m);
            for (int y=0;y<170;y+=10) {
                ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0xabcd && guard.back()==0xabcd);
                std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
            }
            CHECK(full==strip);
            ppm(folder+"/extra_"+std::to_string(theme)+"_"+std::to_string(page)+"_"+std::to_string(state)+".ppm",full);
        }
    }
    for (unsigned theme=0;theme<3;++theme) for (unsigned state=0;state<8;++state) {
        m.theme=static_cast<DisplayTheme>(theme);m.page=4;m.page_position=3;m.ota={};
        m.preferences=prefs;std::strcpy(m.preferences.anniversary_name,"我們的紀念日");m.anniversary_days=26;
        if (state>=1 && state<=3) { m.ota.prompt=true;m.ota.choice=state-1;std::strcpy(m.ota.latest_version,"1.4.5"); }
        if (state>=4) {
            m.ota.foreground=true;m.ota.state=state==4?OtaState::CHECKING:state==5?OtaState::DOWNLOADING:state==6?OtaState::IDLE:OtaState::ERROR;
            m.ota.current=state==6;
            if (state==5) {m.ota.percentage=43;m.ota.downloaded_bytes=1300000;m.ota.total_bytes=3000000;}
        }
        std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0xabcd);ui_render(full.data(),0,170,m);
        for (int y=0;y<170;y+=10) {ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0xabcd && guard.back()==0xabcd);std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);}
        CHECK(full==strip);ppm(folder+"/preferences_"+std::to_string(theme)+"_"+std::to_string(state)+".ppm",full);
    }
    CalendarYear clear[2]{};CHECK(taiwan_calendar_install(clear));
    std::printf("PASS: %u clock/holiday/navigation checks\n",checks);
}

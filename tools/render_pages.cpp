// Host preview using the firmware's actual renderer and salary/calendar logic.
#include "ui_renderer.h"
#include "ui_animation.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
static bool render(const std::filesystem::path &path,const UiModel &m) {
    std::vector<uint16_t> full(320*170),strip(320*170);
    ui_render(full.data(),0,170,m);
    for(int y=0;y<170;y+=10)ui_render(strip.data()+y*320,y,10,m);
    if(full!=strip) {std::fprintf(stderr,"Strip mismatch: %s\n",path.string().c_str());return false;}
    FILE *file=std::fopen(path.string().c_str(),"wb");if(!file)return false;
    std::fprintf(file,"P6\n320 170\n255\n");
    for(auto p:full) {unsigned char rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(rgb,1,3,file);}
    return std::fclose(file)==0;
}
int main(int argc,char **argv) {
    const std::filesystem::path out=argc>1?argv[1]:".artifacts/all-pages";
    std::filesystem::create_directories(out);
    tm t{};t.tm_year=126;t.tm_mon=8;t.tm_mday=23;t.tm_hour=15;t.tm_min=32;t.tm_sec=8;
    UiModel m{};m.config=config_defaults();m.config.monthly_salary=40000;
    m.system=SYSTEM_RUNNING;m.synced=m.sntp_synced=m.connected=m.associated=true;
    m.salary=calculate_salary(m.config,t,0,true,m.preferences.job_start_date);
    m.holiday=holiday_countdown(t);m.weekday=3;m.hundredths=36;m.animation_ms=2400;
    std::strcpy(m.date,"2026/09/23");std::strcpy(m.clock,"15:32:08");
    std::strcpy(m.ssid,"Office-WiFi");std::strcpy(m.ip,"192.168.1.108");
    std::strcpy(m.idf,"5.5.0");std::string version;std::ifstream("version.txt")>>version;
    std::snprintf(m.firmware,sizeof(m.firmware),"%s",version.c_str());
    m.rssi=-53;m.free_heap=110000;m.free_psram=7300000;m.uptime=2451;
    m.battery={BatteryState::BatteryPower,3850};m.rtc.present=true;m.rtc.valid=true;
    CoinPhysicsEngine physics(42);
    physics.set_count(work_coin_count(m.salary));m.physics=&physics;
    for(unsigned theme=0;theme<3;++theme) for(unsigned position=0;position<UI_PAGE_COUNT;++position) {
        m.theme=static_cast<DisplayTheme>(theme);m.page_position=position;m.page=m.preferences.order[position]-'0';
        const auto path=out/("theme_"+std::to_string(theme)+"_page_"+std::to_string(position+1)+".ppm");
        if(!render(path,m))return 1;
    }
    m.theme=DisplayTheme::Classic;m.page=0;m.page_position=0;
    const UiModel base=m;
    m.system=SYSTEM_SETUP_MODE;m.synced=false;std::strcpy(m.ap_ssid,"SalaryThief-A31F");
    std::strcpy(m.date,"----/--/--");std::strcpy(m.clock,"--:--:--");
    if(!render(out/"manual_setup.ppm",m))return 1;
    m=base;m.synced=m.sntp_synced=false;m.rtc={};std::strcpy(m.clock,"--:--:--");
    if(!render(out/"manual_waiting.ppm",m))return 1;
    m=base;m.page=7;m.page_position=6;m.brightness=50;m.brightness_editing=true;
    if(!render(out/"manual_brightness_edit.ppm",m))return 1;
    m.brightness_editing=false;
    if(!render(out/"manual_brightness_saved.ppm",m))return 1;
    m=base;m.page=4;m.page_position=4;
    std::strcpy(m.preferences.anniversary_name,"旅行出發");
    std::strcpy(m.preferences.anniversary_date,"2026-10-01");m.preferences.anniversary_annual=0;
    m.anniversary_days=anniversary_days(m.preferences,t);
    if(!render(out/"manual_anniversary.ppm",m))return 1;
    m=base;m.page=3;m.page_position=7;m.ota.prompt=true;m.ota.state=OtaState::UPDATE_AVAILABLE;
    // Illustrative future version, not a statement about published releases.
    std::strcpy(m.ota.latest_version,"1.4.9");
    if(!render(out/"manual_update_prompt.ppm",m))return 1;
    m.ota.prompt=false;m.ota.foreground=true;m.ota.state=OtaState::DOWNLOADING;
    m.ota.percentage=43;m.ota.downloaded_bytes=1290000;m.ota.total_bytes=3000000;
    if(!render(out/"manual_update_progress.ppm",m))return 1;
    m=base;m.page=3;m.page_position=7;m.battery={BatteryState::ExternalPower,4750};
    if(!render(out/"manual_usb_power.ppm",m))return 1;
    // Use real salary snapshots to show an entire day, including the final coin.
    const int hours[]={9,11,14,16,18};
    for(unsigned theme=0;theme<3;++theme)for(unsigned step=0;step<5;++step) {
        m=base;m.theme=static_cast<DisplayTheme>(theme);t.tm_hour=hours[step];t.tm_min=t.tm_sec=0;
        m.salary=calculate_salary(m.config,t,0,true,m.preferences.job_start_date);
        std::snprintf(m.clock,sizeof(m.clock),"%02d:00:00",hours[step]);
        // Use matching random piles across themes so the comparison is meaningful.
        physics=CoinPhysicsEngine(42);
        physics.set_count(work_coin_count(m.salary));
        if(!render(out/("progress_"+std::to_string(theme)+"_"+std::to_string(step)+".ppm"),m))return 1;
    }
    for (unsigned theme=0;theme<3;++theme) for (unsigned state=0;state<6;++state) {
        m=base; m.theme=static_cast<DisplayTheme>(theme);
        const CrashReportState states[]={CrashReportState::Prompt,CrashReportState::Prompt,
            CrashReportState::Sending,CrashReportState::Sent,CrashReportState::Failed,CrashReportState::CleanupFailed};
        m.crash.state=states[state];m.crash.report=state==1;
        m.crash.http_status=state==4?401:state==5?204:0;
        if(!render(out/("crash_"+std::to_string(theme)+"_"+std::to_string(state)+".ppm"),m))return 1;
    }
    std::puts("Rendered 24 pages, 8 manual scenes, 15 work-progress scenes and 18 crash dialogs; full/strip matched.");
}

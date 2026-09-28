// Host preview using the firmware's actual renderer and salary/calendar logic.
#include "ui_renderer.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
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
    for(int i=0;i<500;++i) {if(i%45==0)physics.spawn();physics.update(.02f);}m.physics=&physics;
    for(unsigned theme=0;theme<3;++theme) for(unsigned position=0;position<UI_PAGE_COUNT;++position) {
        m.theme=static_cast<DisplayTheme>(theme);m.page_position=position;m.page=m.preferences.order[position]-'0';
        std::vector<uint16_t> full(320*170);ui_render(full.data(),0,170,m);
        const auto path=out/("theme_"+std::to_string(theme)+"_page_"+std::to_string(position+1)+".ppm");
        FILE *file=std::fopen(path.string().c_str(),"wb");if(!file)return 1;
        std::fprintf(file,"P6\n320 170\n255\n");
        for(auto p:full) {unsigned char rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(rgb,1,3,file);}
        std::fclose(file);
    }
    std::puts("Rendered all 7 pages in all 3 themes.");
}

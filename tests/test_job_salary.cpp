#include "ui_renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
static SalaryStatus at(int month,int day,int h,int min=0,int sec=0,const char *start="2026-08-01") {
    tm t{};t.tm_year=126;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=h;t.tm_min=min;t.tm_sec=sec;
    auto c=config_defaults();c.monthly_salary=40000;
    return calculate_salary(c,t,0,true,start);
}
static bool close(double a,double b) {return std::abs(a-b)<.00001;}
static void ppm(const char *path,const std::vector<uint16_t> &pixels) {
    FILE *f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n320 170\n255\n");
    for(auto p:pixels){unsigned char rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
int main() {
    assert(at(7,31,18).job_total_available && at(7,31,18).job_earned_money==0);
    assert(close(at(8,31,23).job_earned_money,40000));
    assert(close(at(9,1,9).job_earned_money,40000));
    auto a=at(9,1,10),b=at(9,1,10,0,1);
    assert(close(b.job_earned_money-a.job_earned_money,a.salary_per_second));
    assert(close(at(9,1,12).job_earned_money,at(9,1,12,59,59).job_earned_money));
    assert(close(at(9,1,18).job_earned_money,at(9,1,23).job_earned_money));
    assert(close(at(9,4,18).job_earned_money,at(9,6,23).job_earned_money));
    assert(close(at(9,30,23).job_earned_money,80000));
    assert(close(at(9,1,18,0,0,"2026-09-01").job_earned_money,at(9,1,18).daily_salary));
    assert(!at(9,1,18,0,0,"2026-02-30").job_total_available);
    assert(!at(9,1,18,0,0,"1900-01-01").job_total_available);
    UiModel m{};m.synced=true;m.system=SYSTEM_RUNNING;m.config=config_defaults();m.config.monthly_salary=40000;
    m.salary=at(9,28,15,32,8);std::strcpy(m.date,"2026/09/28");std::strcpy(m.clock,"15:32:08");
    for(unsigned theme=0;theme<3;++theme) for(unsigned page:{6U,1U}) {
        m.theme=static_cast<DisplayTheme>(theme);m.page=page;m.page_position=page==6?3:1;
        std::vector<uint16_t> full(320*170),strip(320*170);ui_render(full.data(),0,170,m);
        for(int y=0;y<170;y+=10)ui_render(strip.data()+y*320,y,10,m);
        assert(full==strip);char path[128];std::snprintf(path,sizeof(path),".artifacts/host/job_%u_%u.ppm",theme,page);ppm(path,full);
    }
    for(int sec=0;sec<8;++sec) {
        m.theme=DisplayTheme::Classic;m.page=6;m.page_position=3;m.salary=at(9,23,15,32,sec);
        std::strcpy(m.date,"2026/09/23");std::snprintf(m.clock,sizeof(m.clock),"15:32:%02d",sec);
        std::vector<uint16_t> pixels(320*170);ui_render(pixels.data(),0,170,m);
        char path[128];std::snprintf(path,sizeof(path),".artifacts/host/job_tick_%d.ppm",sec);ppm(path,pixels);
    }
    std::puts("PASS: job salary boundaries, second ticks, monthly totals, missing calendar and LCD strip rendering");
}

#include "app_types.h"
#include "button_logic.h"
#include "wifi_policy.h"
#include "coin_physics.h"
#include "ui_renderer.h"
#include "ui_animation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
static unsigned checks=0;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expression); std::exit(1); } } while(0)
static tm local(int y,int m,int d,int h,int min,int sec) { tm t{};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=min;t.tm_sec=sec;return t; }
static SalaryStatus at(const AppConfig &c,int h,int m,int s=0,int day=23) {return calculate_salary(c,local(2026,9,day,h,m,s),1790126218,true);}
static unsigned count(const CoinPhysicsEngine &p) { unsigned n=0;for(const auto &c:p.coins())n+=c.active;return n; }
static void config_tests() {
    auto c=config_defaults();CHECK(config_validate(c,false));CHECK(!config_validate(c,true));
    std::strcpy(c.wifi_ssid,"Office");CHECK(config_validate(c,true));
    const auto crc=config_checksum(c);c.monthly_salary++;CHECK(crc!=config_checksum(c));
    c=config_defaults();c.version++;CHECK(!config_validate(c,false));
    c=config_defaults();std::memset(c.timezone,'x',sizeof(c.timezone));CHECK(!config_validate(c,false));
    c=config_defaults();c.work_days=0;CHECK(!config_validate(c,false));
    c.work_days=128;CHECK(!config_validate(c,false));
    c=config_defaults();c.work_end=c.lunch_end;CHECK(!config_validate(c,false));
    c=config_defaults();c.work_start=c.lunch_start;CHECK(!config_validate(c,false));
    c=config_defaults();c.lunch_start=c.lunch_end;CHECK(!config_validate(c,false));
    c=config_defaults();c.work_end=1440;CHECK(!config_validate(c,false));
    c=config_defaults();c.monthly_salary=0;CHECK(!config_validate(c,false));
    c=config_defaults();std::strcpy(c.wifi_password,"short");CHECK(!config_validate(c,false));
    std::memset(c.wifi_password,'a',64);c.wifi_password[64]=0;CHECK(config_validate(c,false));
    c.wifi_password[3]='z';CHECK(!config_validate(c,false));
    CHECK(std::strcmp(timezone_posix("Asia/Taipei"),"CST-8")==0);
}
static void salary_tests(const char *oracle) {
    auto c=config_defaults();CHECK(hm_to_seconds(9,30)==34200);
    CHECK(calculate_salary(c,local(2026,9,23,12,0,0),0,false).work_state==WORK_STATE_NO_TIME);
    CHECK(at(c,8,59,59).seconds_before_work==1);CHECK(at(c,9,0).work_state==WORK_STATE_WORKING_MORNING);
    CHECK(at(c,11,59,59).worked_seconds==10799);CHECK(at(c,12,0).worked_seconds==10800);
    CHECK(at(c,12,0).work_state==WORK_STATE_LUNCH);CHECK(at(c,12,59,59).earned_money==at(c,12,0).earned_money);
    CHECK(at(c,13,0).work_state==WORK_STATE_WORKING_AFTERNOON);CHECK(at(c,13,0).worked_seconds==10800);
    CHECK(at(c,17,59,59).remaining_work_seconds==1);CHECK(at(c,18,0).work_state==WORK_STATE_AFTER_WORK);
    CHECK(at(c,18,0).progress==1);CHECK(at(c,23,59,59).worked_seconds==28800);
    CHECK(at(c,10,0).remaining_work_seconds==25200);CHECK(at(c,12,30).remaining_work_seconds==18000);
    CHECK(at(c,14,0).remaining_work_seconds==14400);
    CHECK(at(c,12,0,0,25).work_state==WORK_STATE_LUNCH);CHECK(at(c,12,0,0,26).work_state==WORK_STATE_DAY_OFF);
    CHECK(at(c,12,0,0,27).remaining_money==0);CHECK(at(c,9,0,0,28).work_state==WORK_STATE_WORKING_MORNING);
    for(int sec=0;sec<86400;++sec) {
        const auto s=at(c,sec/3600,sec/60%60,sec%60);
        CHECK(s.worked_seconds+s.remaining_work_seconds==s.daily_work_seconds);
        CHECK(s.earned_money>=0 && s.remaining_money>=0 && s.progress>=0 && s.progress<=1);
        CHECK(std::abs(s.earned_money+s.remaining_money-s.daily_salary)<1e-9);
    }
    const auto reboot=at(c,14,37,17);CHECK(reboot.earned_money==at(c,14,37,17).earned_money);
    c.monthly_salary=80000;CHECK(std::abs(at(c,14,37,17).earned_money-reboot.earned_money*2)<1e-9);
    c.work_start=8*60;c.lunch_start=11*60+30;c.lunch_end=12*60+15;c.work_end=16*60+30;
    CHECK(at(c,16,30).daily_work_seconds==27900);CHECK(at(c,11,45).worked_seconds==12600);
    c.work_days=1<<5;CHECK(at(c,12,0,0,26).work_state==WORK_STATE_LUNCH);CHECK(at(c,12,0,0,23).work_state==WORK_STATE_DAY_OFF);
    std::ifstream file(oracle);CHECK(file.good());int year,month,mask,expected;unsigned cases=0;
    while(file>>year>>month>>mask>>expected) { CHECK(calculate_work_days_in_month(year,month,mask)==expected);++cases; }
    CHECK(cases>20000);
    CHECK(calculate_work_days_in_month(2024,2,127)==29);CHECK(calculate_work_days_in_month(2100,2,127)==28);
    CHECK(calculate_work_days_in_month(2000,2,127)==29);CHECK(calculate_work_days_in_month(2026,4,127)==30);
    CHECK(calculate_work_days_in_month(2026,1,127)==31);CHECK(calculate_work_days_in_month(2026,13,127)==0);
    const auto dec=calculate_salary(config_defaults(),local(2026,12,31,18,0,0),0,true);
    const auto jan=calculate_salary(config_defaults(),local(2027,1,1,0,0,0),0,true);
    CHECK(dec.date_key==20261231 && jan.date_key==20270101 && jan.earned_money==0);
}
static void button_tests() {
    ButtonLogic b;CHECK(b.update(true,100)==ButtonAction::None);b.update(false,110);b.update(true,120);
    CHECK(b.update(true,150)==ButtonAction::None);b.update(false,300);CHECK(b.update(false,330)==ButtonAction::Page);
    b.update(true,1000);b.update(true,1030);CHECK(b.update(true,6030)==ButtonAction::None);
    b.update(false,6200);CHECK(b.update(false,6230)==ButtonAction::Setup);
    b.update(true,7000);b.update(true,7030);CHECK(b.update(true,17030)==ButtonAction::Reset);
    CHECK(b.update(true,18000)==ButtonAction::None);b.update(false,18010);CHECK(b.update(false,18040)==ButtonAction::None);
    ButtonLogic wrap;const uint32_t start=UINT32_MAX-4000;wrap.update(true,start);wrap.update(true,start+30);
    CHECK(wrap.held_ms(start+6030)==6000);wrap.update(false,start+6030);CHECK(wrap.update(false,start+6060)==ButtonAction::Setup);
}
static void wifi_tests() {
    using State=WifiLinkState;
    WifiPolicy first(false,0);
    CHECK(first.update(State::Disconnected,false,0)==WifiAction::Setup);
    CHECK(first.update(State::Disconnected,true,100000)==WifiAction::None);

    // An in-flight scan/authentication must not be restarted every five seconds.
    WifiPolicy absent(true,1000000);
    CHECK(absent.update(State::Disconnected,false,1000000)==WifiAction::Connect);
    CHECK(absent.update(State::Connecting,false,1000000)==WifiAction::None);
    CHECK(absent.update(State::Connecting,false,6000000)==WifiAction::None);
    CHECK(absent.update(State::Connecting,false,16000000)==WifiAction::None);
    CHECK(absent.update(State::Connecting,false,20999999)==WifiAction::None);
    CHECK(absent.update(State::Connecting,false,21000000)==WifiAction::Setup);

    // Failed credentials produce disconnect events, each followed by retry backoff.
    WifiPolicy wrong(true,0);
    CHECK(wrong.update(State::Disconnected,false,0)==WifiAction::Connect);
    CHECK(wrong.update(State::Connecting,false,0)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,2000000)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,6999999)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,7000000)==WifiAction::Connect);
    CHECK(wrong.update(State::Connecting,false,7000000)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,9000000)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,13999999)==WifiAction::None);
    CHECK(wrong.update(State::Disconnected,false,14000000)==WifiAction::Connect);
    CHECK(wrong.update(State::Connecting,false,14000000)==WifiAction::None);
    CHECK(wrong.update(State::Connecting,false,19999999)==WifiAction::None);
    CHECK(wrong.update(State::Connecting,false,20000000)==WifiAction::Setup);

    // Reported hardware sequence: association at 12 s, with no IP at 16 or 21 s.
    WifiPolicy slow_dhcp(true,0);
    CHECK(slow_dhcp.update(State::Disconnected,false,0)==WifiAction::Connect);
    CHECK(slow_dhcp.update(State::Connecting,false,0)==WifiAction::None);
    CHECK(slow_dhcp.update(State::WaitingForIp,false,12000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::WaitingForIp,false,16000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::WaitingForIp,false,21000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::WaitingForIp,false,69999999)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Online,false,70000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Online,false,72000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Disconnected,false,73000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Disconnected,false,77999999)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Disconnected,false,78000000)==WifiAction::Connect);
    CHECK(slow_dhcp.update(State::Connecting,false,78000000)==WifiAction::None);
    CHECK(slow_dhcp.update(State::Online,false,79000000)==WifiAction::None);

    // An initial association receives exactly 60 s to obtain a lease, then setup.
    WifiPolicy no_lease(true,0);
    CHECK(no_lease.update(State::Disconnected,false,0)==WifiAction::Connect);
    CHECK(no_lease.update(State::Connecting,false,0)==WifiAction::None);
    CHECK(no_lease.update(State::WaitingForIp,false,12000000)==WifiAction::None);
    CHECK(no_lease.update(State::WaitingForIp,false,21000000)==WifiAction::None);
    CHECK(no_lease.update(State::WaitingForIp,false,71999999)==WifiAction::None);
    CHECK(no_lease.update(State::WaitingForIp,false,72000000)==WifiAction::Setup);
    CHECK(no_lease.update(State::WaitingForIp,true,73000000)==WifiAction::None);

    // Once online, an association timeout requests one disconnect, never setup.
    WifiPolicy reconnect(true,0);
    CHECK(reconnect.update(State::Online,false,0)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,10000000)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,14999999)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,15000000)==WifiAction::Connect);
    CHECK(reconnect.update(State::Connecting,false,15000000)==WifiAction::None);
    CHECK(reconnect.update(State::Connecting,false,34999999)==WifiAction::None);
    CHECK(reconnect.update(State::Connecting,false,35000000)==WifiAction::Disconnect);
    CHECK(reconnect.update(State::Connecting,false,35250000)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,35500000)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,40499999)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,40500000)==WifiAction::Connect);
    CHECK(reconnect.update(State::Connecting,false,40500000)==WifiAction::None);
    CHECK(reconnect.update(State::WaitingForIp,false,41000000)==WifiAction::None);
    CHECK(reconnect.update(State::Online,false,42000000)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,3600000000LL)==WifiAction::None);
    CHECK(reconnect.update(State::Disconnected,false,3605000000LL)==WifiAction::Connect);

    // Losing only the IP address also waits for DHCP; a failed renewal reconnects.
    WifiPolicy lost_ip(true,0);
    CHECK(lost_ip.update(State::Online,false,0)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,1000000)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,60999999)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,61000000)==WifiAction::Disconnect);
    CHECK(lost_ip.update(State::WaitingForIp,false,62000000)==WifiAction::None);
    CHECK(lost_ip.update(State::Disconnected,false,63000000)==WifiAction::None);
    CHECK(lost_ip.update(State::Disconnected,false,67999999)==WifiAction::None);
    CHECK(lost_ip.update(State::Disconnected,false,68000000)==WifiAction::Connect);
    CHECK(lost_ip.update(State::Connecting,false,68000000)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,69000000)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,128999999)==WifiAction::None);
    CHECK(lost_ip.update(State::Online,false,129000000)==WifiAction::None);
    CHECK(lost_ip.update(State::WaitingForIp,false,130000000)==WifiAction::None);
    CHECK(lost_ip.update(State::Online,false,130250000)==WifiAction::None);

    WifiPolicy setup(true,0);
    CHECK(setup.update(State::Disconnected,true,0)==WifiAction::None);
    CHECK(setup.update(State::Connecting,true,21000000)==WifiAction::None);
    CHECK(setup.update(State::WaitingForIp,true,90000000)==WifiAction::None);
    CHECK(setup.update(State::Online,true,100000000)==WifiAction::None);
}
static void physics_tests() {
    CoinPhysicsEngine p(42);for(unsigned i=0;i<MAX_COINS+7;++i)p.spawn();CHECK(count(p)==MAX_COINS);
    bool bounce=false,rotation=false;float previous_y=p.coins()[0].y;
    for(int frame=0;frame<2000;++frame) {
        p.update(frame==10?20.f:.04f);
        for(const auto &c:p.coins()) {
            CHECK(std::isfinite(c.x)&&std::isfinite(c.y));CHECK(c.x-c.radius>=p.LEFT-.01f);
            CHECK(c.x+c.radius<=p.RIGHT+.01f);CHECK(c.y+c.radius<=p.FLOOR+.01f);
            CHECK(c.radius>=CoinPhysicsEngine::MIN_RADIUS && c.radius<=CoinPhysicsEngine::MAX_RADIUS);if(c.vy<0)bounce=true;if(std::abs(c.rotation)>.3f)rotation=true;
        }
    }
    CHECK(bounce&&rotation);CHECK(p.coins()[0].y>previous_y);
    for(const auto &c:p.coins())CHECK(c.sleeping&&c.vx==0&&c.vy==0);
    p.reset();CHECK(count(p)==0);p.rest_coin();CHECK(count(p)==1&&p.coins()[0].sleeping);
    CoinPhysicsEngine a(22),b(22);a.spawn();b.spawn();a.update(8);b.update(.05f);CHECK(a.coins()[0].y==b.coins()[0].y);
    const auto y=a.coins()[0].y;a.update(-1);a.update(std::numeric_limits<float>::quiet_NaN());CHECK(a.coins()[0].y==y);
    CoinPhysicsEngine fast(5),slow(5);fast.spawn();slow.spawn();for(int i=0;i<30;++i)fast.update(1.f/30);for(int i=0;i<20;++i)slow.update(.05f);
    CHECK(std::abs(fast.coins()[0].y-slow.coins()[0].y)<4);
    UiAnimation anim(45);auto s=at(config_defaults(),11,59,59);anim.update(s,0,.04f);const auto n=count(anim.physics());
    s=at(config_defaults(),12,0);for(int i=0;i<500;++i)anim.update(s,i*40000LL,.04f);CHECK(count(anim.physics())==n);
    s=at(config_defaults(),17,59,59);anim.update(s,30000000,.04f);const auto before_end=count(anim.physics());
    s=at(config_defaults(),18,0);anim.update(s,31000000,.04f);
    CHECK(count(anim.physics())==before_end);
    anim.update(s,40000000,.04f);CHECK(count(anim.physics())==before_end);
    s=at(config_defaults(),17,59,59);anim.update(s,41000000,.04f);const auto resumed=count(anim.physics());
    s=at(config_defaults(),18,0);anim.update(s,42000000,.04f);CHECK(count(anim.physics())==resumed);
    UiAnimation boot;boot.update(s,0,.04f);CHECK(count(boot.physics())==0);
}
static void ppm(const std::string &path,const std::vector<uint16_t> &frame) {
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 170\n255\n";
    for(auto p:frame) { const char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};out.write(rgb,3); }
}
static void render_tests(const char *directory) {
    UiModel m{};m.config=config_defaults();m.system=SYSTEM_RUNNING;m.synced=m.connected=true;
    m.salary=at(m.config,14,37,21);std::strcpy(m.clock,"14:37:21");std::strcpy(m.ap_ssid,"SalaryThief-A31F");
    std::strcpy(m.date,"2026/09/23");
    std::strcpy(m.ssid,"Office Wi-Fi");std::strcpy(m.ip,"192.168.1.25");std::strcpy(m.idf,"v5.5.0");std::strcpy(m.firmware,APP_FIRMWARE_VERSION);
    m.rssi=-53;m.free_heap=110000;m.free_psram=7300000;m.uptime=2451;
    CoinPhysicsEngine p(17);
    const auto pile=[&]() {
        p.reset();
        for(unsigned i=0;i<12;++i) { p.spawn(); for(int step=0;step<40;++step)p.update(.04f); }
        for(int step=0;step<200;++step)p.update(.04f);
    };
    pile();p.spawn();for(int i=0;i<9;++i)p.update(.04f);m.physics=&p;
    std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0x55aa);
    for(int scene=0;scene<11;++scene) {
        m.page=scene<4?scene:0;m.system=scene==4?SYSTEM_SETUP_MODE:SYSTEM_RUNNING;m.synced=scene!=5;m.held_ms=0;
        if(scene==6){m.salary=at(m.config,8,43,22);std::strcpy(m.clock,"08:43:22");p.rest_coin();}
        if(scene==7){m.salary=at(m.config,12,30);std::strcpy(m.clock,"12:30:00");pile();}
        if(scene==8){m.salary=at(m.config,18,0);std::strcpy(m.clock,"18:00:00");}
        if(scene==9){m.salary=at(m.config,12,0,0,26);std::strcpy(m.date,"2026/09/26");std::strcpy(m.clock,"12:00:00");p.rest_coin();}
        if(scene==10)m.held_ms=7000;
        ui_render(full.data(),0,170,m);
        for(int y=0;y<170;y+=10){ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa&&guard.back()==0x55aa);std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);}
        CHECK(full==strip);ppm(std::string(directory)+"/scene_"+std::to_string(scene)+".ppm",full);
    }
    m.synced=true;m.held_ms=0;m.page=0;p.reset();
    std::strcpy(m.date,"2026/09/23");
    // Faster spawn cadence for a short demonstration; physics still uses 25 FPS.
    for(int i=0;i<400;++i) {
        if(i%18==0 && i/18<int(MAX_COINS))p.spawn();
        p.update(.04f);m.salary=at(m.config,10,0,i/25);
        std::snprintf(m.clock,sizeof(m.clock),"10:00:%02d",i/25);
        ui_render(full.data(),0,170,m);ppm(std::string(directory)+"/coin_"+std::to_string(i)+".ppm",full);
    }
    ppm(std::string(directory)+"/stack_settled.ppm",full);
    // Exercise every animation frame through both framebuffer paths, with the
    // salary and clock held still to verify motion uses its own frame timestamp.
    for (int scene=0;scene<2;++scene) {
        m.salary=at(m.config,scene?18:12,0);std::strcpy(m.clock,scene?"18:00:00":"12:00:00");
        std::vector<uint16_t> first;
        bool moved=false;
        for (int frame=0;frame<(scene?100:120);++frame) {
            m.animation_ms=frame*40;
            ui_render(full.data(),0,170,m);
            if (frame==0) first=full;
            else moved|=full!=first;
            for (int y=0;y<170;y+=10) {
                ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa&&guard.back()==0x55aa);
                std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
            }
            CHECK(full==strip);
            bool outside_unchanged=true;
            for (int y=0;y<170;++y) for (int x=0;x<320;++x)
                if (x<200 || x>=318 || y<29 || y>=145) outside_unchanged&=full[y*320+x]==first[y*320+x];
            CHECK(outside_unchanged);
            ppm(std::string(directory)+(scene?"/rest_":"/lunch_")+std::to_string(frame)+".ppm",full);
        }
        CHECK(moved);
        // Hidden coins must never leak into the meal/rest scenes.
        m.physics=nullptr;ui_render(strip.data(),0,170,m);CHECK(full==strip);m.physics=&p;
    }
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    config_tests();salary_tests(argv[1]);button_tests();wifi_tests();physics_tests();render_tests(argv[2]);
    std::printf("PASS: %u checks (salary, Gregorian calendar, configuration, button, Wi-Fi policy, physics, animation, rendering)\n",checks);
}

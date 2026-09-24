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
    CHECK(at(c,12,0,0,25).work_state==WORK_STATE_DAY_OFF);CHECK(at(c,12,0,0,26).work_state==WORK_STATE_DAY_OFF);
    CHECK(at(c,12,0,0,27).remaining_money==0);CHECK(at(c,9,0,0,28).work_state==WORK_STATE_DAY_OFF);
    CHECK(at(c,12,0).monthly_work_days==20 && at(c,12,0).monthly_work_seconds==160*3600);
    CHECK(at(c,18,0).daily_salary==2000);
    const auto removed=calculate_salary(c,local(2025,9,23,18,0,0),0,true);
    CHECK(removed.work_state==WORK_STATE_NO_CALENDAR && removed.monthly_work_seconds==0);
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
    CHECK(at(c,16,30).monthly_work_seconds==20*27900);
    // Legacy weekday selections cannot override a national holiday or makeup day.
    c.work_days=1<<5;CHECK(at(c,12,0,0,26).work_state==WORK_STATE_DAY_OFF);CHECK(at(c,12,0,0,23).work_state==WORK_STATE_LUNCH);
    std::ifstream file(oracle);CHECK(file.good());int year,month,day,working,expected,index;unsigned cases=0;
    double month_total=0;
    while(file>>year>>month>>day>>working>>expected>>index) {
        if(day==1)month_total=0;
        const auto s=calculate_salary(config_defaults(),local(year,month,day,18,0,0),0,true);
        CHECK(calculate_work_days_in_month(year,month)==expected);
        CHECK(s.monthly_work_days==expected && s.work_day_index==index);
        CHECK(s.monthly_work_seconds==uint32_t(expected*28800));
        CHECK(s.work_state==(working?WORK_STATE_AFTER_WORK:WORK_STATE_DAY_OFF));
        CHECK(std::abs(s.earned_money-(working?40000.0/expected:0))<1e-8);
        month_total+=s.earned_money;
        if(working && index==expected)CHECK(std::abs(month_total-40000)<1e-7);
        ++cases;
    }
    CHECK(cases>=730);
    CHECK(calculate_work_days_in_month(2026,2)==14);
    CHECK(calculate_work_days_in_month(2026,13)==-1);
    CHECK(calculate_salary(c,local(2026,2,29,12,0,0),0,true).work_state==WORK_STATE_NO_TIME);
    CHECK(calculate_salary(c,local(2028,2,29,12,0,0),0,true).work_state==WORK_STATE_NO_CALENDAR);
    const auto missing=calculate_salary(c,local(2099,1,1,12,0,0),0,true);
    CHECK(missing.work_state==WORK_STATE_NO_CALENDAR && missing.monthly_work_seconds==0 && missing.earned_money==0);
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

    // A valid RTC keeps the clock screen available at boot while Wi-Fi retries.
    WifiPolicy rtc(true,0);
    CHECK(rtc.update(State::Disconnected,false,0,true)==WifiAction::Connect);
    CHECK(rtc.update(State::Connecting,false,0,true)==WifiAction::None);
    CHECK(rtc.update(State::Connecting,false,20000000,true)==WifiAction::Disconnect);
    CHECK(rtc.update(State::Disconnected,false,21000000,true)==WifiAction::None);
    CHECK(rtc.update(State::Disconnected,false,26000000,true)==WifiAction::Connect);
    CHECK(rtc.update(State::WaitingForIp,false,30000000,true)==WifiAction::None);
    CHECK(rtc.update(State::WaitingForIp,false,90000000,true)==WifiAction::Disconnect);
    CHECK(rtc.update(State::Online,false,95000000,true)==WifiAction::None);
    CHECK(rtc.update(State::Disconnected,true,100000000,true)==WifiAction::None);
    WifiPolicy unconfigured(false,0);
    CHECK(unconfigured.update(State::Disconnected,false,0,true)==WifiAction::Setup);
    WifiPolicy invalid_rtc(true,0);
    CHECK(invalid_rtc.update(State::Disconnected,false,20000000,false)==WifiAction::Setup);
}
static void battery_tests() {
    using State=BatteryState;
    BatteryMonitor monitor;
    CHECK(BatteryStatus{}.state==State::Measuring);
    CHECK(monitor.update(3850).state==State::Measuring);
    CHECK(monitor.update(3840).state==State::Measuring);
    auto battery=monitor.update(3860);
    CHECK(battery.state==State::BatteryPower && battery.supply_mv==3860);
    // USB insertion immediately removes the previous battery inference.
    CHECK(monitor.update(4750).state==State::Measuring);
    CHECK(monitor.update(4700).state==State::Measuring);
    CHECK(monitor.update(4730).state==State::ExternalPower);
    // USB removal likewise requires new stable samples, without averaging rails.
    CHECK(monitor.update(4050).state==State::Measuring);
    CHECK(monitor.update(4040).state==State::Measuring);
    CHECK(monitor.update(4060).state==State::BatteryPower);
    auto failed=monitor.update(0);
    CHECK(failed.state==State::Unavailable && failed.supply_mv==0);
    CHECK(monitor.update(3860).state==State::Measuring);
    CHECK(monitor.update(3860).state==State::Measuring);
    CHECK(monitor.update(3860).state==State::BatteryPower);
    for (const int mv:{4201,4300,4399}) {
        BatteryMonitor ambiguous;
        ambiguous.update(mv);ambiguous.update(mv);
        CHECK(ambiguous.update(mv).state==State::Unknown);
    }
    for (const int mv:{-1,2499,5501,65536}) {
        CHECK(monitor.update(mv).state==State::Unavailable);
        CHECK(monitor.update(mv).supply_mv==0);
    }
    for (const int mv:{2500,4200,4400,5500}) {
        BatteryMonitor boundary;boundary.update(mv);boundary.update(mv);
        CHECK(boundary.update(mv).state==(mv<=4200?State::BatteryPower:State::ExternalPower));
    }
    for (int i=0;i<20;++i) CHECK(monitor.update(i%2?4100:4600).state==State::Measuring);
    CHECK(std::strcmp(battery_state_name(State::ExternalPower),"external_power")==0);
    CHECK(std::strcmp(battery_state_name(State::Unknown),"unknown")==0);
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
static void money_gain_tests() {
    UiAnimation animation;
    auto s=at(config_defaults(),10,0);
    s.earned_money=100.00;
    animation.update(s,0,.04f);CHECK(animation.gain_money()==0);
    s.earned_money=100.22;
    animation.update(s,1000000,.04f);
    CHECK(std::abs(animation.gain_money()-.22)<1e-9 && animation.gain_progress()==0);
    animation.update(s,1450000,.04f);
    CHECK(animation.gain_progress()==.5f && std::abs(animation.gain_money()-.22)<1e-9);
    // Real elapsed time expires a popup even if the display missed many frames.
    animation.update(s,1900000,.04f);CHECK(animation.gain_money()==0);
    const auto pulse_before=animation.pulse();
    s.earned_money=100.224;
    animation.update(s,2000000,.04f);CHECK(animation.gain_money()==0 && animation.pulse()<pulse_before);
    s.earned_money=100.226;
    animation.update(s,3000000,.04f);CHECK(std::abs(animation.gain_money()-.01)<1e-9);
    s.earned_money=100.45;
    animation.update(s,3200000,.04f);
    CHECK(std::abs(animation.gain_money()-.22)<1e-9 && animation.gain_progress()==0);
    s.earned_money=90;
    animation.update(s,3300000,.04f);CHECK(animation.gain_money()==0 && animation.pulse()==0);
    s.earned_money=90.22;animation.update(s,3400000,.04f);
    ++s.date_key;s.earned_money=120;
    animation.update(s,3500000,.04f);CHECK(animation.gain_money()==0 && animation.pulse()==0);
    // The last paid second can arrive exactly when the lunch/rest scene starts.
    s.work_state=WORK_STATE_LUNCH;s.earned_money=120.22;
    animation.update(s,4000000,.04f);CHECK(std::abs(animation.gain_money()-.22)<1e-9);
    animation.update(s,5000000,.04f);CHECK(animation.gain_money()==0);
    s.work_state=WORK_STATE_NO_TIME;s.date_key=0;s.earned_money=0;
    animation.update(s,6000000,.04f);CHECK(animation.gain_money()==0);
    s=at(config_defaults(),14,0);
    animation.update(s,7000000,.04f);CHECK(animation.gain_money()==0);
}
static void transition_tests() {
    const auto config=config_defaults();
    const int boundaries[]={config.work_start,config.lunch_start,config.lunch_end,config.work_end};
    const WorkState states[]={WORK_STATE_WORKING_MORNING,WORK_STATE_LUNCH,
        WORK_STATE_WORKING_AFTERNOON,WORK_STATE_AFTER_WORK};
    for (int event=0;event<4;++event) {
        UiAnimation animation;
        const int minute=boundaries[event];
        const auto before=at(config,(minute-1)/60,(minute-1)%60,59);
        const auto after=at(config,minute/60,minute%60);
        animation.update(before,0,.04f);CHECK(animation.transition_state()==WORK_STATE_NO_TIME);
        animation.update(after,1000000,.04f);
        CHECK(animation.transition_state()==states[event] && animation.transition_progress()==0);
        animation.update(after,2600000,.04f);CHECK(animation.transition_progress()==.5f);
        animation.update(after,4200000,.04f);CHECK(animation.transition_state()==WORK_STATE_NO_TIME);
        animation.update(after,5000000,.04f);CHECK(animation.transition_state()==WORK_STATE_NO_TIME);
        UiAnimation boot;boot.update(after,0,.04f);CHECK(boot.transition_state()==WORK_STATE_NO_TIME);
        boot.update(before,1000000,.04f);CHECK(boot.transition_state()==WORK_STATE_NO_TIME);
    }
    // A custom schedule still triggers from salary states, not hard-coded times.
    auto custom=config;custom.work_start=8*60;custom.lunch_start=11*60+30;
    custom.lunch_end=12*60+15;custom.work_end=16*60+30;
    UiAnimation animation;
    animation.update(at(custom,11,29,59),0,.04f);
    animation.update(at(custom,11,30),1000000,.04f);CHECK(animation.transition_state()==WORK_STATE_LUNCH);
    animation.update(at(custom,11,30,0,24),1100000,.04f);CHECK(animation.transition_state()==WORK_STATE_NO_TIME);
    animation.update(SalaryStatus{},1200000,.04f);
    animation.update(at(custom,16,30),1300000,.04f);CHECK(animation.transition_state()==WORK_STATE_NO_TIME);
    UiAnimation holiday;
    holiday.update(at(config,23,59,59,24),0,.04f);
    holiday.update(at(config,0,0,0,25),1000000,.04f);CHECK(holiday.transition_state()==WORK_STATE_DAY_OFF);
    holiday.update(at(config,0,0,4,25),5000000,.04f);CHECK(holiday.transition_state()==WORK_STATE_NO_TIME);
    holiday.update(at(config,12,0,0,25),6000000,.04f);CHECK(holiday.transition_state()==WORK_STATE_NO_TIME);
    holiday.update(at(config,0,0,0,26),7000000,.04f);CHECK(holiday.transition_state()==WORK_STATE_DAY_OFF);
    UiAnimation holiday_boot;holiday_boot.update(at(config,12,0,0,28),0,.04f);
    CHECK(holiday_boot.transition_state()==WORK_STATE_DAY_OFF);
    CHECK(holiday_boot.gain_money()==0 && count(holiday_boot.physics())==1);
}
static void ppm(const std::string &path,const std::vector<uint16_t> &frame) {
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 170\n255\n";
    for(auto p:frame) { const char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};out.write(rgb,3); }
}
static void render_tests(const char *directory) {
    UiModel m{};m.config=config_defaults();m.system=SYSTEM_RUNNING;m.synced=m.connected=m.sntp_synced=true;
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
        std::vector<uint16_t> classic;
        for (int theme=0;theme<3;++theme) {
            m.theme=static_cast<DisplayTheme>(theme);
            ui_render(full.data(),0,170,m);
            for(int y=0;y<170;y+=10){ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa&&guard.back()==0x55aa);std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);}
            CHECK(full==strip);
            if (theme==0) {classic=full;ppm(std::string(directory)+"/scene_"+std::to_string(scene)+".ppm",full);}
            else CHECK(full!=classic);
            ppm(std::string(directory)+"/theme_"+std::to_string(theme)+"_scene_"+std::to_string(scene)+".ppm",full);
            if (theme==2) {
                auto colors=full;std::sort(colors.begin(),colors.end());
                CHECK(std::unique(colors.begin(),colors.end())-colors.begin()<=4);
            }
        }
        m.theme=DisplayTheme::Classic;
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
    for (int scene=0;scene<3;++scene) {
        m.salary=at(m.config,scene==1?18:12,0,0,scene==2?25:23);
        std::strcpy(m.clock,scene==1?"18:00:00":"12:00:00");
        std::strcpy(m.date,scene==2?"2026/09/25":"2026/09/23");
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
            ppm(std::string(directory)+(scene==2?"/holiday_":scene==1?"/rest_":"/lunch_")+std::to_string(frame)+".ppm",full);
        }
        CHECK(moved);
        // Hidden coins must never leak into the meal/rest scenes.
        m.physics=nullptr;ui_render(strip.data(),0,170,m);CHECK(full==strip);m.physics=&p;
    }
    // Check popup motion, its bounds, theme palette and both rendering paths.
    m.salary=at(m.config,14,37,21);std::strcpy(m.clock,"14:37:21");m.physics=nullptr;
    std::strcpy(m.date,"2026/09/23");
    for (int theme=0;theme<3;++theme) {
        m.theme=static_cast<DisplayTheme>(theme);m.gain_money=0;
        ui_render(full.data(),0,170,m);const auto baseline=full;
        std::vector<uint16_t> first;
        bool moved=false;
        for (int frame=0;frame<25;++frame) {
            m.gain_money=.22;m.gain_progress=std::min(1.f,float(frame*40000)/900000.f);
            ui_render(full.data(),0,170,m);
            if (frame==0) {first=full;CHECK(full!=baseline);}
            else if (frame<20) moved|=full!=first;
            for (int y=0;y<170;y+=10) {
                ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa&&guard.back()==0x55aa);
                std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
            }
            CHECK(full==strip);
            bool outside_unchanged=true;
            for (int y=0;y<170;++y) for (int x=0;x<320;++x)
                if (x<50 || x>=194 || y<51 || y>=80) outside_unchanged&=full[y*320+x]==baseline[y*320+x];
            CHECK(outside_unchanged);
            if (theme==2) {
                auto colors=full;std::sort(colors.begin(),colors.end());
                CHECK(std::unique(colors.begin(),colors.end())-colors.begin()<=4);
            }
            ppm(std::string(directory)+"/gain_"+std::to_string(theme)+"_"+std::to_string(frame)+".ppm",full);
        }
        CHECK(moved && full==baseline);
    }
}
static void transition_render_tests(const char *directory) {
    UiModel m{};m.config=config_defaults();m.system=SYSTEM_RUNNING;m.synced=true;
    // Offline after synchronization must still show every schedule announcement.
    std::strcpy(m.date,"2026/09/23");
    const int minutes[]={m.config.work_start,m.config.lunch_start,m.config.lunch_end,m.config.work_end,0};
    std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0x55aa);
    for (int event=0;event<5;++event) for (int theme=0;theme<3;++theme) {
        m.theme=static_cast<DisplayTheme>(theme);m.page=0;m.transition_state=WORK_STATE_NO_TIME;
        const int minute=minutes[event];
        m.salary=at(m.config,minute/60,minute%60,0,event==4?25:23);
        std::strcpy(m.date,event==4?"2026/09/25":"2026/09/23");
        std::snprintf(m.clock,sizeof(m.clock),"%02u:%02u:00",unsigned(minute/60)%24U,unsigned(minute)%60U);
        ui_render(full.data(),0,170,m);const auto baseline=full;
        UiAnimation animation;
        animation.update(event==4?at(m.config,23,59,59,24):at(m.config,(minute-1)/60,(minute-1)%60,59),0,.04f);
        bool moved=false;
        for (int frame=0;frame<85;++frame) {
            animation.update(m.salary,1000000+frame*40000LL,.04f);
            m.transition_state=animation.transition_state();m.transition_progress=animation.transition_progress();
            ui_render(full.data(),0,170,m);
            moved|=full!=baseline;
            for (int y=0;y<170;y+=10) {
                ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa&&guard.back()==0x55aa);
                std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
            }
            CHECK(full==strip);
            bool outside_unchanged=true;
            for (int y=0;y<170;++y) for (int x=0;x<320;++x)
                if (x<8 || x>=312 || y<31 || y>=145) outside_unchanged&=full[y*320+x]==baseline[y*320+x];
            CHECK(outside_unchanged);
            if (theme==2) {
                auto colors=full;std::sort(colors.begin(),colors.end());
                CHECK(std::unique(colors.begin(),colors.end())-colors.begin()<=4);
            }
            if (theme==0 || frame==30)
                ppm(std::string(directory)+"/transition_"+std::to_string(event)+"_"+
                    std::to_string(theme)+"_"+std::to_string(frame)+".ppm",full);
            if (frame==30) {
                // The same opaque announcement appears over each of the four pages.
                for (unsigned page=1;page<4;++page) {
                    m.page=page;ui_render(strip.data(),0,170,m);
                    for (int y=31;y<145;++y)
                        CHECK(std::equal(full.begin()+y*320+8,full.begin()+y*320+312,strip.begin()+y*320+8));
                }
                m.page=0;
            }
        }
        CHECK(moved && full==baseline);
    }
    // A transition never obscures setup, error, time-sync or button prompts.
    for (int mode=0;mode<4;++mode) {
        m.system=mode==0?SYSTEM_SETUP_MODE:mode==1?SYSTEM_ERROR:SYSTEM_RUNNING;
        m.synced=mode!=2;m.held_ms=mode==3?700:0;
        m.transition_state=WORK_STATE_NO_TIME;ui_render(full.data(),0,170,m);
        m.transition_state=WORK_STATE_LUNCH;m.transition_progress=.4f;
        ui_render(strip.data(),0,170,m);CHECK(full==strip);
    }
}
static void rtc_render_tests(const char *directory) {
    UiModel m{};m.config=config_defaults();m.system=SYSTEM_RUNNING;m.synced=true;
    m.salary=at(m.config,14,37);std::strcpy(m.clock,"14:37:00");std::strcpy(m.date,"2026/09/24");
    m.rtc.present=m.rtc.valid=true;
    std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0x55aa);
    for (int theme=0;theme<3;++theme) {
        m.theme=static_cast<DisplayTheme>(theme);m.rtc.operation=RtcOperation::None;
        ui_render(full.data(),0,170,m);const auto baseline=full;
        ppm(std::string(directory)+"/rtc_mode_"+std::to_string(theme)+".ppm",full);
        m.connected=true;ui_render(strip.data(),0,170,m);CHECK(full==strip);m.connected=false;
        m.rtc.present=false;ui_render(strip.data(),0,170,m);CHECK(full!=strip);m.rtc.present=true;
        for (int action=0;action<3;++action) {
            m.rtc.operation=action==0?RtcOperation::Read:RtcOperation::Write;
            m.rtc.result=action==2?RtcResult::Failed:RtcResult::Success;
            bool moved=false;std::vector<uint16_t> previous=baseline;
            for (int i=0;i<=80;++i) {
                m.animation_ms=i*40;m.rtc_progress=float(i)/80;
                ui_render(full.data(),0,170,m);
                for (int y=0;y<170;y+=10) {
                    ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa && guard.back()==0x55aa);
                    std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
                }
                CHECK(full==strip);moved|=full!=previous;previous=full;
                for (int y=0;y<170;++y) for (int x=0;x<320;++x)
                    if (y<31 || y>=145 || x<8 || x>=312) CHECK(full[y*320+x]==baseline[y*320+x]);
                ppm(std::string(directory)+"/rtc_"+std::to_string(action)+"_"+std::to_string(theme)+"_"+std::to_string(i)+".ppm",full);
            }
            CHECK(moved && full==baseline);
        }
    }
    // Preserve setup/error/button prompts; show RTC activity on a time-wait screen.
    for (int mode=0;mode<4;++mode) {
        m.system=mode==0?SYSTEM_SETUP_MODE:mode==1?SYSTEM_ERROR:SYSTEM_RUNNING;
        m.held_ms=mode==2?700:0;m.synced=mode!=3;m.rtc_progress=.4f;
        m.rtc.operation=RtcOperation::None;ui_render(full.data(),0,170,m);
        m.rtc.operation=RtcOperation::Write;ui_render(strip.data(),0,170,m);
        CHECK((full==strip)==(mode!=3));
    }
}
static void battery_render_tests(const char *directory) {
    UiModel m{};m.config=config_defaults();m.system=SYSTEM_RUNNING;m.page=3;
    m.synced=m.sntp_synced=m.connected=true;m.rtc.present=true;
    m.salary=at(m.config,14,37);std::strcpy(m.date,"2026/09/24");std::strcpy(m.clock,"14:37:00");
    std::strcpy(m.ssid,"Office Wi-Fi");std::strcpy(m.ip,"192.168.1.25");std::strcpy(m.idf,"v5.5.0");std::strcpy(m.firmware,APP_FIRMWARE_VERSION);
    m.rssi=-53;m.free_heap=110000;m.free_psram=7300000;m.uptime=2451;
    const BatteryStatus cases[]={{BatteryState::BatteryPower,3850},{BatteryState::ExternalPower,4750},
        {BatteryState::Unknown,4310},{BatteryState::Unavailable,0},{BatteryState::Measuring,0}};
    std::vector<uint16_t> full(320*170),strip(320*170),guard(320*10+2,0x55aa);
    for (int theme=0;theme<3;++theme) {
        m.theme=static_cast<DisplayTheme>(theme);m.battery={};ui_render(full.data(),0,170,m);const auto baseline=full;
        for (int state=0;state<5;++state) {
            m.battery=cases[state];ui_render(full.data(),0,170,m);
            for (int y=0;y<170;y+=10) {
                ui_render(guard.data()+1,y,10,m);CHECK(guard.front()==0x55aa && guard.back()==0x55aa);
                std::copy(guard.begin()+1,guard.end()-1,strip.begin()+y*320);
            }
            CHECK(full==strip);
            CHECK(std::equal(full.begin(),full.begin()+132*320,baseline.begin()));
            CHECK(std::equal(full.begin()+146*320,full.end(),baseline.begin()+146*320));
            CHECK((full==baseline)==(state==4));
            ppm(std::string(directory)+"/battery_"+std::to_string(theme)+"_"+std::to_string(state)+".ppm",full);
        }
    }
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    config_tests();salary_tests(argv[1]);button_tests();wifi_tests();battery_tests();physics_tests();money_gain_tests();transition_tests();
    render_tests(argv[2]);transition_render_tests(argv[2]);rtc_render_tests(argv[2]);battery_render_tests(argv[2]);
    std::printf("PASS: %u checks (salary, Gregorian calendar, configuration, button, Wi-Fi policy, battery status, physics, animation, rendering)\n",checks);
}

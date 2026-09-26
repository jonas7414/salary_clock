#include "config_json.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"JSON FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static const std::string body=R"({"config_version":1,"wifi_ssid":"Office","wifi_password":"test-pass","monthly_salary":40000,"work_days":31,"work_start":"09:00","lunch_start":"12:00","lunch_end":"13:00","work_end":"18:00","timezone":"Asia/Taipei"})";
static std::string replace(std::string value,const std::string &from,const std::string &to){value.replace(value.find(from),from.size(),to);return value;}
int main(){
    AppConfig current=config_defaults(),result{};const char *reason=nullptr;
    auto parse=[&](const std::string &s){return config_parse_json(s.data(),s.size(),current,result,&reason);};
    CHECK(parse(body));CHECK(result.monthly_salary==40000&&result.work_days==31&&result.work_start==540);
    CHECK(std::strcmp(result.wifi_password,"test-pass")==0);
    CHECK(!parse(body+"{}"));CHECK(parse(body+" \r\n\t"));CHECK(!parse(body+std::string(1,'\0')));
    CHECK(!parse("[]"));CHECK(!parse("{}"));CHECK(!parse(std::string(2049,' ')));
    CHECK(!parse(std::string(900,'[')+std::string(900,']')));
    CHECK(!parse(replace(body,"40000","{\"deep\":1}")));
    CHECK(!parse(replace(body,"Office","Office\\u0000hidden")));
    CHECK(!parse(replace(body,"40000","1.5")));CHECK(!parse(replace(body,"40000","-2")));
    CHECK(!parse(replace(body,"40000","1e999")));CHECK(!parse(replace(body,"40000","\"40000\"")));
    CHECK(!parse(replace(body,"40000","1000000001")));CHECK(!parse(replace(body,"40000","0")));
    CHECK(!parse(replace(body,"\"work_days\":31","\"work_days\":0")));
    CHECK(!parse(replace(body,"\"work_days\":31","\"work_days\":128")));
    CHECK(!parse(replace(body,"09:00","9:00")));CHECK(!parse(replace(body,"09:00","09:60")));
    CHECK(!parse(replace(body,"09:00","12:00")));CHECK(!parse(replace(body,"18:00","24:00")));
    CHECK(!parse(replace(body,"Asia/Taipei","Europe/Imaginary")));
    CHECK(!parse(replace(body,"Office",std::string(33,'s'))));CHECK(!parse(replace(body,"test-pass","short")));
    CHECK(!parse(replace(body,"\"config_version\":1","\"config_version\":2")));
    CHECK(!parse(replace(body,"\"monthly_salary\":40000","\"monthly_salary\":40000,\"monthly_salary\":2")));
    CHECK(parse(replace(body,"test-pass","")));CHECK(result.wifi_password[0]==0);
    const auto no_password=replace(body,"\"wifi_password\":\"test-pass\",","");
    CHECK(!parse(no_password));std::strcpy(current.wifi_ssid,"Office");std::strcpy(current.wifi_password,"old-password");
    CHECK(parse(no_password));CHECK(std::strcmp(result.wifi_password,"old-password")==0);
    CHECK(!parse(replace(no_password,"Office","Different")));
    DisplayTheme theme=DisplayTheme::Amber;
    auto parse_theme=[&](const std::string &s){return config_parse_json(s.data(),s.size(),current,result,&reason,&theme);};
    CHECK(parse_theme(body));CHECK(theme==DisplayTheme::Amber); // Old clients retain the current preference.
    for (int value=0;value<3;++value) {
        CHECK(parse_theme(replace(body,"\"config_version\":1","\"display_theme\":"+std::to_string(value)+",\"config_version\":1")));
        CHECK(static_cast<int>(theme)==value);
    }
    for (const auto value:{"3","256","-1","1.5","null","true","\"1\""}) {
        CHECK(!parse_theme(replace(body,"\"config_version\":1",std::string("\"display_theme\":")+value+",\"config_version\":1")));
        CHECK(theme==DisplayTheme::Handheld);
    }
    CHECK(!parse_theme(replace(body,"\"config_version\":1","\"display_theme\":1,\"display_theme\":2,\"config_version\":1")));
    DisplaySchedule schedule{};
    auto parse_schedule=[&](const std::string &s){return config_parse_json(s.data(),s.size(),current,result,&reason,&theme,&schedule);};
    auto with_times=[&](const std::string &on,const std::string &off){
        return replace(body,"\"config_version\":1","\"display_on\":"+on+",\"display_off\":"+off+",\"config_version\":1");
    };
    CHECK(parse_schedule(body));CHECK(schedule.on_minute==480 && schedule.off_minute==1140);
    CHECK(parse_schedule(with_times("\"22:00\"","\"07:30\"")));
    CHECK(schedule.on_minute==1320 && schedule.off_minute==450);
    CHECK(parse_schedule(body));CHECK(schedule.on_minute==1320 && schedule.off_minute==450); // Old clients preserve it.
    CHECK(parse_schedule(replace(body,"\"config_version\":1","\"display_on\":\"23:59\",\"config_version\":1")));
    CHECK(schedule.on_minute==1439 && schedule.off_minute==450);
    CHECK(parse_schedule(with_times("\"00:00\"","\"00:00\"")));
    CHECK(schedule.on_minute==0 && schedule.off_minute==0);
    for (const auto value:{"\"24:00\"","\"08:60\"","\"8:00\"","\"-1:00\"","\"08:00:00\"","480","null","true","\"\""}) {
        CHECK(!parse_schedule(with_times(value,"\"19:00\"")));
        CHECK(!parse_schedule(with_times("\"08:00\"",value)));
        CHECK(schedule.on_minute==0 && schedule.off_minute==0);
    }
    CHECK(!parse_schedule(replace(body,"\"config_version\":1","\"display_on\":\"08:00\",\"display_on\":\"09:00\",\"config_version\":1")));
    CHECK(!parse_schedule(replace(with_times("\"08:00\"","\"19:00\""),"Office","")));
    CHECK(schedule.on_minute==0 && schedule.off_minute==0); // No mutation on other validation errors.
    DisplayPreferences prefs{};
    auto parse_prefs=[&](const std::string &extra){const auto s=replace(body,"\"config_version\":1",extra+",\"config_version\":1");return config_parse_json(s.data(),s.size(),current,result,&reason,&theme,&schedule,&prefs);};
    CHECK(parse_prefs("\"page_order\":\"450123\",\"anniversary_name\":\"Our day\",\"anniversary_date\":\"2024-02-29\",\"anniversary_annual\":1"));
    CHECK(std::strcmp(prefs.order,"450123")==0 && std::strcmp(prefs.anniversary_date,"2024-02-29")==0);
    CHECK(config_parse_json(body.data(),body.size(),current,result,&reason,&theme,&schedule,&prefs));
    CHECK(std::strcmp(prefs.order,"450123")==0); // Missing fields preserve the preference.
    for (const auto field:{"\"page_order\":\"001234\"","\"page_order\":\"0123456\"","\"page_order\":\"01234\"",
        "\"page_order\":[0,1,2,4,5,3]","\"anniversary_date\":\"2025-02-29\"","\"anniversary_date\":\"2024-04-31\"",
        "\"anniversary_name\":\"\"","\"anniversary_annual\":2","\"anniversary_name\":\"\\ud83d\\ude00\"",
        "\"anniversary_name\":\"bad\\nname\"","\"anniversary_name\":\"                         \""}) {
        const auto before=prefs; CHECK(!parse_prefs(field)); CHECK(std::memcmp(&prefs,&before,sizeof(prefs))==0);
    }
    CHECK(parse_prefs("\"anniversary_name\":\"\\u6211\\u5011\\u7684\\u7d00\\u5ff5\\u65e5\""));
    CHECK(parse_prefs("\"anniversary_name\":\"\",\"anniversary_date\":\"\""));
    std::printf("PASS: %u JSON/API settings checks (types, ranges, schedules, duplicates, version, credentials, oversized bodies, preferences)\n",checks);
}

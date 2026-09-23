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
    std::printf("PASS: %u JSON/API settings checks (types, ranges, schedules, duplicates, version, credentials, oversized bodies)\n",checks);
}

#include "calendar_download.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
static unsigned checks;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } ++checks; } while(0)
struct Input { const std::string &text; size_t at{}; int end{-1}; };
int read(void *p) { auto &s=*static_cast<Input*>(p); return s.at<s.text.size() ? uint8_t(s.text[s.at++]) : s.end; }
bool parse(const std::string &s,int year,CalendarYear &out,int end=-1) { Input in{s,0,end}; return calendar_parse_year(read,&in,year,out); }
std::string fixture(int year,int omitted=0,bool duplicate=false) {
    std::ostringstream s; s<<"{\"metadata\":{\"test\":[true,false,null,-1.25e+3,\"escaped\\\"\\u4e2d\"]},\"months\":[";
    for (int m=1;m<=12;++m) {
        if (m>1) s<<',';
        s<<"{\"holidays\":["; bool first=true;
        for (int d=1;d<=calendar_days_in_month(year,m);++d) {
            if (m==2 && d==omitted) continue;
            if (!first) s<<',';
            first=false;
            const int date=year*10000+m*100+(duplicate && m==2 && d==2 ? 1 : d);
            s<<"{\"isHoliday\":"<<(d%3==0 ? "true":"false")<<",\"date\":\""<<date<<"\"}";
        }
        s<<"],\"month\":"<<m<<'}';
    }
    s<<"],\"year\":"<<year<<'}'; return s.str();
}
std::string replace(std::string s,const std::string &a,const std::string &b) { const auto at=s.find(a); CHECK(at!=std::string::npos); s.replace(at,a.size(),b); return s; }
int main(int argc,char **argv) {
    CalendarYear entry{};
    for (int year : {2026,2027,2028,2100,2400}) {
        const auto json=fixture(year); CHECK(parse(json,year,entry)); CHECK(calendar_year_valid(entry));
        for (int m=1;m<=12;++m) for (int d=1;d<=calendar_days_in_month(year,m);++d)
            CHECK(bool(entry.workdays[m-1]&(1U<<(d-1)))==(d%3!=0));
    }
    const auto json=fixture(2028);
    CalendarYear unchanged{}; unchanged.year=7777;
    CHECK(!parse(fixture(2028,29),2028,unchanged)); CHECK(unchanged.year==7777);
    CHECK(!parse(fixture(2028,0,true),2028,unchanged));
    CHECK(!parse(json,2027,unchanged));
    CHECK(!parse(replace(json,"\"isHoliday\":false","\"isHoliday\":0"),2028,unchanged));
    CHECK(!parse(replace(json,"20280101","20280132"),2028,unchanged));
    CHECK(!parse(replace(json,"\"month\":2","\"month\":1"),2028,unchanged));
    CHECK(!parse(replace(json,"\"year\":2028","\"year\":2028,\"year\":2028"),2028,unchanged));
    CHECK(!parse(replace(json,"\"month\":1}","\"month\":1,}"),2028,unchanged));
    CHECK(!parse(json+"garbage",2028,unchanged));
    CHECK(!parse(json,2028,unchanged,-2));
    CHECK(!parse(replace(json,"\"year\":2028","\"year\":2028.5"),2028,unchanged));
    CHECK(!parse(replace(json,"\"test\":[","\"test\":[[[[[[[[[[[[[[[[[[[[["),2028,unchanged));
    CHECK(!parse(replace(json,"escaped","bad\\q"),2028,unchanged));
    CHECK(!parse(replace(json,"escaped",std::string(161*1024,'x')),2028,unchanged));
    for (size_t n=0;n<json.size();n+=31) CHECK(!parse(json.substr(0,n),2028,unchanged));
    CHECK(parse(replace(json,"\"year\"","\"ye\\u0061r\""),2028,entry));
    CalendarCache cache{}; calendar_cache_seal(cache); CHECK(calendar_cache_valid(cache));
    CHECK(calendar_check_due(cache,20000)); cache.last_attempt_day=20000; calendar_cache_seal(cache);
    CHECK(!calendar_check_due(cache,20000)); CHECK(calendar_check_due(cache,20001));
    CalendarCache reboot=cache; CHECK(!calendar_check_due(reboot,20000));
    CHECK(calendar_check_due(cache,19999)); CHECK(!calendar_check_due(cache,-1));
    calendar_cache_replace(cache,entry); calendar_cache_seal(cache); CHECK(calendar_cache_valid(cache));
    CalendarYear next{}; CHECK(parse(fixture(2029),2029,next)); calendar_cache_replace(cache,next);
    calendar_cache_seal(cache); CHECK(calendar_cache_valid(cache));
    CHECK(taiwan_calendar_install(cache.years)); uint32_t days=0;
    CHECK(taiwan_calendar_month(2028,2,days) && days==entry.workdays[1]);
    CHECK(taiwan_calendar_month(2026,2,days)); // Built-in fallback remains available.
    CHECK(!taiwan_calendar_month(2030,2,days));
    CalendarCache corrupted=cache; corrupted.years[0].workdays[1]^=1;
    CHECK(!calendar_cache_valid(corrupted));
    corrupted=cache; corrupted.years[0].workdays[1]|=1U<<31; calendar_cache_seal(corrupted);
    CHECK(!calendar_cache_valid(corrupted)); CHECK(!taiwan_calendar_install(corrupted.years));
    corrupted=cache; corrupted.years[1]=corrupted.years[0]; calendar_cache_seal(corrupted);
    CHECK(!calendar_cache_valid(corrupted));
    CalendarYear later{}; CHECK(parse(fixture(2030),2030,later)); calendar_cache_replace(cache,later);
    CHECK(cache.years[0].year==2030 && cache.years[1].year==2029);
    CalendarCache gap{}; gap.years[1]=next; calendar_cache_replace(gap,next);
    CHECK(!gap.years[0].year && gap.years[1].year==2029);
    // Concurrent readers must see an entire month's old or new mask.
    CalendarYear a[2]{entry,next},b[2]{entry,next}; b[0].workdays[0]=0;
    CHECK(taiwan_calendar_install(a)); std::atomic<bool> ok{true};
    std::thread reader([&] { for (int i=0;i<20000;++i) { uint32_t value=0;
        if (!taiwan_calendar_month(2028,1,value) || (value!=a[0].workdays[0] && value!=0)) ok=false; } });
    for (int i=0;i<20000;++i) taiwan_calendar_install(i%2 ? a : b);
    reader.join(); CHECK(ok.load());
    CalendarYear clear[2]{}; CHECK(taiwan_calendar_install(clear));
    CHECK(!taiwan_calendar_month(2028,1,days));
    // Optional real upstream files are checked against the bundled reference.
    for (int i=1;i<argc;++i) {
        const int year=2025+i; std::ifstream file(argv[i],std::ios::binary);
        CHECK(file.good()); const std::string live((std::istreambuf_iterator<char>(file)),{});
        CHECK(parse(live,year,entry));
        for (int m=1;m<=12;++m) { CHECK(taiwan_calendar_month(year,m,days)); CHECK(days==entry.workdays[m-1]); }
    }
    std::printf("PASS: %u calendar streaming/cache checks (cache %zu bytes)\n",checks,sizeof(CalendarCache));
}

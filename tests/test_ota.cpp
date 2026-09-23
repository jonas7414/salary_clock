#include "ota_policy.h"
#include "ota_transfer.h"
#include "ota_retry.h"
#include "task_health.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"OTA FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
static int compare(const char *a,const char *b) { ota::Version x{},y{}; CHECK(ota::parse_version(a,x)); CHECK(ota::parse_version(b,y)); return ota::compare_versions(x,y); }
static std::string digest(64,'a');
static std::string asset(const std::string &name="firmware.bin",const std::string &hash="default") {
    return "{\"browser_download_url\":\"https://github.com/test/repo/releases/download/v1.2.0/"+name+
        "\",\"size\":4096,\"name\":\""+name+"\",\"digest\":"+(hash=="default"?"\"sha256:"+digest+"\"":hash)+"}";
}
static std::string release(const std::string &assets) { return "{\"assets\":["+assets+"],\"prerelease\":false,\"tag_name\":\"v1.2.0\",\"draft\":false}"; }
static bool parse(const std::string &s) { ota::Release out{}; const char *reason=nullptr; return ota::parse_release(s.data(),s.size(),"test/repo","firmware.bin",out,&reason); }
static std::string replace(std::string s,const std::string &a,const std::string &b) { auto p=s.find(a); CHECK(p!=std::string::npos); s.replace(p,a.size(),b); return s; }
static void retry_tests() {
    // IDF may return a short positive read on the first timeout, then -0x7007
    // when the next call times out without bytes. Neither may replay data.
    const std::vector<ota::ReadAttempt> trace={{7,false,true},{-0x7007,false,true},
        {-0x7007,false,true},{9,false,false},{0,true,false}};
    size_t cursor=0; unsigned pauses=0; std::vector<int> delivered;
    for (;;) {
        const int count=ota::read_with_retry([&]() { CHECK(cursor<trace.size());return trace[cursor++]; },
            [](){return true;},[&](unsigned retry){CHECK(retry==++pauses);},2);
        CHECK(count>=0); if (!count) break; delivered.push_back(count);
    }
    CHECK(delivered==std::vector<int>({7,9}));CHECK(cursor==trace.size());CHECK(pauses==2);
    unsigned reads=0;pauses=0;
    CHECK(ota::read_with_retry([&](){++reads;return ota::ReadAttempt{-0x7007,false,true};},
        [](){return true;},[&](unsigned){++pauses;},2)==-1);
    CHECK(reads==3 && pauses==2);
    // The errno-based zero-byte timeout variant is also recoverable.
    reads=0;
    CHECK(ota::read_with_retry([&](){return ++reads==1?ota::ReadAttempt{0,false,true}:ota::ReadAttempt{4,false,false};},
        [](){return true;},[](unsigned){},2)==4);CHECK(reads==2);
    for (const auto failure:{ota::ReadAttempt{0,false,false},ota::ReadAttempt{-1,false,false}}) {
        reads=0;pauses=0;
        CHECK(ota::read_with_retry([&](){++reads;return failure;},[](){return true;},
            [&](unsigned){++pauses;},2)==-1);CHECK(reads==1 && pauses==0);
    }
    reads=0;
    CHECK(ota::read_with_retry([&](){++reads;return ota::ReadAttempt{4,false,false};},
        [](){return false;},[](unsigned){},2)==-1);CHECK(reads==0);
    bool online=true;pauses=0;
    CHECK(ota::read_with_retry([&](){online=false;return ota::ReadAttempt{4,false,false};},
        [&](){return online;},[&](unsigned){++pauses;},2)==-1);CHECK(pauses==0);
    int elapsed=0;reads=0;
    CHECK(ota::read_with_retry([&](){++reads;elapsed+=30;return ota::ReadAttempt{-0x7007,false,true};},
        [&](){return elapsed<60;},[](unsigned){},10)==-1);CHECK(reads==2);
    using D=ota::DownloadAttempt;
    unsigned attempts=0;pauses=0;
    CHECK(ota::download_with_retry([&](){return ++attempts<3?D::Retryable:D::Success;},
        [](){return true;},[&](unsigned retry){CHECK(retry==++pauses);},3));
    CHECK(attempts==3 && pauses==2);
    attempts=0;pauses=0;
    CHECK(!ota::download_with_retry([&](){++attempts;return D::Retryable;},
        [](){return true;},[&](unsigned){++pauses;},3));CHECK(attempts==3 && pauses==2);
    attempts=0;
    CHECK(!ota::download_with_retry([&](){++attempts;return D::Failed;},
        [](){return true;},[](unsigned){CHECK(false);},3));CHECK(attempts==1);
    online=true;attempts=0;
    CHECK(!ota::download_with_retry([&](){++attempts;return D::Retryable;},
        [&](){return online;},[&](unsigned){online=false;},3));CHECK(attempts==1);
    // Every restarted transfer starts with fresh bytes/hash state. Only the
    // successful attempt reaches verification and activation.
    attempts=0;unsigned activations=0;std::vector<unsigned> written;
    CHECK(ota::download_with_retry([&](){
        ++attempts;uint8_t data[8]{};unsigned bytes=0;int chunks=0;
        const auto result=ota::transfer_image(data,8,4,12,
            [&](uint8_t *,size_t){return ++chunks==1?(attempts==1?-1:8):0;},
            [&](const uint8_t *,size_t count){bytes+=count;return true;},
            [&](){CHECK(bytes==12);return true;},[](){return true;},
            [&](){++activations;return true;},[](uint32_t,uint32_t){});
        written.push_back(bytes);
        return result==ota::TransferResult::Success?D::Success:D::Retryable;
    },[](){return true;},[](unsigned){},3));
    CHECK(attempts==2 && activations==1);CHECK(written==std::vector<unsigned>({4,12}));
}
static void transfer_tests() {
    using R=ota::TransferResult;
    // Inject failures into the production transfer ordering, not a second model.
    for (int failure=0;failure<8;++failure) {
        uint8_t data[16]{}; std::vector<std::string> calls; int reads=0,writes=0;
        auto result=ota::transfer_image(data,16,8,24,
            [&](uint8_t *,size_t) { ++reads; return reads==1 ? (failure==1?-1:failure==2?8:failure==3?17:16) : 0; },
            [&](const uint8_t *,size_t) { calls.push_back("write"); return !(failure==4 && ++writes==2); },
            [&]() { calls.push_back("digest");return failure!=5; },
            [&]() { calls.push_back("image");return failure!=6; },
            [&]() { calls.push_back("activate");return failure!=7; },
            [](uint32_t,uint32_t) {});
        const R expected[]={R::Success,R::Incomplete,R::Incomplete,R::Oversize,R::WriteFailed,R::DigestFailed,R::ImageFailed,R::ActivateFailed};
        CHECK(result==expected[failure]);
        bool activated=false; for (const auto &call:calls) activated|=call=="activate";
        CHECK(activated==(failure==0 || failure==7));
        if (activated) CHECK(calls==std::vector<std::string>({"write","write","digest","image","activate"}));
    }
    // Disconnect at every possible read boundary and short/chunked reads.
    for (int cutoff=0;cutoff<=48;++cutoff) {
        uint8_t data[16]{}; int read=0; bool activated=false;
        auto result=ota::transfer_image(data,16,1,49,
            [&](uint8_t *,size_t) { if (read++==cutoff) return cutoff==48?0:-1; return 1; },
            [](const uint8_t *,size_t) {return true;},[](){return true;},[](){return true;},
            [&](){activated=true;return true;},[](uint32_t,uint32_t){});
        CHECK(activated==(cutoff==48)); CHECK((result==R::Success)==activated);
    }
}
int main() {
    CHECK(compare("1.9.0","1.10.0")<0);CHECK(compare("1.10.0","2.0.0")<0);CHECK(compare("1.2.9","1.3.0")<0);
    CHECK(compare("v1.2.0","1.2.0")==0);CHECK(compare("2.0.0","1.99.99")>0);
    CHECK(compare("1.0.0+abc","1.0.0+def")==0);
    const char *ordered[]={"1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta","1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.1","1.0.0"};
    for (size_t i=1;i<8;++i) CHECK(compare(ordered[i-1],ordered[i])<0);
    for (const char *bad:{"","v","1","1.0","1.2.3.4","01.2.3","1.02.3","1.2.03","-1.0.0","1.0.0-01","1.0.0-","1.0.0+","1.0.0+a..b","1.0.0 ","1.0.0/xx","4294967296.0.0"}) { ota::Version v{};CHECK(!ota::parse_version(bad,v)); }
    CHECK(ota::https_url_allowed("https://github.com/test")); CHECK(ota::https_url_allowed("https://release-assets.githubusercontent.com/a?sig=abc"));
    for (const char *bad:{"http://github.com/a","https://github.com.evil/a","https://github.com@evil/a","https://evil@github.com/a","https://github.com:443/a","https://github.com/a#b","https://github.com/a\nb","https://github.com\\evil/a"}) CHECK(!ota::https_url_allowed(bad));
    uint8_t sha[32];CHECK(ota::parse_sha256(digest.c_str(),sha));CHECK(sha[0]==0xaa);
    for (const auto &s:{digest,digest+"\n",digest+"  firmware.bin\n",digest+" *firmware.bin\r\n"}) CHECK(ota::parse_checksum_file(s.data(),s.size(),"firmware.bin",sha));
    for (const auto &s:{digest+"  wrong.bin\n",digest+"  firmware.bin.evil",digest.substr(1),digest+"\nextra"}) CHECK(!ota::parse_checksum_file(s.data(),s.size(),"firmware.bin",sha));
    const auto valid=release(asset()); CHECK(parse(valid)); CHECK(parse(release(asset("ignored.txt")+","+asset())));
    CHECK(parse(release(asset("firmware.bin","null")+","+asset("firmware.sha256"))));
    CHECK(!parse(release(asset("firmware.bin","null")))); CHECK(!parse(release(asset()+","+asset())));
    CHECK(!parse(release(asset("other.bin")))); CHECK(!parse(valid+"{}")); CHECK(!parse("{}")); CHECK(!parse("["));
    for (const auto &entry:std::vector<std::pair<std::string,std::string>>{
        {"\"draft\":false","\"draft\":true"},{"\"prerelease\":false","\"prerelease\":true"},
        {"v1.2.0","v1.2.0-rc.1"},{"https://github.com/","http://github.com/"},{"test/repo/","test/other/"},
        {"4096","-1"},{"4096","4096.5"},{"4096","4294967296"},{"4096","0"},{"sha256:","md5:"},
        {"\"tag_name\":","\"tag_name\":\"v1.2.0\",\"tag_name\":"},{"firmware.bin\"","firmware.bin\\u0000\""}}) CHECK(!parse(replace(valid,entry.first,entry.second)));
    CHECK(!parse(std::string(32769,' '))); CHECK(!parse(std::string(13,'[')+"0"+std::string(13,']')));
    CHECK(!parse(valid+std::string(1,0)));
    TaskHealth tasks; CHECK(!tasks.healthy(100,10));
    for (unsigned i=0;i<unsigned(CriticalTask::Count);++i) tasks.beat(CriticalTask(i),100);
    CHECK(tasks.healthy(110,10));CHECK(!tasks.healthy(111,10));CHECK(!tasks.healthy(99,10));
    BootProbation boot(100,30,90);CHECK(boot.update(100,true,false)==BootDecision::Waiting);
    CHECK(boot.update(129,true,false)==BootDecision::Waiting);CHECK(boot.update(130,true,false)==BootDecision::Valid);
    BootProbation unstable(100,30,90);CHECK(unstable.update(100,true,false)==BootDecision::Waiting);
    CHECK(unstable.update(125,false,false)==BootDecision::Waiting);CHECK(unstable.update(130,true,false)==BootDecision::Waiting);
    CHECK(unstable.update(159,true,false)==BootDecision::Waiting);CHECK(unstable.update(160,true,false)==BootDecision::Valid);
    BootProbation failed(100,30,90);CHECK(failed.update(190,false,false)==BootDecision::Rollback);
    CHECK(failed.update(101,true,true)==BootDecision::Rollback);CHECK(failed.update(99,true,false)==BootDecision::Rollback);
    transfer_tests();retry_tests();
    std::printf("PASS: %u OTA checks (SemVer, Release JSON, TLS URL policy, checksums, boot health, transfer faults, bounded network retries)\n",checks);
}

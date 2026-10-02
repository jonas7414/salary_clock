#include "crash_report_status.h"
#include "crash_payload.h"
#include "cJSON.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main(int argc,char **argv) {
    CrashReportPolicy p;
    p.begin(0); p.poll(4000000,false,true);
    assert(p.status.state==CrashReportState::Waiting && !crash_report_visible(p.status));
    p.poll(90000000,true,true); assert(p.status.state==CrashReportState::Hidden);
    p.poll(100000000,true,true); assert(p.status.state==CrashReportState::Hidden);
    p.begin(0); p.poll(4000000,true,false); assert(p.status.state==CrashReportState::Waiting);
    p.poll(5000000,true,true); assert(p.status.state==CrashReportState::Prompt && !p.status.report);
    p.confirm(true); assert(p.status.state==CrashReportState::Hidden);
    p.poll(6000000,true,true); assert(p.status.state==CrashReportState::Hidden);
    p.begin(0); p.poll(4000000,true,true); p.next(); p.confirm(false);
    assert(p.status.state==CrashReportState::Hidden);
    p.begin(0); p.poll(4000000,true,true); p.next(); p.poll(5000000,false,true);
    assert(p.status.state==CrashReportState::Hidden);
    p.begin(0); p.poll(4000000,true,true); p.next(); p.confirm(true);
    assert(p.status.state==CrashReportState::Sending);
    p.confirm(true); assert(p.status.state==CrashReportState::Sending);
    p.complete(false,false,401); assert(p.status.state==CrashReportState::Failed);
    p.poll(5000000,true,true); assert(p.status.state==CrashReportState::Failed);
    p.confirm(true); assert(p.status.state==CrashReportState::Hidden);
    int uploaded=0,erased=0;
    for (int code : {0,200,202,260,301,400,401,429,500}) {
        const auto result=crash_report_deliver([&]() { ++uploaded; return crash_report_acknowledged(true,code); },
                                               [&]() { ++erased; return true; });
        assert(result==CrashReportState::Failed && erased==0);
    }
    assert(!crash_report_acknowledged(false,204));
    assert(crash_report_deliver([&]() { ++uploaded; return true; },[&]() {
        assert(uploaded==10); ++erased; return true;
    })==CrashReportState::Sent);
    assert(erased==1);
    assert(crash_report_deliver([]() { return true; },[]() { return false; })==CrashReportState::CleanupFailed);
    CrashDetails d{};
    std::strcpy(d.device,"salary-clock-TEST00000001");
    std::strcpy(d.reason,"synthetic test: \"quoted\"\\path\nnot a real crash");
    std::strcpy(d.task,"synthetic"); std::strcpy(d.elf_sha256,"test-only"); std::strcpy(d.report_id,"synthetic-001");
    d.pc=0x40371234; d.depth=99; d.dump_bytes=4096;
    for (unsigned i=0;i<16;++i) d.backtrace[i]=d.pc+i*4;
    assert(!crash_report_payload(d,"1.4.8",0));
    char *payload=crash_report_payload(d,"test-only",1790884800123456LL); assert(payload);
    auto *root=cJSON_Parse(payload); assert(root);
    auto *streams=cJSON_GetObjectItem(root,"streams"); assert(cJSON_GetArraySize(streams)==1);
    auto *item=cJSON_GetArrayItem(streams,0);
    auto *labels=cJSON_GetObjectItem(item,"stream");
    assert(!std::strcmp(cJSON_GetObjectItem(labels,"device")->valuestring,d.device));
    auto *values=cJSON_GetObjectItem(item,"values"); assert(cJSON_GetArraySize(values)==1);
    auto *row=cJSON_GetArrayItem(values,0); assert(cJSON_GetArraySize(row)==2);
    assert(!std::strcmp(cJSON_GetArrayItem(row,0)->valuestring,"1790884800123456000"));
    auto *line=cJSON_Parse(cJSON_GetArrayItem(row,1)->valuestring); assert(line);
    assert(!std::strcmp(cJSON_GetObjectItem(line,"reason")->valuestring,d.reason));
    assert(!std::strcmp(cJSON_GetObjectItem(line,"device")->valuestring,d.device));
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(line,"backtrace"))==16);
    assert(!cJSON_GetObjectItem(line,"token") && !cJSON_GetObjectItem(line,"wifi_password"));
    if (argc>1) { FILE *out=std::fopen(argv[1],"wb"); assert(out); std::fputs(payload,out); assert(std::fclose(out)==0); }
    cJSON_Delete(line); cJSON_Delete(root); cJSON_free(payload);
    std::puts("PASS: crash consent/offline/one-attempt policy, acknowledgment-before-erase, Loki JSON escaping and bounded payload");
}

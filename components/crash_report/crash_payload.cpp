#include "crash_payload.h"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>

char *crash_report_payload(const CrashDetails &d,const char *version,int64_t timestamp_us) {
    if (timestamp_us<1767225600000000LL || !d.device[0] || !version) return nullptr;
    cJSON *line=cJSON_CreateObject();
    if (!line) return nullptr;
    bool ok=cJSON_AddStringToObject(line,"event","panic") &&
        cJSON_AddStringToObject(line,"device",d.device) &&
        cJSON_AddStringToObject(line,"report_id",d.report_id) &&
        cJSON_AddStringToObject(line,"reason",d.reason) &&
        cJSON_AddStringToObject(line,"task",d.task) &&
        cJSON_AddStringToObject(line,"crashed_elf_sha256",d.elf_sha256) &&
        cJSON_AddStringToObject(line,"reporter_firmware",version) &&
        cJSON_AddNumberToObject(line,"exception",d.exception) &&
        cJSON_AddNumberToObject(line,"dump_bytes",d.dump_bytes) &&
        cJSON_AddBoolToObject(line,"backtrace_corrupted",d.backtrace_corrupted);
    char hex[16]; std::snprintf(hex,sizeof(hex),"0x%08lx",static_cast<unsigned long>(d.pc));
    ok=ok && cJSON_AddStringToObject(line,"pc",hex);
    cJSON *bt=cJSON_AddArrayToObject(line,"backtrace"); ok=ok && bt;
    for (uint32_t i=0;ok && i<std::min<uint32_t>(d.depth,16);++i) {
        std::snprintf(hex,sizeof(hex),"0x%08lx",static_cast<unsigned long>(d.backtrace[i]));
        cJSON *entry=cJSON_CreateString(hex);
        if (!entry || !cJSON_AddItemToArray(bt,entry)) { cJSON_Delete(entry); ok=false; }
    }
    char *message=ok?cJSON_PrintUnformatted(line):nullptr; cJSON_Delete(line);
    if (!message) return nullptr;
    cJSON *root=cJSON_CreateObject();
    cJSON *streams=root?cJSON_AddArrayToObject(root,"streams"):nullptr;
    cJSON *item=cJSON_CreateObject();
    if (!streams || !item || !cJSON_AddItemToArray(streams,item)) {
        cJSON_Delete(item); cJSON_Delete(root); cJSON_free(message); return nullptr;
    }
    cJSON *labels=cJSON_AddObjectToObject(item,"stream");
    ok=labels && cJSON_AddStringToObject(labels,"job","salary_clock") &&
        cJSON_AddStringToObject(labels,"source","device") && cJSON_AddStringToObject(labels,"device",d.device);
    char timestamp[32];
    // String construction avoids overflow and Loki requires a string of nanoseconds.
    std::snprintf(timestamp,sizeof(timestamp),"%lld000",static_cast<long long>(timestamp_us));
    const char *values[]={timestamp,message};
    cJSON *rows=cJSON_AddArrayToObject(item,"values");
    cJSON *row=cJSON_CreateStringArray(values,2);
    if (!rows || !row || !cJSON_AddItemToArray(rows,row)) { cJSON_Delete(row); ok=false; }
    cJSON_free(message);
    char *payload=ok?cJSON_PrintUnformatted(root):nullptr; cJSON_Delete(root);
    return payload;
}

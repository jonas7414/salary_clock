#pragma once
#include <cstdint>

struct CrashDetails {
    char device[32]{},reason[200]{},task[17]{},elf_sha256[65]{},report_id[24]{};
    uint32_t pc{},exception{},dump_bytes{},backtrace[16]{},depth{};
    bool backtrace_corrupted{};
};
// Returns cJSON-allocated memory; caller releases with cJSON_free.
char *crash_report_payload(const CrashDetails &details,const char *running_version,int64_t timestamp_us);

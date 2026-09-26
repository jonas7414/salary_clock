#pragma once
#include "sdkconfig.h"
#include <cstddef>
#include <cstdint>

namespace ota_config {
constexpr char REPOSITORY[]="jonas7414/salary_clock";
#if CONFIG_SPIRAM
constexpr char ASSET[]="firmware.bin";
#else
constexpr char ASSET[]="firmware-no-psram.bin";
#endif
constexpr int64_t INITIAL_DELAY_US=20*1000000;
constexpr int64_t PROBATION_US=30*1000000, BOOT_DEADLINE_US=90*1000000;
constexpr int64_t HEARTBEAT_MAX_AGE_US=3*1000000;
constexpr int HTTP_TIMEOUT_MS=10000;
constexpr int FIRMWARE_HTTP_TIMEOUT_MS=30000;
constexpr unsigned READ_TIMEOUT_RETRIES=2, DOWNLOAD_ATTEMPTS=3;
constexpr unsigned READ_RETRY_DELAY_MS=250, DOWNLOAD_RETRY_DELAY_MS=1000;
constexpr int64_t REQUEST_DEADLINE_US=60*1000000, DOWNLOAD_DEADLINE_US=10*60*1000000LL;
constexpr size_t STREAM_BUFFER_SIZE=4096, MAX_REDIRECT_URL=2048;
constexpr unsigned MAX_REDIRECTS=5;
}

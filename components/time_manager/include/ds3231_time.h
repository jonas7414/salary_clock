#pragma once
#include <cstdint>
#include <ctime>

// RTC registers always contain UTC. The configured timezone is display-only.
// Limit dates to 2000..2099, within the DS3231's leap-year guarantee.
bool ds3231_decode_time(const uint8_t registers[7],uint8_t status,time_t &utc);
bool ds3231_encode_time(time_t utc,uint8_t registers[7]);

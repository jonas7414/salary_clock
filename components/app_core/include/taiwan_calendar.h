#pragma once
#include <cstdint>

// Bit zero is the first day of the month. False means this month is not bundled.
bool taiwan_calendar_month(int year,int month,uint32_t &workdays);

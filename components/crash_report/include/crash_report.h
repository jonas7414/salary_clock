#pragma once
#include "crash_report_status.h"
#include "esp_err.h"

// Read-only boot report; capture itself is performed by ESP-IDF's panic handler.
void crash_report_print();
esp_err_t crash_report_start();
CrashReportStatus crash_report_snapshot();
void crash_report_next();
void crash_report_confirm();

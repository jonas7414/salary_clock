#pragma once
#include <cstdlib>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103;
#define ESP_ERROR_CHECK(x) do { if((x)!=ESP_OK) std::abort(); } while(0)

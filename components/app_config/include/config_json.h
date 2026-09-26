#pragma once
#include "app_types.h"
#include "display_preferences.h"
#include <cstddef>
bool config_parse_json(const char *body,size_t length,const AppConfig &current,AppConfig &result,const char **reason,
                       DisplayTheme *theme=nullptr,DisplaySchedule *schedule=nullptr,DisplayPreferences *preferences=nullptr);

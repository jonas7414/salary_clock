idf_component_register(SRCS "time_manager.cpp" "ds3231.cpp" "ds3231_time.cpp" INCLUDE_DIRS "include" REQUIRES app_config app_system esp_netif lwip esp_driver_i2c)

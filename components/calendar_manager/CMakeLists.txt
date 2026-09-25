idf_component_register(SRCS "calendar_manager.cpp" "calendar_json.cpp" "calendar_cache.cpp" INCLUDE_DIRS "include" REQUIRES app_core app_config app_system nvs_flash esp_http_client mbedtls esp_timer)

idf_component_register(SRCS "ota_manager.cpp" "ota_policy.cpp" INCLUDE_DIRS "include"
    REQUIRES app_core app_config app_system app_update esp_http_client mbedtls esp_timer esp_system esp_partition json)

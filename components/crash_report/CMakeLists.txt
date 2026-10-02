idf_component_register(SRCS "crash_report.cpp" "crash_upload.cpp" "crash_payload.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core app_system app_config ota_manager espcoredump esp_partition esp_http_client
             mbedtls esp_hw_support esp_timer json)
# Only local builds with explicit credentials enable cloud reporting.
if(EXISTS "${PROJECT_DIR}/.tools/secrets/loki_credentials.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/.tools/secrets")
endif()

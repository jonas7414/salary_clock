idf_component_register(SRCS "setup_portal.cpp" INCLUDE_DIRS "include" REQUIRES app_config app_system esp_http_server esp_wifi json esp_timer EMBED_TXTFILES "web/index.html")

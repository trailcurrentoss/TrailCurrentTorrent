idf_component_register(SRCS "can_handler.c" "pwm_output.c" "wifi_config.c" "main.c" "ota.c" "discovery.c"
                  INCLUDE_DIRS "."
                  REQUIRES driver esp_timer nvs_flash esp_wifi esp_netif esp_http_server esp_app_format mdns app_update can_common)

# Module type for discovery mDNS advertisement
target_compile_definitions(${COMPONENT_LIB} PRIVATE MODULE_TYPE="torrent")

# Module address (0-2): idf.py build -DTORRENT_ADDRESS=1
if(DEFINED TORRENT_ADDRESS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TORRENT_ADDRESS=${TORRENT_ADDRESS})
endif()

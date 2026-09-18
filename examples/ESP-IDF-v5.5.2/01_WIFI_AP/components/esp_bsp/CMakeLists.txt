set(SRC_FILES
    "bsp_can.c"
    "bsp_common.c"
    "bsp_rtc.c"
    "bsp_web.c"
    "bsp_xl2515.c"
    "waveshare_twai_port.c")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_http_server esp_netif nvs_flash driver esp_timer
                    EMBED_TXTFILES "root.html" "rtc.html")

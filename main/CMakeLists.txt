idf_component_register(
    SRCS "wids_main.c" "console_state.c" "console_display.c"
    INCLUDE_DIRS "." ".."
    PRIV_REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_system esp_timer
                  esp_lcd esp_driver_gpio esp_driver_spi
)

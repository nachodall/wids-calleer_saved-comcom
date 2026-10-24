idf_component_register(
    SRCS "demo_main.c" "demo_display.c" "joystick.c" "demo_alert.c"
    INCLUDE_DIRS "." "../../shared"
    PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_timer
                  nvs_flash esp_event esp_netif esp_wifi
)

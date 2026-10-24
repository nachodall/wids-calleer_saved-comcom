idf_component_register(SRCS "wipds_oido.c"
                       INCLUDE_DIRS "." "../../shared"
                       PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash
                                     esp_driver_gpio)

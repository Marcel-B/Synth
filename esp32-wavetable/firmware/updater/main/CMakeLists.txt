# The LEDs and the image layout come from the instruments' sources, so both show and read the same.
idf_component_register(
    SRCS "Updater.cpp" "../../main/hw/Leds.cpp"
    INCLUDE_DIRS "." "../../main"
    REQUIRES esp_driver_rmt esp_driver_gpio esp_wifi esp_netif esp_event esp_http_server app_update spi_flash
             esp_partition nvs_flash esp_timer)
# ESP-IDF's config macros (WIFI_INIT_CONFIG_DEFAULT, HTTPD_DEFAULT_CONFIG) leave fields to their defaults.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
# LEDs that take green, red, blue: -DTONWERK_LED_GRB=1, as for the instruments.
if(TONWERK_LED_GRB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_LED_GRB=1)
endif()

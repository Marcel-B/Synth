idf_component_register(
    SRCS "main.cpp" "dsp/Tables.cpp" "hw/Hardware.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2s esp_driver_spi esp_driver_rmt esp_driver_gpio esp_adc esp_timer nvs_flash esp_psram)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wall -Wextra)
# The bench test (app/Bench.h): idf.py -B build-bench -DTONWERK_BENCH=1 build
if(TONWERK_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_BENCH=1)
endif()

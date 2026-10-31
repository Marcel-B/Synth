# The Physical and PD firmwares (app/PhysicalModule.h, app/PdModule.h) have no wavetables to compute.
if(TONWERK_PHYSICAL OR TONWERK_PD)
    set(sources "main.cpp" "hw/Hardware.cpp")
else()
    set(sources "main.cpp" "dsp/Tables.cpp" "hw/Hardware.cpp")
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2s esp_driver_spi esp_driver_rmt esp_driver_gpio esp_adc esp_timer nvs_flash esp_psram)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wall -Wextra)
# The bench test (app/Bench.h): idf.py -B build-bench -DTONWERK_BENCH=1 build
if(TONWERK_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_BENCH=1)
endif()
# Tonwerk Physical on the same panel: idf.py -B build-physical -DTONWERK_PHYSICAL=1 build
if(TONWERK_PHYSICAL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_PHYSICAL=1)
endif()
# Tonwerk PD on the same panel: idf.py -B build-pd -DTONWERK_PD=1 build
if(TONWERK_PD)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_PD=1)
endif()

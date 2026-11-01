# The Physical, PD, Granular and Additiv firmwares (app/PhysicalModule.h, app/PdModule.h, app/GranularModule.h,
# app/AdditiveModule.h) have no wavetables to compute.
if(TONWERK_PHYSICAL OR TONWERK_PD OR TONWERK_GRANULAR OR TONWERK_ADDITIVE)
    set(sources "main.cpp" "hw/Hardware.cpp" "hw/Leds.cpp")
else()
    set(sources "main.cpp" "dsp/Tables.cpp" "hw/Hardware.cpp" "hw/Leds.cpp")
endif()
idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2s esp_driver_spi esp_driver_rmt esp_driver_gpio esp_adc esp_timer nvs_flash esp_psram app_update)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wall -Wextra)
# LEDs that take green, red, blue (Worldsemi's WS2812D-F5): -DTONWERK_LED_GRB=1. Without it red, green, blue, as the
# APA106s of the first module.
if(TONWERK_LED_GRB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_LED_GRB=1)
endif()
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
# Tonwerk Granular on the same panel: idf.py -B build-granular -DTONWERK_GRANULAR=1 build. Its sources (the plugin's,
# written by `make grain-sources` in host/) go into the partition "tables" with the app, so `idf.py flash` and the
# merged image carry them.
if(TONWERK_GRANULAR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_GRANULAR=1)
    set(grain_sources "${CMAKE_CURRENT_LIST_DIR}/../data/grain-sources.bin")
    if(NOT EXISTS "${grain_sources}")
        message(FATAL_ERROR "data/grain-sources.bin is missing: make -C host grain-sources")
    endif()
    esptool_py_flash_to_partition(flash "tables" "${grain_sources}")
endif()
# Tonwerk Additiv on the same panel: idf.py -B build-additiv -DTONWERK_ADDITIVE=1 build
if(TONWERK_ADDITIVE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TONWERK_ADDITIVE=1)
endif()

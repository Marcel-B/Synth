#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

// Pin plan for an ESP32-S3-DevKitC-1 N16R8 (or a clone with the same pinout). GPIO 33 to 37 belong to the octal
// PSRAM, 19 and 20 to USB, 0, 3, 45 and 46 are strapping pins: none of them carries anything that matters at boot.
namespace pins
{
// The ten pots on ADC1 channels 0 to 9 (GPIO 1 to 10), in the order of twm::Pot. ADC2 would clash with Wi-Fi.
inline constexpr adc_channel_t kPots[10] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4,
    ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7, ADC_CHANNEL_8, ADC_CHANNEL_9,
};

// PCM5102A (I2S, 48 kHz, 32 bit). Its SCK pin goes to ground, so it makes its clock from BCK.
inline constexpr gpio_num_t kI2sBck = GPIO_NUM_11;
inline constexpr gpio_num_t kI2sWs = GPIO_NUM_12;
inline constexpr gpio_num_t kI2sData = GPIO_NUM_13;

// MCP3208 (SPI, 3.3 V, reference 3.0 V from a REF3030).
inline constexpr gpio_num_t kSpiClock = GPIO_NUM_14;
inline constexpr gpio_num_t kSpiMosi = GPIO_NUM_15;
inline constexpr gpio_num_t kSpiMiso = GPIO_NUM_16;
inline constexpr gpio_num_t kAdcSelect = GPIO_NUM_17;

// Gate through an NPN transistor: low while the gate is high.
inline constexpr gpio_num_t kGate = GPIO_NUM_18;

// Buttons to ground, internal pull-ups.
inline constexpr gpio_num_t kWarpButton = GPIO_NUM_21;
inline constexpr gpio_num_t kFilterButton = GPIO_NUM_47;

// Two WS2812-style LEDs in a chain (5 mm through-hole: APA106, or WS2812D-F5 with TONWERK_LED_GRB).
inline constexpr gpio_num_t kLeds = GPIO_NUM_40;
} // namespace pins

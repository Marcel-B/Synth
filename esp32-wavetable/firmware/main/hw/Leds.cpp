// The two LEDs, apart from the rest of the hardware so the updater (../updater) shows the same colours.
#include "Hardware.h"
#include "Pins.h"

#include "driver/rmt_tx.h"
#include "esp_err.h"

namespace hw
{
namespace
{
rmt_channel_handle_t ledChannel = nullptr;
rmt_encoder_handle_t ledEncoder = nullptr;
} // namespace

void startLeds()
{
    rmt_tx_channel_config_t channel = {};
    channel.gpio_num = pins::kLeds;
    channel.clk_src = RMT_CLK_SRC_DEFAULT;
    channel.resolution_hz = 10000000; // 0.1 us per tick
    channel.mem_block_symbols = 64;
    channel.trans_queue_depth = 2;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channel, &ledChannel));
    // WS2812 bits: 0 is 0.3 us high and 0.9 us low, 1 is 0.9 us high and 0.3 us low.
    rmt_bytes_encoder_config_t bytes = {};
    bytes.bit0 = { .duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0 };
    bytes.bit1 = { .duration0 = 9, .level0 = 1, .duration1 = 3, .level1 = 0 };
    bytes.flags.msb_first = 1;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes, &ledEncoder));
    ESP_ERROR_CHECK(rmt_enable(ledChannel));
}

void showLeds(twm::Rgb first, twm::Rgb second)
{
    // A third of full brightness is plenty behind a panel and keeps the 5 V rail calm.
    auto dim = [](uint8_t v) { return (uint8_t) (v / 3); };
    static uint8_t data[6];
    auto put = [&](uint8_t* at, twm::Rgb c) {
#if TONWERK_LED_GRB
        // Worldsemi's own WS2812D-F5 wants green, red, blue.
        at[0] = dim(c.g);
        at[1] = dim(c.r);
#else
        // The 5 mm LEDs sold as WS2812D-F5 on the first module are APA106s (datasheet), which want red, green, blue.
        at[0] = dim(c.r);
        at[1] = dim(c.g);
#endif
        at[2] = dim(c.b);
    };
    put(data, first);
    put(data + 3, second);
    rmt_transmit_config_t config = {};
    rmt_transmit(ledChannel, ledEncoder, data, sizeof(data), &config);
    // The 50 us low that latches the colours comes for free: the next frame is 33 ms away.
}
} // namespace hw

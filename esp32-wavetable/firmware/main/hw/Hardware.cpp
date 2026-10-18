#include "Hardware.h"
#include "Pins.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cmath>

namespace hw
{
namespace
{
const char* kTag = "hw";

// ---------------------------------------------------------------------------------------------------------- audio

i2s_chan_handle_t tx = nullptr;

// ------------------------------------------------------------------------------------------------- inputs: pots

adc_oneshot_unit_handle_t adc = nullptr;
adc_cali_handle_t adcCalibration = nullptr;
/** The pots hang between the REF3030's 3.0 V and ground. */
constexpr float kPotMillivolts = 3000.0f;
/** One-pole smoothing per read: the ESP32's ADC is noisy, a few LSB even on a still pot. */
float potSmooth[twm::kPots] {};

// --------------------------------------------------------------------------------------------- inputs: MCP3208

spi_device_handle_t mcp = nullptr;

int readMcp(int channel)
{
    // Start bit, single-ended, then the channel's three bits; the 12-bit result arrives in the last 12 bits.
    spi_transaction_t t {};
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.length = 24;
    t.tx_data[0] = (uint8_t) (0x06 | ((channel >> 2) & 1));
    t.tx_data[1] = (uint8_t) ((channel & 3) << 6);
    spi_device_polling_transmit(mcp, &t);
    return ((t.rx_data[1] & 0x0f) << 8) | t.rx_data[2];
}

// ----------------------------------------------------------------------------------------------------- LEDs

rmt_channel_handle_t ledChannel = nullptr;
rmt_encoder_handle_t ledEncoder = nullptr;

// ----------------------------------------------------------------------------------------------------- NVS

nvs_handle_t storage = 0;
} // namespace

void startAudio()
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Three buffers of 64 frames: 4 ms of latency at 48 kHz, enough slack for the control task's interrupts.
    chan.dma_desc_num = 3;
    chan.dma_frame_num = 64;
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, nullptr));

    i2s_std_config_t std = {
        // I2S_STD_CLK_DEFAULT_CONFIG spelled out: the macro leaves a field out, which C++ warns about.
        .clk_cfg = { .sample_rate_hz = kSampleRate, .clk_src = I2S_CLK_SRC_DEFAULT, .ext_clk_freq_hz = 0,
                     .mclk_multiple = I2S_MCLK_MULTIPLE_256 },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins::kI2sBck,
            .ws = pins::kI2sWs,
            .dout = pins::kI2sData,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
}

void writeAudio(const float* left, const float* right, int count)
{
    int32_t frames[2 * 64];
    count = std::min(count, 64);
    for (int i = 0; i < count; ++i)
    {
        // The soft clip keeps an overdriven patch round instead of wrapping; the DAC's full scale is ±1.
        frames[2 * i] = (int32_t) (twm::softClip(left[i]) * 2147483000.0f);
        frames[2 * i + 1] = (int32_t) (twm::softClip(right[i]) * 2147483000.0f);
    }
    size_t written = 0;
    i2s_channel_write(tx, frames, (size_t) count * 2 * sizeof(int32_t), &written, portMAX_DELAY);
}

void startInputs()
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1, .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
                                         .ulp_mode = ADC_ULP_MODE_DISABLE };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    // 12 dB attenuation reads up to about 3.1 V: the pots' 3.0 V fits.
    adc_oneshot_chan_cfg_t channel = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    for (auto c : pins::kPots)
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, c, &channel));
    adc_cali_curve_fitting_config_t cali = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0, .atten = ADC_ATTEN_DB_12,
                                             .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cali, &adcCalibration) != ESP_OK)
        ESP_LOGW(kTag, "no ADC calibration in eFuse, pots read uncalibrated");

    spi_bus_config_t bus = {};
    bus.mosi_io_num = pins::kSpiMosi;
    bus.miso_io_num = pins::kSpiMiso;
    bus.sclk_io_num = pins::kSpiClock;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));
    spi_device_interface_config_t device = {};
    // The MCP3208 manages 1 MHz at 2.7 V: a read takes about 30 us, all inputs about a quarter of a millisecond.
    device.clock_speed_hz = 1000000;
    device.mode = 0;
    device.spics_io_num = pins::kAdcSelect;
    device.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &mcp));

    gpio_config_t inputs = {};
    inputs.pin_bit_mask = (1ULL << pins::kGate) | (1ULL << pins::kWarpButton) | (1ULL << pins::kFilterButton);
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&inputs));
}

bool readGate() { return gpio_get_level(pins::kGate) == 0; }

void readInputs(twm::PanelInputs& in, float codes[twm::kCvs])
{
    for (int p = 0; p < twm::kPots; ++p)
    {
        int raw = 0, mv = 0;
        adc_oneshot_read(adc, pins::kPots[p], &raw);
        if (adcCalibration == nullptr || adc_cali_raw_to_voltage(adcCalibration, raw, &mv) != ESP_OK)
            mv = raw * 3100 / 4095;
        const float value = std::clamp((float) mv / kPotMillivolts, 0.0f, 1.0f);
        potSmooth[p] += 0.15f * (value - potSmooth[p]);
        in.pot[p] = potSmooth[p];
    }
    for (int c = 0; c < twm::kCvs; ++c)
    {
        // The V/Oct input averages four reads, which takes its noise down to a few cents; the others need one.
        const int reads = c == twm::pitchCv ? 4 : 1;
        int sum = 0;
        for (int i = 0; i < reads; ++i)
            sum += readMcp(c);
        codes[c] = (float) sum / (float) reads;
    }
    in.gate = readGate();
    in.warpButton = gpio_get_level(pins::kWarpButton) == 0;
    in.filterButton = gpio_get_level(pins::kFilterButton) == 0;
}

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
    // WS2812 wants green, red, blue. A third of full brightness is plenty behind a panel and keeps the 5 V rail calm.
    auto dim = [](uint8_t v) { return (uint8_t) (v / 3); };
    static uint8_t data[6];
    data[0] = dim(first.g);
    data[1] = dim(first.r);
    data[2] = dim(first.b);
    data[3] = dim(second.g);
    data[4] = dim(second.r);
    data[5] = dim(second.b);
    rmt_transmit_config_t config = {};
    rmt_transmit(ledChannel, ledEncoder, data, sizeof(data), &config);
    // The 50 us low that latches the colours comes for free: the next frame is 33 ms away.
}

void startStorage()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("tonwerk", NVS_READWRITE, &storage));
}

bool load(const char* key, void* data, std::size_t size)
{
    std::size_t length = size;
    return nvs_get_blob(storage, key, data, &length) == ESP_OK && length == size;
}

void save(const char* key, const void* data, std::size_t size)
{
    // With XIP from PSRAM (sdkconfig.defaults) the audio task keeps running while the flash is written.
    if (nvs_set_blob(storage, key, data, size) == ESP_OK)
        nvs_commit(storage);
}

void* allocateTables(std::size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

namespace
{
/** The copy's first sector: what it holds. Written last, so a copy cut short by a power-off reads as missing. */
struct TablesHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t bytes;
};
constexpr uint32_t kTablesMagic = 0x54575754; // "TWWT"
constexpr std::size_t kTablesData = 4096;

const esp_partition_t* tablesPartition()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x40, "tables");
}
} // namespace

bool loadTables(void* block, std::size_t bytes, uint32_t version)
{
    const esp_partition_t* part = tablesPartition();
    TablesHeader header {};
    if (part == nullptr || esp_partition_read(part, 0, &header, sizeof header) != ESP_OK)
        return false;
    if (header.magic != kTablesMagic || header.version != version || header.bytes != bytes)
        return false;
    return esp_partition_read(part, kTablesData, block, bytes) == ESP_OK;
}

void storeTables(const void* block, std::size_t bytes, uint32_t version)
{
    const esp_partition_t* part = tablesPartition();
    if (part == nullptr || kTablesData + bytes > part->size)
    {
        ESP_LOGW(kTag, "no partition \"tables\" large enough: the tables are computed at every start");
        return;
    }
    const std::size_t erase = (kTablesData + bytes + part->erase_size - 1) / part->erase_size * part->erase_size;
    if (esp_partition_erase_range(part, 0, erase) != ESP_OK
        || esp_partition_write(part, kTablesData, block, bytes) != ESP_OK)
        return;
    const TablesHeader header { kTablesMagic, version, (uint32_t) bytes };
    esp_partition_write(part, 0, &header, sizeof header);
}
} // namespace hw

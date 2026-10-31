// Tonwerk Wavetable as a Eurorack module: firmware for the ESP32-S3. Built with -DTONWERK_PHYSICAL=1 the same
// panel plays Tonwerk Physical instead (app/PhysicalModule.h, dsp/PhysicalVoice.h), with -DTONWERK_PD=1 Tonwerk PD
// (app/PdModule.h, dsp/PdVoice.h), with -DTONWERK_GRANULAR=1 Tonwerk Granular (app/GranularModule.h,
// dsp/GrainVoice.h, its sources flashed into the partition "tables"); everything else is shared.
//
// Core 1 runs the audio task alone: it renders one control step (32 samples) at a time and blocks on the I2S DMA,
// which paces it. Core 0 runs the control task every 2 ms: pots, CVs, buttons, LEDs, saving. The two share the
// voice's parameters through a sequence lock, so the audio task never waits for the control task.

#if (TONWERK_PHYSICAL + TONWERK_PD + TONWERK_GRANULAR) > 1
#error "one firmware at a time"
#endif
#if TONWERK_PHYSICAL
#include "app/PhysicalModule.h"
#include "dsp/PhysicalVoice.h"
#elif TONWERK_PD
#include "app/PdModule.h"
#include "dsp/PdVoice.h"
#elif TONWERK_GRANULAR
#include "app/GranularModule.h"
#include "dsp/GrainVoice.h"
#else
#include "app/Module.h"
#include "dsp/Tables.h"
#include "dsp/Voice.h"
#endif
#if TONWERK_BENCH
#if TONWERK_PHYSICAL || TONWERK_PD || TONWERK_GRANULAR
#error "the bench test plays the wavetable firmware"
#endif
#include "app/Bench.h"
#endif
#include "hw/Hardware.h"

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace
{
#if TONWERK_PHYSICAL
using Panel = twm::PhysicalModule;
using Instrument = twm::phys::Player;
#elif TONWERK_PD
using Panel = twm::PdModule;
using Instrument = twm::pd::Player;
#elif TONWERK_GRANULAR
using Panel = twm::GranularModule;
using Instrument = twm::grain::Player;
/** The nine sources in PSRAM, set up before the audio task starts. */
twm::grain::Sources sources;
#else
using Panel = twm::Module;
using Instrument = twm::Voice;
#endif
using Params = Panel::Params;

const char* kTag = "tonwerk";
constexpr int kControlMs = 2;
/** Saved this long after the last change of a mode or a hidden value, so turning a knob writes the flash once. */
constexpr int kSaveAfterMs = 3000;

/** The parameters the control task last made; a sequence lock instead of a mutex keeps the audio task wait-free. */
struct SharedParams
{
    std::atomic<uint32_t> sequence { 0 };
    Params params;

    void write(const Params& p)
    {
        sequence.fetch_add(1, std::memory_order_acq_rel);
        std::memcpy(&params, &p, sizeof p);
        sequence.fetch_add(1, std::memory_order_acq_rel);
    }
    void read(Params& p) const
    {
        uint32_t before, after;
        do
        {
            before = sequence.load(std::memory_order_acquire);
            std::memcpy(&p, &params, sizeof p);
            after = sequence.load(std::memory_order_acquire);
        } while (before != after || (before & 1u) != 0);
    }
};

SharedParams shared;
Instrument voice;
std::atomic<float> envelope { 0.0f };
/** The longest render of a block since the last report, in CPU cycles: the audio core's load on the serial monitor. */
std::atomic<uint32_t> renderCycles { 0 };
Panel module;
#if TONWERK_BENCH
twm::Bench bench;
/** Headphones straight on the module's jack, which has no volume knob: the voice on both sides, 16 dB down. */
constexpr float kBenchLevel = 0.15f;
#endif

void audioTask(void*)
{
    voice.prepare((float) hw::kSampleRate);
    Params params;
    float osc[Instrument::kControl], out[Instrument::kControl];
    for (;;)
    {
        shared.read(params);
#if ! TONWERK_BENCH
        // The gate straight from its pin each block, so a trigger is never later than 0.7 ms.
        params.gate = hw::readGate();
#endif
        const uint32_t begin = esp_cpu_get_cycle_count();
        voice.render(osc, out, Instrument::kControl, params);
        const uint32_t cycles = esp_cpu_get_cycle_count() - begin;
        renderCycles.store(std::max(renderCycles.load(std::memory_order_relaxed), cycles), std::memory_order_relaxed);
        envelope.store(voice.envelope(), std::memory_order_relaxed);
#if TONWERK_BENCH
        for (int i = 0; i < Instrument::kControl; ++i)
            out[i] *= kBenchLevel;
        hw::writeAudio(out, out, Instrument::kControl);
#else
        hw::writeAudio(osc, out, Instrument::kControl);
#endif
    }
}

/** Both buttons held while switching on: the V/Oct input learns 1 V and 3 V, the other CV inputs their 0 V. */
void calibrate()
{
    ESP_LOGI(kTag, "calibration: patch 1.000 V into V/OCT, nothing else, then press WARP");
    twm::PanelInputs in;
    float codes[twm::kCvs];
    auto waitForPress = [&](bool warp, twm::Rgb colour) {
        // Wait for both buttons to be let go, then for the one asked for.
        int blink = 0;
        do
        {
            hw::readInputs(in, codes);
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (in.warpButton || in.filterButton);
        for (;;)
        {
            hw::readInputs(in, codes);
            const bool on = (++blink / 25) % 2 == 0;
            hw::showLeds(warp && on ? colour : twm::Rgb {}, ! warp && on ? colour : twm::Rgb {});
            if (warp ? in.warpButton : in.filterButton)
                return;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    };
    auto settledCodes = [&] {
        // Half a second of readings, averaged.
        float sum[twm::kCvs] {};
        for (int i = 0; i < 50; ++i)
        {
            hw::readInputs(in, codes);
            for (int c = 0; c < twm::kCvs; ++c)
                sum[c] += codes[c] / 50.0f;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        std::memcpy(codes, sum, sizeof sum);
    };

    auto& cal = module.calibration;
    waitForPress(true, { 255, 255, 255 });
    settledCodes();
    cal.pitchCode1V = codes[twm::pitchCv];
    for (int c = 1; c < twm::kCvs; ++c)
        cal.zeroCode[c] = codes[c];
    ESP_LOGI(kTag, "1 V reads %.1f; now patch 3.000 V and press FILTER", cal.pitchCode1V);
    waitForPress(false, { 255, 255, 255 });
    settledCodes();
    cal.pitchCode3V = codes[twm::pitchCv];
    ESP_LOGI(kTag, "3 V reads %.1f", cal.pitchCode3V);

    // A wiring fault shows as two codes close together; keep the old calibration then and blink red.
    const bool plausible = cal.pitchCode1V - cal.pitchCode3V > 500.0f;
    if (plausible)
        hw::save("calibration", &cal, sizeof cal);
    else
        cal = twm::Calibration {};
    const twm::Rgb result = plausible ? twm::Rgb { 0, 255, 60 } : twm::Rgb { 255, 0, 60 };
    for (int i = 0; i < 6; ++i)
    {
        hw::showLeds(i % 2 ? twm::Rgb {} : result, i % 2 ? twm::Rgb {} : result);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void controlTask(void*)
{
    twm::PanelInputs in;
    float codes[twm::kCvs];
    Params params;
    int quietMs = 0, ledMs = 0, reportMs = 0;
    TickType_t wake = xTaskGetTickCount();
    int benchTable = -1;
    for (;;)
    {
#if TONWERK_BENCH
        bench.update(kControlMs, in, module.stored);
        if (bench.currentTable() != benchTable)
        {
            benchTable = bench.currentTable();
            ESP_LOGI(kTag, "bench: table %d, warp mode %d", benchTable + 1, (int) module.stored.warpMode);
        }
        (void) codes;
#else
        (void) benchTable;
        hw::readInputs(in, codes);
        in.cv[twm::pitchCv] = module.calibration.pitchVolts(codes[twm::pitchCv]);
        for (int c = 1; c < twm::kCvs; ++c)
            in.cv[c] = module.calibration.cvVolts(c, codes[c]);
#endif

        module.update(in, kControlMs, params);
        shared.write(params);

#if TONWERK_BENCH
        module.dirty = false;   // the bench's warp modes are not the player's: nothing is saved
#endif
        if (module.dirty)
        {
            module.dirty = false;
            quietMs = kSaveAfterMs;
        }
        else if (quietMs > 0 && (quietMs -= kControlMs) <= 0)
            hw::save(Panel::kStoreKey, &module.stored, sizeof module.stored);

        if ((ledMs += kControlMs) >= 33)
        {
            ledMs = 0;
            const float e = envelope.load(std::memory_order_relaxed);
            hw::showLeds(module.led(0, e), module.led(1, e));
        }
        if ((reportMs += kControlMs) >= 5000)
        {
            reportMs = 0;
            // A block of 32 samples at 48 kHz lasts 0.67 ms, 160000 cycles at 240 MHz.
            const uint32_t worst = renderCycles.exchange(0);
            ESP_LOGI(kTag, "audio core: worst block %lu cycles, %.1f %% load", (unsigned long) worst,
                     100.0 * worst / (240.0e6 * Instrument::kControl / hw::kSampleRate));
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kControlMs));
    }
}
} // namespace

extern "C" void app_main()
{
    hw::startStorage();
    hw::startLeds();
#if TONWERK_BENCH
    ESP_LOGI(kTag, "bench test: no pots, ADC or gate; PCM5102A on IO11 (BCK), IO12 (LCK), IO13 (DIN)");
#else
    hw::startInputs();
#endif
    hw::showLeds({ 252, 238, 10 }, { 0, 240, 255 });

#if ! TONWERK_BENCH
    Panel::Stored stored;
    if (hw::load(Panel::kStoreKey, &stored, sizeof stored) && stored.version == Panel::Stored {}.version)
        module.stored = stored;
    twm::Calibration calibration;
    if (hw::load("calibration", &calibration, sizeof calibration) && calibration.version == twm::Calibration {}.version)
        module.calibration = calibration;
#endif

#if TONWERK_GRANULAR
    // The sources' full-rate samples were flashed with the firmware into the partition "tables"; the lower copies are
    // computed from them in PSRAM at every start (well under a second).
    const int64_t start = esp_timer_get_time();
    auto* block = static_cast<int16_t*>(hw::allocateTables(twm::grain::Sources::bytes()));
    if (block == nullptr
        || ! hw::loadBlock(block, twm::grain::Sources::flashedBytes(), twm::grain::Sources::kMagic,
                           twm::grain::Sources::kVersion))
    {
        // Without its sources the firmware has nothing to play: flash the whole image (tonwerk-granular.bin at 0x0).
        ESP_LOGE(kTag, block == nullptr ? "not enough PSRAM for the sources (an N16R8 board has 8 MB)"
                                        : "no grain sources in the partition \"tables\": flash tonwerk-granular.bin at 0x0");
        for (;;)
        {
            hw::showLeds({ 255, 0, 60 }, { 255, 0, 60 });
            vTaskDelay(pdMS_TO_TICKS(500));
            hw::showLeds({}, {});
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    sources.build(block);
    twm::grain::Windows::instance();
    voice.use(&sources);
    ESP_LOGI(kTag, "grain sources ready in %lld ms", (esp_timer_get_time() - start) / 1000);
#elif ! TONWERK_PHYSICAL && ! TONWERK_PD
    // The tables live in PSRAM. Computing them takes seconds, so the first start after flashing keeps a copy in
    // flash and every later start reads that instead.
    const int64_t start = esp_timer_get_time();
    auto* block = static_cast<int16_t*>(hw::allocateTables(twm::Bank::bytes()));
    if (block == nullptr)
    {
        ESP_LOGE(kTag, "not enough PSRAM for the wavetables (an N16R8 board has 8 MB)");
        for (;;)
        {
            hw::showLeds({ 255, 0, 60 }, { 255, 0, 60 });
            vTaskDelay(pdMS_TO_TICKS(500));
            hw::showLeds({}, {});
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    twm::Bank::layout(block);
    if (hw::loadTables(block, twm::Bank::bytes(), twm::Bank::kVersion))
        ESP_LOGI(kTag, "wavetables read from flash in %lld ms", (esp_timer_get_time() - start) / 1000);
    else
    {
        hw::showLeds({ 255, 0, 60 }, { 252, 238, 10 });
        twm::Bank::compute();
        ESP_LOGI(kTag, "wavetables computed in %lld ms", (esp_timer_get_time() - start) / 1000);
        hw::storeTables(block, twm::Bank::bytes(), twm::Bank::kVersion);
        ESP_LOGI(kTag, "and kept in flash after %lld ms", (esp_timer_get_time() - start) / 1000);
    }
#endif

#if TONWERK_BENCH
    (void) calibrate;
#else
    twm::PanelInputs in;
    float codes[twm::kCvs];
    hw::readInputs(in, codes);
    if (in.warpButton && in.filterButton)
        calibrate();
#endif

    hw::startAudio();
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, configMAX_PRIORITIES - 1, nullptr, 1);
    xTaskCreatePinnedToCore(controlTask, "control", 6144, nullptr, 5, nullptr, 0);
}

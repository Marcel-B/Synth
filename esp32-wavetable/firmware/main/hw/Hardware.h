#pragma once

#include "../app/Module.h"

#include <cstddef>
#include <cstdint>

/** The module's peripherals behind small functions, so main.cpp reads as the module's flow. */
namespace hw
{
inline constexpr int kSampleRate = 48000;

/** I2S to the PCM5102A: left is the oscillator output, right the voice output. */
void startAudio();
/** Writes `count` stereo frames, blocking until the DMA has room: this paces the audio task. */
void writeAudio(const float* left, const float* right, int count);

void startInputs();
/** Reads every pot, CV, the gate and the buttons into `in`; CVs as the MCP3208's raw codes in `codes`. */
void readInputs(twm::PanelInputs& in, float codes[twm::kCvs]);
/** The gate alone, cheap enough for the audio task to read every block. */
bool readGate();

void startLeds();
void showLeds(twm::Rgb first, twm::Rgb second);

void startStorage();
bool load(const char* key, void* data, std::size_t size);
void save(const char* key, const void* data, std::size_t size);

/** PSRAM for the wavetables. */
void* allocateTables(std::size_t bytes);
/** Reads the tables' copy from the flash partition "tables"; false when it is missing or of another version. */
bool loadTables(void* block, std::size_t bytes, uint32_t version);
/** Keeps the computed tables in the flash partition "tables" (takes some 20 s, once after flashing). */
void storeTables(const void* block, std::size_t bytes, uint32_t version);
} // namespace hw

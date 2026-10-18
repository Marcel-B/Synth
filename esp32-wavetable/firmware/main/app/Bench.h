#pragma once

#include "Module.h"

#include <cmath>

namespace twm
{
/**
 * The bench test (build with -DTONWERK_BENCH=1): the DevKit and the PCM5102A module alone on a breadboard, no pots,
 * no ADC, no gate. Plays the panel in place of a player: an arpeggio at 120 BPM, the wavetable position sweeping, a
 * new table every four seconds and, after all ten, the next warp mode. Plain C++ like Module, so it needs no
 * hardware.
 */
class Bench
{
public:
    /** One control tick of `ms` milliseconds: the inputs a player would give, and the warp mode for this round. */
    void update(int ms, PanelInputs& in, Stored& stored)
    {
        timeMs += ms;
        const int step = timeMs / kStepMs;
        const int round = step / (kStepsPerTable * kTables);

        in = PanelInputs {};
        in.pot[tune] = 0.5f;
        in.pot[fine] = 0.5f;
        in.pot[table] = (float) ((step / kStepsPerTable) % kTables) / (float) (kTables - 1);
        // The position goes up and back down in eight seconds.
        const float phase = std::fmod((float) timeMs / 8000.0f, 1.0f);
        in.pot[position] = phase < 0.5f ? 2.0f * phase : 2.0f - 2.0f * phase;
        in.pot[warp] = round % (int) Warp::count == 0 ? 0.0f : 0.5f;
        in.pot[cutoff] = 0.75f;          // about 3.6 kHz
        in.pot[resonance] = 0.15f;
        in.pot[decay] = 0.45f;           // a release of about 0.1 s
        in.pot[positionAmount] = 0.5f;   // attenuverters at zero
        in.pot[cutoffAmount] = 0.5f;

        static constexpr float kNotes[] = { 0, 7, 12, 15, 19, 15, 12, 7 };
        in.cv[pitchCv] = 1.0f + kNotes[step % 8] / 12.0f;   // 1 V is C3
        in.gate = timeMs % kStepMs < kGateMs;

        stored.warpMode = (Warp) (round % (int) Warp::count);
    }

    int currentTable() const { return (timeMs / kStepMs / kStepsPerTable) % kTables; }

private:
    static constexpr int kStepMs = 250;   // eighths at 120 BPM
    static constexpr int kGateMs = 180;
    static constexpr int kStepsPerTable = 16;
    int timeMs = 0;
};
} // namespace twm

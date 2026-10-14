#include "ChorusPresets.h"

#include "ChorusEngine.h"

namespace tonwerkchorus
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float ensemble = 1, flanger = 2, phaser = 3;
constexpr float triangle = 1;
constexpr float oneVoice = 0, threeVoices = 2, fourVoices = 3;
constexpr float sixStages = 1, eightStages = 2, twelveStages = 3;
constexpr float twoBars = kTwoBars, whole = kWhole, quarter = kQuarter, eighth = 10;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // Three voices, a little slower and deeper than Init: the chorus pedal on a clean guitar.
        { "Klassischer Chorus", "Chorus",
          { { "voices", threeVoices }, { "rate", 0.6f }, { "depth", 0.55f }, { "delay", 14 }, { "output", -3 } } },
        // Four voices, a quarter turn apart on each side: wide and thick, for pads and clean guitars.
        { "Breiter Chorus", "Chorus",
          { { "voices", fourVoices }, { "rate", 0.5f }, { "depth", 0.6f }, { "delay", 15 }, { "mix", 0.5f },
            { "output", -4 } } },
        // Slow and shallow, the two sides opposite: more space than movement, in the manner of a rack chorus.
        { "Dimension", "Chorus",
          { { "rate", 0.3f }, { "depth", 0.25f }, { "spread", 180 }, { "delay", 8 }, { "mix", 0.45f },
            { "output", -3 } } },
        // Only the moved signal: the pitch wobbles, nothing doubles.
        { "Vibrato", "Chorus",
          { { "voices", oneVoice }, { "rate", 5.5f }, { "depth", 0.35f }, { "spread", 0 }, { "delay", 6 },
            { "lowCut", 20 }, { "mix", 1.0f }, { "output", 0 } } },
        { "Achtziger-Chorus", "Chorus",
          { { "shape", triangle }, { "voices", threeVoices }, { "rate", 0.9f }, { "depth", 0.7f }, { "delay", 10 },
            { "feedback", 0.2f }, { "highCut", 9000 }, { "mix", 0.5f }, { "output", -2 } } },

        // The string machines' shimmer: three lines, slow and fast at once.
        { "String-Ensemble", "Ensemble",
          { { "mode", ensemble }, { "rate", 0.6f }, { "depth", 0.7f }, { "delay", 8 }, { "spread", 60 },
            { "mix", 0.55f }, { "output", -4 } } },
        { "Sanftes Ensemble", "Ensemble",
          { { "mode", ensemble }, { "rate", 0.35f }, { "depth", 0.4f }, { "delay", 10 }, { "spread", 90 },
            { "highCut", 8000 }, { "mix", 0.45f }, { "output", -3 } } },
        { "Orgel-Leslie", "Ensemble",
          { { "mode", ensemble }, { "rate", 0.9f }, { "depth", 0.9f }, { "delay", 4 }, { "spread", 120 },
            { "mix", 0.5f }, { "output", -3 } } },

        // Two bars down and up, much feedback: the jet plane.
        { "Jet-Flanger", "Flanger",
          { { "mode", flanger }, { "sync", 1 }, { "division", twoBars }, { "depth", 0.95f }, { "delay", 6 },
            { "feedback", 0.75f }, { "mix", 0.5f }, { "output", -5 } } },
        // Negative feedback: odd teeth only, hollow and metallic.
        { "Metall-Flanger", "Flanger",
          { { "mode", flanger }, { "rate", 0.25f }, { "depth", 0.8f }, { "delay", 3 }, { "feedback", -0.8f },
            { "mix", 0.5f }, { "output", -6 } } },
        { "Sanfter Flanger", "Flanger",
          { { "mode", flanger }, { "shape", triangle }, { "rate", 0.15f }, { "depth", 0.7f }, { "delay", 4 },
            { "feedback", 0.3f }, { "mix", 0.45f }, { "output", -3 } } },
        { "Schneller Flanger", "Flanger",
          { { "mode", flanger }, { "sync", 1 }, { "division", eighth }, { "depth", 0.6f }, { "delay", 2.5f },
            { "feedback", 0.5f }, { "mix", 0.5f }, { "output", -3 } } },

        // Four stages, the seventies' little box: two notches wandering.
        { "Klassischer Phaser", "Phaser",
          { { "mode", phaser }, { "rate", 0.4f }, { "depth", 0.8f }, { "frequency", 700 }, { "feedback", 0.3f },
            { "mix", 0.5f }, { "output", -3 } } },
        { "Tiefer Phaser", "Phaser",
          { { "mode", phaser }, { "rate", 0.2f }, { "depth", 0.9f }, { "frequency", 500 }, { "stages", twelveStages },
            { "feedback", 0.6f }, { "mix", 0.5f }, { "output", -5 } } },
        // In the tempo, a quarter each: the phaser pumps with the beat.
        { "Sync-Phaser", "Phaser",
          { { "mode", phaser }, { "sync", 1 }, { "division", quarter }, { "depth", 0.8f }, { "frequency", 900 },
            { "stages", sixStages }, { "feedback", 0.5f }, { "mix", 0.5f }, { "output", -4 } } },
        { "Phaser-Wellen", "Phaser",
          { { "mode", phaser }, { "sync", 1 }, { "division", whole }, { "depth", 1.0f }, { "frequency", 1200 },
            { "stages", eightStages }, { "feedback", -0.6f }, { "spread", 180 }, { "mix", 0.5f }, { "output", -4 } } },
    };
    return presets;
}
} // namespace tonwerkchorus

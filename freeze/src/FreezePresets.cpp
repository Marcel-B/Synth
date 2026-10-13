#include "FreezePresets.h"

namespace tonwerkfreeze
{
const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Menus as the index of their entry (trigger: Manuell, Tempo; interval: 1/4, 1/2, 1 Takt, 2 Takte, 4 Takte),
    // percent as 0 to 1, times in ms (drift in s), pitch in semitones. The levels match each frozen layer to its source.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },
        { "Endlose Fläche", "Flächen", { { "capture", 800 }, { "fade", 2000 }, { "blur", 0.15f }, { "width", 0.8f } } },
        { "Warmes Pad", "Flächen", { { "capture", 600 }, { "fade", 3000 }, { "lowCut", 80 }, { "highCut", 5000 }, { "blur", 0.25f } } },
        { "Dunkle Wolke", "Flächen", { { "pitch", -12 }, { "highCut", 3000 }, { "motion", 0.45f }, { "fade", 2500 }, { "level", 2 } } },
        // Little motion and coherent phases: the frozen chord stands still as glass.
        { "Glas", "Flächen", { { "capture", 150 }, { "motion", 0.1f }, { "blur", 0.0f }, { "width", 0.4f } } },

        { "Schimmer", "Schimmer", { { "shimmer", 0.5f }, { "lowCut", 150 }, { "fade", 2500 }, { "blur", 0.2f } } },
        { "Oktave höher", "Schimmer", { { "pitch", 12 }, { "lowCut", 200 }, { "blur", 0.15f } } },
        { "Engelschor", "Schimmer", { { "pitch", 12 }, { "shimmer", 0.6f }, { "blur", 0.4f }, { "width", 1.0f }, { "lowCut", 300 }, { "fade", 3000 }, { "level", -1 } } },

        { "Atmend", "Bewegt", { { "motion", 0.8f }, { "drift", 3 }, { "fade", 1500 }, { "level", -2 } } },
        { "Langsame Drift", "Bewegt", { { "motion", 0.6f }, { "drift", 20 }, { "fade", 4000 }, { "width", 0.9f } } },
        // Fully random phases every frame: Paulstretch's endless wash.
        { "Paulstretch", "Bewegt", { { "blur", 1.0f }, { "capture", 1500 }, { "fade", 4000 }, { "width", 1.0f }, { "level", 2 } } },

        // Freezes anew on every grid line while the song plays; switch the freeze on to start.
        { "Jeder Takt", "Rhythmisch", { { "trigger", 1 }, { "interval", 2 }, { "fade", 150 }, { "capture", 200 } } },
        { "Viertel-Stapel", "Rhythmisch", { { "trigger", 1 }, { "interval", 0 }, { "stack", 0.7f }, { "fade", 100 }, { "capture", 100 } } },
        { "Morph alle 2 Takte", "Rhythmisch", { { "trigger", 1 }, { "interval", 3 }, { "fade", 2000 }, { "capture", 500 } } },

        // The dry gives way while frozen: only the pad is heard.
        { "Nur Freeze", "Klangeffekte", { { "mix", 1.0f }, { "fade", 300 } } },
        { "Unterwasser", "Klangeffekte", { { "highCut", 1200 }, { "pitch", -5 }, { "blur", 0.5f }, { "motion", 0.6f } } },
        { "Geisterstimme", "Klangeffekte", { { "pitch", 7 }, { "blur", 0.6f }, { "lowCut", 400 }, { "width", 1.0f }, { "motion", 0.5f } } },
    };
    return presets;
}
} // namespace tonwerkfreeze

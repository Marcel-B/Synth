#include "RigProcessor.h"

namespace tonwerkrig
{
const std::vector<RigPreset>& factoryRigs()
{
    // Each names its plugins' own factory sounds, so a rig changes when they are retuned; RigTests holds every name to
    // a sound that exists. Sources 1 to 8 are the groovebox's tracks: 1 Bassdrum, 2 Snare, 3 Clap.
    static const std::vector<RigPreset> rigs {
        { "Init", "Leads", "analog", "", {} },
        { "Techno-Rumble", "Drums", "groovebox", "Berlin Techno",
          { { "rumble", 1, "Klassisch" }, { "glue", 0, "Mix-Kleber" } } },
        { "Industrie-Halle", "Drums", "groovebox", "Industrial",
          { { "rumble", 1, "Industrie" }, { "distortion", 2, "Drum-Dreck" }, { "tape", 0, "Heißes Band" } } },
        { "House mit Raum", "Drums", "groovebox", "Deep House",
          { { "shimmer", 3, "Heller Raum" }, { "delay", 2, "Punktierte Achtel" }, { "glue", 0, "Sanfter Bus" } } },
        { "Acid-Kette", "Bass", "bass", "Acid Klassik",
          { { "distortion", 0, "Röhren-Bass" }, { "delay", 0, "Punktierte Achtel" } } },
        { "Neuro-Wand", "Bass", "neuro", "Neuro-Growl",
          { { "distortion", 0, "Bass-Knurren" }, { "glue", 0, "Dichter Bus" } } },
        { "Engelsfläche", "Flächen", "wavetable", "Dunkle Fläche",
          { { "chorus", 0, "String-Ensemble" }, { "shimmer", 0, "Engelschor" } } },
        { "Kassetten-Tasten", "Flächen", "expander", "Poly-Tasten",
          { { "delay", 0, "Bandecho" }, { "tape", 0, "Walkman" } } },
        { "Supersaw-Weite", "Leads", "wavetable", "Supersaw",
          { { "chorus", 0, "Breiter Chorus" }, { "delay", 0, "Achtel Ping-Pong" }, { "shimmer", 0, "Großer Hall" } } },
        { "Dub-Lead", "Leads", "bass", "Acid-Lead", { { "tape", 0, "Heißes Band" }, { "delay", 0, "Dub-Echo" } } },
    };
    return rigs;
}
} // namespace tonwerkrig

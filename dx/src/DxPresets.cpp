#include "DxPresets.h"

namespace tonwerkdx
{
namespace
{
using Stages = std::array<int, 4>;

OperatorPatch op(int coarse, int level, Stages rates, Stages levels, int velocity = 0, int detune = 0, int fine = 0,
                 int rateScaling = 2)
{
    OperatorPatch o;
    o.coarse = coarse;
    o.fine = fine;
    o.level = level;
    o.rates = rates;
    o.levels = levels;
    o.velocity = velocity;
    o.detune = detune;
    o.rateScaling = rateScaling;
    return o;
}

OperatorPatch off()
{
    OperatorPatch o;
    o.on = false;
    o.level = 0;
    return o;
}

/** A gentle vibrato that comes in after the attack. */
void vibrato(DxPatch& p, int depth, int delay = 50)
{
    p.lfoWave = LfoWave::sine;
    p.lfoSpeed = 34;
    p.lfoDelay = delay;
    p.lfoPitchDepth = depth;
    p.lfoPitchSens = 3;
}

NamedDxPatch electricPiano()
{
    DxPatch p;
    p.algorithm = 4; // 5: three pairs
    p.feedback = 6;
    // The body: two carriers a few cents apart, the tine a quick 14th partial on top.
    p.ops[0] = op(1, 99, { 96, 25, 25, 67 }, { 99, 75, 0, 0 }, 2, 0, 0, 3);
    p.ops[1] = op(14, 70, { 95, 50, 35, 78 }, { 99, 0, 0, 0 }, 7, 0, 0, 3);
    p.ops[2] = op(1, 99, { 95, 20, 20, 50 }, { 99, 95, 0, 0 }, 2, 3);
    p.ops[3] = op(1, 82, { 95, 29, 20, 50 }, { 99, 95, 0, 0 }, 6);
    p.ops[4] = op(1, 84, { 95, 20, 20, 50 }, { 99, 95, 0, 0 }, 2, -3);
    p.ops[5] = op(1, 80, { 95, 29, 20, 50 }, { 99, 95, 0, 0 }, 6);
    p.fx.reverbMix = 0.2f;
    p.volume = 0.6f;
    return { "E-Piano", p };
}

NamedDxPatch bass()
{
    DxPatch p;
    p.algorithm = 15; // 16: one carrier, three branches
    p.feedback = 6;
    p.transpose = -12;
    p.ops[0] = op(1, 99, { 99, 40, 30, 70 }, { 99, 85, 75, 0 }, 3);
    p.ops[1] = op(1, 86, { 99, 55, 40, 70 }, { 99, 65, 50, 0 }, 5);
    p.ops[2] = op(3, 80, { 99, 70, 50, 70 }, { 99, 40, 0, 0 }, 6);
    p.ops[3] = op(1, 70, { 99, 60, 40, 70 }, { 99, 60, 40, 0 });
    p.ops[4] = op(0, 78, { 99, 50, 40, 70 }, { 99, 70, 60, 0 });
    p.ops[5] = op(1, 72, { 99, 60, 40, 70 }, { 99, 50, 40, 0 });
    p.volume = 0.7f;
    return { "Bass", p };
}

NamedDxPatch brass()
{
    DxPatch p;
    p.algorithm = 21; // 22: operator 6 drives three carriers
    p.feedback = 7;
    // A slow swell on the modulators makes the brass open up after the note starts.
    p.ops[0] = op(1, 99, { 72, 76, 99, 71 }, { 99, 88, 96, 0 }, 1);
    p.ops[1] = op(1, 84, { 62, 51, 29, 71 }, { 82, 95, 96, 0 }, 4);
    p.ops[2] = op(1, 95, { 72, 76, 99, 71 }, { 99, 88, 96, 0 }, 1, 2);
    p.ops[3] = op(1, 95, { 72, 76, 99, 71 }, { 99, 88, 96, 0 }, 1, -2);
    p.ops[4] = op(1, 90, { 72, 76, 99, 71 }, { 99, 88, 96, 0 }, 1, 4);
    p.ops[5] = op(1, 80, { 55, 40, 40, 70 }, { 99, 92, 88, 0 }, 4);
    vibrato(p, 4, 60);
    p.fx.reverbMix = 0.18f;
    p.volume = 0.5f;
    return { "Blech", p };
}

NamedDxPatch tubularBell()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 3;
    // A modulator at 3.5 times the note gives the bell its clang; the carriers ring for seconds.
    p.ops[0] = op(1, 99, { 98, 22, 25, 30 }, { 99, 70, 0, 0 }, 2, 0, 0, 2);
    p.ops[1] = op(3, 80, { 98, 25, 25, 30 }, { 99, 60, 0, 0 }, 4, 0, 50);
    p.ops[2] = op(1, 90, { 98, 20, 25, 30 }, { 99, 70, 0, 0 }, 2, 4);
    p.ops[3] = op(3, 74, { 98, 28, 25, 30 }, { 99, 50, 0, 0 }, 4, 0, 50);
    p.ops[4] = op(2, 80, { 98, 30, 25, 30 }, { 99, 50, 0, 0 }, 2, -4);
    p.ops[5] = op(5, 70, { 98, 35, 25, 30 }, { 99, 40, 0, 0 }, 4, 0, 30);
    p.fx.reverbMix = 0.3f;
    p.fx.reverbDecay = 3.5f;
    p.volume = 0.6f;
    return { "Röhrenglocke", p };
}

NamedDxPatch marimba()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    p.ops[0] = op(1, 99, { 99, 32, 35, 55 }, { 99, 0, 0, 0 }, 3, 0, 0, 4);
    p.ops[1] = op(4, 74, { 99, 70, 50, 70 }, { 99, 0, 0, 0 }, 5, 0, 0, 4);
    // The mallet: a high, very short pair.
    p.ops[2] = op(1, 80, { 99, 45, 40, 60 }, { 99, 0, 0, 0 }, 4, 2, 0, 4);
    p.ops[3] = op(10, 72, { 99, 85, 60, 80 }, { 99, 0, 0, 0 }, 6, 0, 0, 4);
    p.ops[4] = off();
    p.ops[5] = off();
    p.fx.reverbMix = 0.22f;
    return { "Marimba", p };
}

NamedDxPatch harmonica()
{
    DxPatch p;
    p.algorithm = 0; // 1: a pair and a chain of four
    p.feedback = 5;
    p.ops[0] = op(1, 99, { 80, 40, 50, 70 }, { 99, 95, 90, 0 }, 3);
    p.ops[1] = op(1, 82, { 78, 40, 50, 70 }, { 99, 92, 88, 0 }, 5);
    p.ops[2] = op(2, 78, { 80, 40, 50, 70 }, { 99, 95, 90, 0 }, 3);
    p.ops[3] = op(1, 78, { 78, 40, 50, 70 }, { 99, 90, 85, 0 }, 4);
    p.ops[4] = op(3, 70, { 78, 40, 50, 70 }, { 99, 90, 85, 0 });
    p.ops[5] = op(1, 66, { 78, 40, 50, 70 }, { 99, 90, 85, 0 });
    vibrato(p, 10, 45);
    return { "Mundharmonika", p };
}

NamedDxPatch clavi()
{
    DxPatch p;
    p.algorithm = 2; // 3: two chains of three
    p.feedback = 5;
    p.ops[0] = op(1, 99, { 99, 55, 30, 80 }, { 99, 88, 0, 0 }, 3, 0, 0, 3);
    p.ops[1] = op(3, 82, { 99, 65, 40, 80 }, { 99, 60, 0, 0 }, 5, 0, 0, 3);
    p.ops[2] = op(5, 74, { 99, 75, 40, 80 }, { 99, 40, 0, 0 }, 6, 0, 0, 3);
    p.ops[3] = op(2, 90, { 99, 55, 30, 80 }, { 99, 88, 0, 0 }, 3, 2, 0, 3);
    p.ops[4] = op(1, 80, { 99, 65, 40, 80 }, { 99, 60, 0, 0 }, 5, 0, 0, 3);
    p.ops[5] = op(7, 70, { 99, 80, 40, 80 }, { 99, 30, 0, 0 }, 6, 0, 0, 3);
    return { "Clavi", p };
}

NamedDxPatch pad()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 4;
    // Three detuned pairs fading in slowly; their modulators open even later.
    p.ops[0] = op(1, 99, { 40, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, 0, 0, 1);
    p.ops[1] = op(1, 76, { 30, 25, 30, 40 }, { 99, 90, 85, 0 }, 2, 0, 0, 1);
    p.ops[2] = op(1, 96, { 40, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, 4, 0, 1);
    p.ops[3] = op(2, 70, { 30, 25, 30, 40 }, { 99, 85, 80, 0 }, 2, 0, 0, 1);
    p.ops[4] = op(1, 96, { 40, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, -4, 0, 1);
    p.ops[5] = op(3, 64, { 28, 25, 30, 40 }, { 99, 85, 80, 0 }, 2, 0, 0, 1);
    p.lfoWave = LfoWave::triangle;
    p.lfoSpeed = 28;
    p.lfoAmpDepth = 20;
    p.ops[0].ampModSens = 1;
    p.ops[2].ampModSens = 1;
    p.ops[4].ampModSens = 1;
    p.fx.reverbMix = 0.35f;
    p.fx.reverbDecay = 4.0f;
    p.fx.delayMix = 0.18f;
    p.fx.delaySync = true;
    return { "Fläche", p };
}

NamedDxPatch organ()
{
    DxPatch p;
    p.algorithm = 31; // 32: six sines, like drawbars
    p.feedback = 3;
    const Stages rates { 99, 99, 99, 85 };
    const Stages levels { 99, 99, 99, 0 };
    p.ops[0] = op(0, 90, rates, levels);
    p.ops[1] = op(1, 99, rates, levels);
    p.ops[2] = op(2, 92, rates, levels);
    p.ops[3] = op(3, 84, rates, levels);
    p.ops[4] = op(4, 80, rates, levels);
    p.ops[5] = op(8, 74, rates, levels);
    vibrato(p, 6, 0);
    p.lfoSpeed = 40;
    p.fx.reverbMix = 0.2f;
    p.volume = 0.5f;
    return { "Orgel", p };
}

NamedDxPatch flute()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 7;
    p.ops[0] = op(1, 99, { 75, 50, 50, 70 }, { 99, 96, 94, 0 }, 3);
    p.ops[1] = op(1, 66, { 70, 50, 40, 70 }, { 99, 85, 80, 0 }, 4);
    p.ops[2] = op(2, 64, { 75, 50, 50, 70 }, { 99, 90, 88, 0 }, 3);
    p.ops[3] = off();
    // Breath: a quiet carrier roughened by operator 6 at full feedback, mostly at the start of the note.
    p.ops[4] = op(1, 60, { 85, 55, 50, 70 }, { 99, 60, 50, 0 }, 4);
    p.ops[5] = op(13, 80, { 90, 60, 40, 70 }, { 99, 50, 40, 0 }, 4);
    vibrato(p, 8, 60);
    p.fx.reverbMix = 0.25f;
    return { "Flöte", p };
}

NamedDxPatch lead()
{
    DxPatch p;
    p.algorithm = 1; // 2: feedback on operator 2 turns the first pair into a sawtooth
    p.feedback = 7;
    p.ops[0] = op(1, 99, { 99, 50, 50, 70 }, { 99, 95, 90, 0 }, 2);
    p.ops[1] = op(1, 86, { 99, 50, 50, 70 }, { 99, 95, 90, 0 }, 3);
    p.ops[2] = op(1, 95, { 99, 50, 50, 70 }, { 99, 95, 90, 0 }, 2, 5);
    p.ops[3] = op(1, 80, { 99, 50, 50, 70 }, { 99, 90, 85, 0 }, 3);
    p.ops[4] = op(2, 72, { 99, 50, 50, 70 }, { 99, 85, 80, 0 }, 3);
    p.ops[5] = op(3, 64, { 99, 50, 50, 70 }, { 99, 80, 75, 0 }, 3);
    vibrato(p, 8, 55);
    p.fx.delayMix = 0.22f;
    p.fx.delaySync = true;
    p.fx.reverbMix = 0.15f;
    return { "Lead", p };
}
} // namespace

const std::vector<NamedDxPatch>& factoryPresets()
{
    static const std::vector<NamedDxPatch> presets { electricPiano(), bass(), brass(),     tubularBell(),
                                                     marimba(),       harmonica(), clavi(), pad(),
                                                     organ(),         flute(),     lead() };
    return presets;
}
} // namespace tonwerkdx

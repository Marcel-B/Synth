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

/** A gentle vibrato that comes in after the attack: speed 66 is about 5.3 Hz (`lfoHz`). */
void vibrato(DxPatch& p, int depth, int delay = 50)
{
    p.lfoWave = LfoWave::sine;
    p.lfoSpeed = 66;
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
    p.fx.reverbMix = 0.04f;
    p.volume = 0.31f;
    return { "E-Piano", p, "Tasten" };
}

NamedDxPatch bass()
{
    DxPatch p;
    p.algorithm = 15; // 16: one carrier, three branches
    p.feedback = 6;
    p.transpose = -12;
    p.ops[0] = op(1, 99, { 99, 40, 30, 70 }, { 99, 95, 90, 0 }, 1);
    p.ops[1] = op(1, 86, { 99, 55, 40, 70 }, { 99, 65, 50, 0 }, 5);
    p.ops[2] = op(3, 80, { 99, 70, 50, 70 }, { 99, 40, 0, 0 }, 6);
    p.ops[3] = op(1, 70, { 99, 60, 40, 70 }, { 99, 60, 40, 0 });
    p.ops[4] = op(0, 74, { 99, 50, 40, 70 }, { 99, 70, 60, 0 });
    p.ops[5] = op(1, 72, { 99, 60, 40, 70 }, { 99, 50, 40, 0 });
    p.volume = 0.69f;
    return { "Bass", p, "Bässe" };
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
    p.fx.reverbMix = 0.04f;
    p.volume = 0.3f;
    return { "Blech", p, "Bläser & Streicher" };
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
    p.fx.reverbMix = 0.05f;
    p.fx.reverbDecay = 3.5f;
    p.volume = 0.48f;
    return { "Röhrenglocke", p, "Glocken" };
}

NamedDxPatch marimba()
{
    DxPatch p;
    // Algorithm 1 with operators 5 and 6 off: two pairs, and the output is shared by two carriers, not three.
    p.algorithm = 0;
    p.feedback = 0;
    p.ops[0] = op(1, 99, { 99, 30, 35, 55 }, { 99, 0, 0, 0 }, 1, 0, 0, 4);
    p.ops[1] = op(4, 74, { 99, 70, 50, 70 }, { 99, 0, 0, 0 }, 5, 0, 0, 4);
    // The mallet: a high, very short pair.
    p.ops[2] = op(1, 80, { 99, 45, 40, 60 }, { 99, 0, 0, 0 }, 4, 2, 0, 4);
    p.ops[3] = op(10, 72, { 99, 85, 60, 80 }, { 99, 0, 0, 0 }, 6, 0, 0, 4);
    p.ops[4] = off();
    p.ops[5] = off();
    p.fx.reverbMix = 0.04f;
    p.volume = 0.77f;
    return { "Marimba", p, "Glocken" };
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
    p.volume = 0.83f;
    return { "Mundharmonika", p, "Bläser & Streicher" };
}

NamedDxPatch clavi()
{
    DxPatch p;
    p.algorithm = 2; // 3: two chains of three
    p.feedback = 5;
    p.ops[0] = op(1, 99, { 99, 55, 30, 80 }, { 99, 93, 0, 0 }, 1, 0, 0, 3);
    p.ops[1] = op(3, 82, { 99, 65, 40, 80 }, { 99, 60, 0, 0 }, 5, 0, 0, 3);
    p.ops[2] = op(5, 74, { 99, 75, 40, 80 }, { 99, 40, 0, 0 }, 6, 0, 0, 3);
    p.ops[3] = op(2, 90, { 99, 55, 30, 80 }, { 99, 93, 0, 0 }, 1, 2, 0, 3);
    p.ops[4] = op(1, 80, { 99, 65, 40, 80 }, { 99, 60, 0, 0 }, 5, 0, 0, 3);
    p.ops[5] = op(7, 70, { 99, 80, 40, 80 }, { 99, 30, 0, 0 }, 6, 0, 0, 3);
    p.volume = 0.81f;
    return { "Clavi", p, "Tasten" };
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
    p.fx.reverbMix = 0.07f;
    p.fx.reverbDecay = 4.0f;
    p.fx.delayMix = 0.12f;
    p.fx.delaySync = true;
    p.volume = 0.26f;
    return { "Fläche", p, "Flächen" };
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
    p.fx.reverbMix = 0.04f;
    p.volume = 0.3f;
    return { "Orgel", p, "Tasten" };
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
    p.fx.reverbMix = 0.04f;
    p.volume = 0.65f;
    return { "Flöte", p, "Bläser & Streicher" };
}

NamedDxPatch lead()
{
    DxPatch p;
    p.algorithm = 1; // 2: feedback on operator 2 turns the first pair into a sawtooth
    p.feedback = 7;
    p.ops[0] = op(1, 99, { 99, 50, 50, 70 }, { 99, 97, 95, 0 }, 2);
    p.ops[1] = op(1, 86, { 99, 50, 50, 70 }, { 99, 95, 90, 0 }, 3);
    p.ops[2] = op(1, 95, { 99, 50, 50, 70 }, { 99, 97, 95, 0 }, 2, 5);
    p.ops[3] = op(1, 80, { 99, 50, 50, 70 }, { 99, 90, 85, 0 }, 3);
    p.ops[4] = op(2, 72, { 99, 50, 50, 70 }, { 99, 85, 80, 0 }, 3);
    p.ops[5] = op(3, 64, { 99, 50, 50, 70 }, { 99, 80, 75, 0 }, 3);
    vibrato(p, 8, 55);
    p.fx.delayMix = 0.18f;
    p.fx.delaySync = true;
    p.fx.reverbMix = 0.03f;
    p.volume = 0.66f;
    return { "Lead", p, "Leads" };
}

// Since 0.4.0. A modulator at level 91 bends its carrier by about 6 radians, 83 by 3, 75 by 1.5: 8 steps halve it.

NamedDxPatch brightPiano()
{
    DxPatch p;
    p.algorithm = 4; // 5: three pairs
    p.feedback = 0;
    // A harder strike than the E-Piano: the first pair barks with the velocity, the second rings a twelfth above.
    p.ops[0] = op(1, 99, { 95, 30, 25, 60 }, { 99, 80, 0, 0 }, 2, 0, 0, 3);
    p.ops[1] = op(1, 80, { 95, 45, 30, 70 }, { 99, 60, 0, 0 }, 6, 0, 0, 3);
    p.ops[2] = op(1, 95, { 95, 28, 25, 60 }, { 99, 82, 0, 0 }, 2, 4, 0, 3);
    p.ops[3] = op(7, 62, { 98, 60, 40, 70 }, { 99, 0, 0, 0 }, 5, 0, 0, 3);
    p.ops[4] = op(4, 72, { 98, 40, 30, 60 }, { 99, 40, 0, 0 }, 3, -3, 0, 3);
    p.ops[5] = op(1, 62, { 95, 50, 40, 60 }, { 99, 50, 0, 0 }, 4, 0, 0, 3);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.48f;
    return { "E-Piano Hell", p, "Tasten" };
}

NamedDxPatch harpsichord()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 6;
    // Plucked by a quill: bright, the same at any velocity, and shorter on the high notes.
    p.ops[0] = op(1, 99, { 99, 35, 30, 55 }, { 99, 70, 0, 0 }, 0, 0, 0, 4);
    p.ops[1] = op(3, 84, { 99, 40, 30, 60 }, { 99, 60, 0, 0 }, 0, 0, 0, 4);
    p.ops[2] = op(2, 90, { 99, 38, 30, 55 }, { 99, 65, 0, 0 }, 0, 0, 0, 4);
    p.ops[3] = op(5, 80, { 99, 45, 35, 60 }, { 99, 55, 0, 0 }, 0, 0, 0, 4);
    p.ops[4] = op(1, 86, { 99, 36, 30, 55 }, { 99, 68, 0, 0 }, 0, -3, 0, 4);
    p.ops[5] = op(1, 82, { 99, 40, 30, 60 }, { 99, 60, 0, 0 }, 0, 0, 0, 4);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.55f;
    return { "Cembalo", p, "Tasten" };
}

NamedDxPatch accordion()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 5;
    // Two reeds a few cents apart beat against each other, the third sounds an octave up.
    const Stages rates { 80, 50, 50, 70 };
    const Stages levels { 99, 96, 96, 0 };
    p.ops[0] = op(1, 99, rates, levels, 2);
    p.ops[1] = op(1, 82, rates, levels, 3);
    p.ops[2] = op(1, 97, rates, levels, 2, 7);
    p.ops[3] = op(1, 82, rates, levels, 3);
    p.ops[4] = op(2, 85, rates, levels, 2, -5);
    p.ops[5] = op(1, 80, rates, levels, 3);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.43f;
    return { "Akkordeon", p, "Bläser & Streicher" };
}

NamedDxPatch slapBass()
{
    DxPatch p;
    p.algorithm = 16; // 17: one carrier, operator 2 feeding back
    p.feedback = 6;
    p.transpose = -12;
    // The snap: modulators bright for a moment, harder the harder the note; operator 5 at half the note adds the sub.
    p.ops[0] = op(1, 99, { 99, 45, 30, 70 }, { 99, 95, 90, 0 }, 1);
    p.ops[1] = op(1, 86, { 99, 65, 40, 70 }, { 99, 62, 52, 0 }, 6);
    p.ops[2] = op(3, 76, { 99, 80, 50, 70 }, { 99, 0, 0, 0 }, 6);
    p.ops[3] = op(5, 65, { 99, 85, 50, 70 }, { 99, 0, 0, 0 }, 4);
    p.ops[4] = op(0, 62, { 99, 50, 40, 70 }, { 99, 80, 75, 0 }, 2);
    p.ops[5] = off();
    p.volume = 0.8f;
    return { "Slapbass", p, "Bässe" };
}

NamedDxPatch woodBass()
{
    DxPatch p;
    p.algorithm = 15; // 16: one carrier, three branches
    p.feedback = 0;
    p.transpose = -12;
    // A round body, a modulator at half the note for the sub octave, and the finger on the string: a high, very short
    // branch.
    p.ops[0] = op(1, 99, { 90, 35, 30, 65 }, { 99, 94, 89, 0 }, 1);
    p.ops[1] = op(1, 72, { 99, 55, 40, 65 }, { 99, 55, 45, 0 }, 5);
    p.ops[2] = op(0, 64, { 99, 45, 35, 65 }, { 99, 75, 65, 0 }, 2);
    p.ops[3] = op(1, 50, { 99, 60, 40, 65 }, { 99, 40, 0, 0 });
    p.ops[4] = op(13, 62, { 99, 80, 50, 70 }, { 99, 0, 0, 0 }, 4);
    p.ops[5] = op(1, 50, { 99, 80, 50, 70 }, { 99, 0, 0, 0 });
    p.volume = 0.7f;
    return { "Holzbass", p, "Bässe" };
}

NamedDxPatch synthBass()
{
    DxPatch p;
    p.algorithm = 16; // 17: one carrier, operator 2 feeding back, so the pair is close to a sawtooth
    p.feedback = 7;
    p.transpose = -12;
    // The sawtooth closes after the attack like a filter, over a sub octave from operator 3 at half the note.
    p.ops[0] = op(1, 99, { 99, 50, 40, 75 }, { 99, 95, 92, 0 }, 1);
    p.ops[1] = op(1, 88, { 99, 55, 35, 75 }, { 99, 74, 64, 0 }, 4);
    p.ops[2] = op(0, 62, { 99, 50, 40, 75 }, { 99, 85, 80, 0 }, 1);
    p.ops[3] = op(1, 45, { 99, 60, 40, 75 }, { 99, 60, 50, 0 });
    p.ops[4] = off();
    p.ops[5] = off();
    p.volume = 0.86f;
    return { "Synthbass", p, "Bässe" };
}

NamedDxPatch vibraphone()
{
    DxPatch p;
    p.algorithm = 0; // 1 with operators 5 and 6 off: two pairs
    p.feedback = 0;
    // The bar and its fourth partial, each struck by a short mallet, and the motor's tremolo on both.
    p.ops[0] = op(1, 99, { 99, 26, 25, 40 }, { 99, 60, 0, 0 }, 1);
    p.ops[1] = op(4, 62, { 99, 55, 40, 60 }, { 99, 0, 0, 0 }, 5);
    p.ops[2] = op(4, 80, { 99, 30, 25, 40 }, { 99, 50, 0, 0 }, 1);
    p.ops[3] = op(1, 58, { 99, 60, 40, 60 }, { 99, 0, 0, 0 }, 4);
    p.ops[4] = off();
    p.ops[5] = off();
    p.ops[0].ampModSens = 2;
    p.ops[2].ampModSens = 2;
    p.lfoWave = LfoWave::sine;
    p.lfoSpeed = 62;
    p.lfoAmpDepth = 40;
    p.fx.reverbMix = 0.05f;
    p.fx.reverbDecay = 2.5f;
    p.volume = 0.78f;
    return { "Vibrafon", p, "Glocken" };
}

NamedDxPatch glockenspiel()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    // Steel bars: partials at about 2.76 and 5.4 times the note, each ringing shorter than the one below.
    p.ops[0] = op(1, 99, { 99, 27, 25, 45 }, { 99, 60, 0, 0 }, 0, 0, 0, 3);
    p.ops[1] = op(5, 62, { 99, 70, 50, 60 }, { 99, 0, 0, 0 }, 5);
    p.ops[2] = op(2, 85, { 99, 32, 25, 45 }, { 99, 55, 0, 0 }, 0, 0, 38, 3);
    p.ops[3] = op(7, 55, { 99, 70, 50, 60 }, { 99, 0, 0, 0 }, 4);
    p.ops[4] = op(5, 75, { 99, 42, 25, 45 }, { 99, 45, 0, 0 }, 0, 0, 8, 3);
    p.ops[5] = op(1, 50, { 99, 70, 50, 60 }, { 99, 0, 0, 0 });
    p.fx.reverbMix = 0.05f;
    p.fx.reverbDecay = 2.5f;
    p.volume = 0.76f;
    return { "Glockenspiel", p, "Glocken" };
}

NamedDxPatch kalimba()
{
    DxPatch p;
    p.algorithm = 0; // 1 with operators 5 and 6 off: two pairs
    p.feedback = 0;
    // The thumb's click on the tine, and the tine's partial near six times the note that fades first.
    p.ops[0] = op(1, 99, { 99, 30, 30, 50 }, { 99, 60, 0, 0 }, 1);
    p.ops[1] = op(2, 70, { 99, 75, 50, 70 }, { 99, 0, 0, 0 }, 5);
    p.ops[2] = op(5, 78, { 99, 55, 40, 60 }, { 99, 0, 0, 0 }, 2, 0, 18);
    p.ops[3] = op(1, 50, { 99, 70, 50, 60 }, { 99, 0, 0, 0 });
    p.ops[4] = off();
    p.ops[5] = off();
    p.fx.reverbMix = 0.04f;
    p.volume = 0.82f;
    return { "Kalimba", p, "Glocken" };
}

NamedDxPatch steelDrum()
{
    DxPatch p;
    p.algorithm = 0; // 1 with operators 5 and 6 off: two pairs
    p.feedback = 0;
    // A modulator at one and a half times the note puts partials between the harmonics: the pan's ring.
    p.ops[0] = op(1, 99, { 99, 32, 30, 55 }, { 99, 65, 0, 0 }, 1);
    p.ops[1] = op(1, 72, { 99, 50, 35, 60 }, { 99, 30, 0, 0 }, 5, 0, 50);
    p.ops[2] = op(2, 82, { 99, 38, 30, 55 }, { 99, 55, 0, 0 }, 1, 4);
    p.ops[3] = op(3, 62, { 99, 70, 50, 60 }, { 99, 0, 0, 0 }, 4);
    p.ops[4] = off();
    p.ops[5] = off();
    p.fx.reverbMix = 0.04f;
    p.volume = 0.77f;
    return { "Steeldrum", p, "Glocken" };
}

NamedDxPatch gong()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 5;
    // Modulators at odd ratios, a few cents off, beat against low carriers; the second pair swells after the strike.
    p.ops[0] = op(0, 99, { 85, 20, 20, 25 }, { 99, 70, 0, 0 }, 2);
    p.ops[1] = op(1, 80, { 70, 22, 20, 25 }, { 99, 75, 0, 0 }, 3, 7, 41);
    p.ops[2] = op(1, 92, { 85, 22, 20, 25 }, { 99, 65, 0, 0 }, 2, -5);
    p.ops[3] = op(2, 82, { 50, 25, 20, 25 }, { 99, 80, 0, 0 }, 3, 0, 37);
    p.ops[4] = op(3, 80, { 90, 25, 20, 25 }, { 99, 55, 0, 0 }, 2, 0, 17);
    p.ops[5] = op(5, 70, { 90, 30, 20, 25 }, { 99, 50, 0, 0 }, 3, 0, 23);
    p.fx.reverbMix = 0.06f;
    p.fx.reverbDecay = 5.0f;
    p.volume = 0.54f;
    return { "Gong", p, "Glocken" };
}

NamedDxPatch strings()
{
    DxPatch p;
    p.algorithm = 18; // 19: operator 6 drives two carriers, a chain of three beside them
    p.feedback = 6;
    // Three carriers spread by detune, two of them on one modulator close to a sawtooth: a section, not a soloist.
    const Stages bow { 45, 40, 50, 45 };
    p.ops[0] = op(1, 99, bow, { 99, 95, 95, 0 }, 2);
    p.ops[1] = op(1, 78, { 40, 40, 50, 45 }, { 99, 90, 88, 0 }, 3);
    p.ops[2] = op(1, 68, { 40, 40, 50, 45 }, { 99, 90, 88, 0 }, 3);
    p.ops[3] = op(1, 95, bow, { 99, 95, 95, 0 }, 2, 5);
    p.ops[4] = op(1, 95, bow, { 99, 95, 95, 0 }, 2, -5);
    p.ops[5] = op(1, 80, { 40, 40, 50, 45 }, { 99, 92, 90, 0 }, 3);
    vibrato(p, 20, 50);
    p.fx.reverbMix = 0.06f;
    p.fx.reverbDecay = 3.0f;
    p.volume = 0.37f;
    return { "Streicher", p, "Bläser & Streicher" };
}

NamedDxPatch clarinet()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    // A modulator at twice the note leaves only odd harmonics: the clarinet's hollow tone, darker when played softly.
    p.ops[0] = op(1, 99, { 70, 50, 50, 70 }, { 99, 95, 95, 0 }, 2);
    p.ops[1] = op(2, 80, { 65, 50, 50, 70 }, { 99, 90, 88, 0 }, 4);
    p.ops[2] = op(3, 70, { 70, 50, 50, 70 }, { 99, 92, 90, 0 }, 2);
    p.ops[3] = op(2, 60, { 65, 50, 50, 70 }, { 99, 88, 85, 0 }, 4);
    p.ops[4] = off();
    p.ops[5] = off();
    vibrato(p, 12, 60);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.43f;
    return { "Klarinette", p, "Bläser & Streicher" };
}

NamedDxPatch oboe()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 4;
    // Nasal: the second to fourth harmonics stronger than the note itself.
    p.ops[0] = op(1, 90, { 72, 50, 50, 70 }, { 99, 95, 95, 0 }, 2);
    p.ops[1] = op(1, 84, { 68, 50, 50, 70 }, { 99, 92, 90, 0 }, 4);
    p.ops[2] = op(3, 92, { 72, 50, 50, 70 }, { 99, 95, 95, 0 }, 2);
    p.ops[3] = op(1, 70, { 68, 50, 50, 70 }, { 99, 90, 88, 0 }, 4);
    p.ops[4] = op(2, 90, { 72, 50, 50, 70 }, { 99, 95, 95, 0 }, 2);
    p.ops[5] = op(1, 74, { 68, 50, 50, 70 }, { 99, 90, 88, 0 }, 4);
    vibrato(p, 14, 55);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.52f;
    return { "Oboe", p, "Bläser & Streicher" };
}

NamedDxPatch trumpet()
{
    DxPatch p;
    p.algorithm = 1; // 2
    p.feedback = 6;
    // The modulators open a little after the note starts, as a player's lips do, and play brighter when blown harder.
    p.ops[0] = op(1, 99, { 75, 60, 99, 70 }, { 99, 92, 96, 0 }, 1);
    p.ops[1] = op(1, 88, { 60, 50, 30, 70 }, { 88, 99, 94, 0 }, 4);
    p.ops[2] = op(1, 92, { 75, 60, 99, 70 }, { 99, 92, 96, 0 }, 1, 3);
    p.ops[3] = op(1, 80, { 58, 50, 30, 70 }, { 85, 97, 92, 0 }, 4);
    p.ops[4] = op(2, 66, { 60, 50, 30, 70 }, { 85, 95, 90, 0 }, 4);
    p.ops[5] = op(1, 60, { 60, 50, 30, 70 }, { 85, 95, 90, 0 }, 3);
    vibrato(p, 16, 65);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.4f;
    return { "Trompete", p, "Bläser & Streicher" };
}

NamedDxPatch glassPad()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    // Three slow pairs with high, quiet modulators; the LFO lets the carriers breathe.
    p.ops[0] = op(1, 99, { 40, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, 0, 0, 1);
    p.ops[1] = op(3, 68, { 30, 25, 30, 40 }, { 99, 90, 85, 0 }, 2, 0, 0, 1);
    p.ops[2] = op(1, 96, { 40, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, 5, 0, 1);
    p.ops[3] = op(7, 55, { 28, 25, 30, 40 }, { 99, 85, 80, 0 }, 2, 0, 0, 1);
    p.ops[4] = op(2, 85, { 38, 30, 30, 40 }, { 99, 95, 95, 0 }, 1, -5, 0, 1);
    p.ops[5] = op(5, 52, { 26, 25, 30, 40 }, { 99, 85, 80, 0 }, 2, 0, 0, 1);
    p.ops[0].ampModSens = 1;
    p.ops[2].ampModSens = 1;
    p.ops[4].ampModSens = 1;
    p.lfoWave = LfoWave::triangle;
    p.lfoSpeed = 30;
    p.lfoAmpDepth = 30;
    p.fx.reverbMix = 0.07f;
    p.fx.reverbDecay = 4.0f;
    p.volume = 0.32f;
    return { "Glasfläche", p, "Flächen" };
}

NamedDxPatch choir()
{
    DxPatch p;
    p.algorithm = 30; // 31: five carriers, the last with a modulator
    p.feedback = 7;
    // Voices singing "ah": the note, its octave and the harmonics near the vowel's formant, each slightly apart;
    // operator 6, fed back, is the breath on the highest.
    const Stages slow { 35, 30, 50, 40 };
    p.ops[0] = op(1, 99, slow, { 99, 96, 96, 0 }, 1, 6);
    p.ops[1] = op(1, 95, slow, { 99, 96, 96, 0 }, 1, -6);
    p.ops[2] = op(2, 88, slow, { 99, 95, 95, 0 }, 1, 3);
    p.ops[3] = op(3, 84, slow, { 99, 94, 94, 0 }, 1, -3);
    p.ops[4] = op(4, 78, slow, { 99, 92, 92, 0 }, 1);
    p.ops[5] = op(5, 60, { 30, 30, 50, 40 }, { 99, 90, 88, 0 }, 1);
    vibrato(p, 18, 50);
    p.fx.reverbMix = 0.07f;
    p.fx.reverbDecay = 4.0f;
    p.volume = 0.35f;
    return { "Chor", p, "Flächen" };
}

NamedDxPatch harp()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    // A plucked string: bright as it is let go, then round; longer on the low strings.
    p.ops[0] = op(1, 99, { 99, 30, 28, 45 }, { 99, 70, 0, 0 }, 2, 0, 0, 3);
    p.ops[1] = op(1, 76, { 99, 50, 40, 60 }, { 99, 40, 0, 0 }, 5, 0, 0, 3);
    p.ops[2] = op(2, 82, { 99, 35, 28, 45 }, { 99, 60, 0, 0 }, 2, 0, 0, 3);
    p.ops[3] = op(1, 60, { 99, 70, 50, 60 }, { 99, 0, 0, 0 }, 5, 0, 0, 3);
    p.ops[4] = op(1, 80, { 99, 32, 28, 45 }, { 99, 68, 0, 0 }, 2, 4, 0, 3);
    p.ops[5] = op(3, 58, { 99, 70, 50, 60 }, { 99, 0, 0, 0 }, 5, 0, 0, 3);
    p.fx.reverbMix = 0.05f;
    p.fx.reverbDecay = 2.5f;
    p.volume = 0.81f;
    return { "Harfe", p, "Plucks" };
}

NamedDxPatch koto()
{
    DxPatch p;
    p.algorithm = 1; // 2: operator 2 feeds back
    p.feedback = 6;
    // A twang: a nearly sawtooth pair that dulls quickly, over a third and fifth harmonic that ring on.
    p.ops[0] = op(1, 99, { 99, 36, 30, 55 }, { 99, 70, 0, 0 }, 2, 0, 0, 3);
    p.ops[1] = op(1, 85, { 99, 55, 40, 60 }, { 99, 40, 0, 0 }, 5, 0, 0, 3);
    p.ops[2] = op(3, 80, { 99, 40, 30, 55 }, { 99, 60, 0, 0 }, 1, 0, 0, 3);
    p.ops[3] = op(2, 70, { 99, 60, 40, 60 }, { 99, 30, 0, 0 }, 5, 0, 0, 3);
    p.ops[4] = op(5, 64, { 99, 70, 40, 60 }, { 99, 0, 0, 0 }, 4, 0, 0, 3);
    p.ops[5] = op(1, 60, { 99, 70, 40, 60 }, { 99, 0, 0, 0 }, 4, 0, 0, 3);
    p.fx.reverbMix = 0.04f;
    p.volume = 0.87f;
    return { "Koto", p, "Plucks" };
}

NamedDxPatch squareLead()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 0;
    // Modulators at twice the note give odd harmonics only, close to a square; the third pair sits an octave up.
    p.ops[0] = op(1, 99, { 99, 50, 50, 70 }, { 99, 96, 94, 0 }, 2);
    p.ops[1] = op(2, 84, { 99, 50, 50, 70 }, { 99, 94, 92, 0 }, 3);
    p.ops[2] = op(1, 92, { 99, 50, 50, 70 }, { 99, 96, 94, 0 }, 2, 5);
    p.ops[3] = op(2, 84, { 99, 50, 50, 70 }, { 99, 94, 92, 0 }, 3);
    p.ops[4] = op(2, 74, { 99, 50, 50, 70 }, { 99, 94, 92, 0 }, 2, -5);
    p.ops[5] = op(4, 70, { 99, 50, 50, 70 }, { 99, 90, 88, 0 }, 3);
    vibrato(p, 16, 55);
    p.fx.delayMix = 0.18f;
    p.fx.delaySync = true;
    p.fx.reverbMix = 0.03f;
    p.volume = 0.72f;
    return { "Rechteck-Lead", p, "Leads" };
}

NamedDxPatch dataStream()
{
    DxPatch p;
    p.algorithm = 4; // 5
    p.feedback = 4;
    // The LFO's sample and hold throws the pitch around by up to an octave, seven times a second: a computer at work.
    p.ops[0] = op(1, 99, { 99, 50, 50, 70 }, { 99, 95, 95, 0 }, 2);
    p.ops[1] = op(3, 75, { 99, 50, 50, 70 }, { 99, 90, 88, 0 }, 3);
    p.ops[2] = op(2, 85, { 99, 50, 50, 70 }, { 99, 95, 95, 0 }, 2, 7);
    p.ops[3] = op(5, 70, { 99, 50, 50, 70 }, { 99, 90, 88, 0 }, 3);
    p.ops[4] = off();
    p.ops[5] = off();
    p.lfoWave = LfoWave::sampleHold;
    p.lfoSpeed = 70;
    p.lfoPitchDepth = 80;
    p.lfoPitchSens = 7;
    p.fx.delayMix = 0.25f;
    p.fx.delaySync = true;
    p.fx.reverbMix = 0.06f;
    p.fx.reverbDecay = 3.0f;
    p.volume = 0.8f;
    return { "Datenstrom", p, "Effekte" };
}
} // namespace

const std::vector<NamedDxPatch>& factoryPresets()
{
    static const std::vector<NamedDxPatch> presets { electricPiano(), bass(),        brass(),       tubularBell(),
                                                     marimba(),       harmonica(),   clavi(),       pad(),
                                                     organ(),         flute(),       lead(),
                                                     // Since 0.4.0: a full bank of 32.
                                                     brightPiano(),   harpsichord(), accordion(),   slapBass(),
                                                     woodBass(),      synthBass(),   vibraphone(),  glockenspiel(),
                                                     kalimba(),       steelDrum(),   gong(),        strings(),
                                                     clarinet(),      oboe(),        trumpet(),     glassPad(),
                                                     choir(),         harp(),        koto(),        squareLead(),
                                                     dataStream() };
    return presets;
}
} // namespace tonwerkdx

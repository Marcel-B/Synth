#pragma once

#include "ExpanderEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/**
 * A patch as knob positions on a two-oscillator expander module of the kind Tonwerk Expander is modelled on (two
 * VCOs with sync, a 12 dB filter whose notch knob runs LP to HP plus a BP switch, mixer knobs saw–off–pulse, two ADS
 * envelopes, an LFO), two of them under a two-voice controller (assign UNI/SPLIT/DUO, pan, portamento). Plain C++.
 *
 * Such panels carry no scales, so a position is 0 (fully left, 7 o'clock) to 1 (fully right, 5 o'clock) and the
 * ranges below are estimates: a starting point, finished by ear. What the hardware cannot do goes into `notes`.
 */
namespace tonwerkexpander::hardware
{
/** The estimated ranges of the hardware's knobs. */
inline constexpr float kCutoffLow = 20.0f, kCutoffHigh = 20000.0f;  // VCF FREQUENCY, logarithmic
inline constexpr float kLfoLow = 0.05f, kLfoHigh = 20.0f;           // LFO rate, Hz
inline constexpr float kAttackLow = 1.0f, kAttackHigh = 5000.0f;    // ms
inline constexpr float kDecayLow = 2.0f, kDecayHigh = 10000.0f;     // ms, the release too: the envelopes are ADS
inline constexpr float kGlideLow = 1.0f, kGlideHigh = 2000.0f;      // ms
inline constexpr float kVcfOctaves = 6.0f;                          // VCF MODULATION fully + or −
inline constexpr float kVcoOctaves = 4.0f;                          // VCO MODULATION fully to FREQ
inline constexpr float kVibratoCents = 1200.0f;                     // VCO MODULATION fully to FREQ with the LFO

/** One knob: where it goes and what it stands for; without `set` it is tuned by ear. */
struct Knob
{
    float position = 0.5f;
    std::string value;
    bool set = true;
};

enum class VcoSource { none, env, envAndVibrato, lfo };
enum class VcfSource { none, env, ext, lfo };
enum class Assign { uni, split, duo };

struct Panel
{
    std::array<std::string, 2> tuning;          // VCO FREQUENCY, by ear
    bool sync = false;
    std::array<Knob, 2> vcoModulation;          // FREQ (left) – off – PW (right)
    std::array<VcoSource, 2> vcoSource { VcoSource::none, VcoSource::none };
    std::array<Knob, 2> pulseWidth;             // 10 % to 90 %
    std::array<Knob, 2> mixer;                  // saw (left) – off – pulse (right)
    Knob ext;                                   // in – off – noise
    Knob cutoff, resonance, notch, vcfModulation;
    bool bandpass = false;
    VcfSource vcfSource = VcfSource::none;
    Knob lfoRate;
    std::array<Knob, 3> ampEnvelope, filterEnvelope; // attack, decay, sustain
    Assign assign = Assign::duo;
    bool secondSilent = false;                  // mono: module 2's level down
    std::array<Knob, 2> pan;
    Knob portamento;
    bool portamentoOn = false;
    std::vector<std::string> notes;
};

/** A position as a clock hour to the quarter, "10:30". */
inline std::string clockOf(float position)
{
    const int minutes = (int) std::lround((7.0 * 60.0 + std::clamp(position, 0.0f, 1.0f) * 600.0) / 15.0) * 15;
    const int hour = (minutes / 60 - 1) % 12 + 1;
    char text[8];
    std::snprintf(text, sizeof(text), "%d:%02d", hour, minutes % 60);
    return text;
}

inline float logPosition(float value, float low, float high)
{
    return std::clamp(std::log(std::max(value, low) / low) / std::log(high / low), 0.0f, 1.0f);
}

/** Whole numbers with units, a decimal comma where a tenth matters. */
inline std::string whole(float value, const char* unit, bool sign = false)
{
    char text[32];
    std::snprintf(text, sizeof(text), sign ? "%+d %s" : "%d %s", (int) std::lround(value), unit);
    return text;
}

inline std::string tenths(float value, const char* unit, bool sign = false, bool english = false)
{
    char text[32];
    std::snprintf(text, sizeof(text), sign ? "%+.1f %s" : "%.1f %s", value, unit);
    std::string s = text;
    if (! english)
        std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

inline std::string percentOf(float share) { return whole(share * 100.0f, "%"); }

/** The sheet in German or, with `english`, in English (plain C++, so the texts are chosen here, not by `tr`). */
inline Panel panelFor(const Settings& s, bool english = false)
{
    Panel p;
    auto t = [english](const char* german, const char* inEnglish) { return std::string(english ? inEnglish : german); };
    const char* octaves = english ? "oct" : "Okt";
    const char* semitones = english ? "st" : "HT";
    const auto off = t("aus", "off");
    auto note = [&p](std::string text) {
        if (std::find(p.notes.begin(), p.notes.end(), text) == p.notes.end())
            p.notes.push_back(std::move(text));
    };

    // Oscillators: the frequencies by ear, VCO 2 as an interval to VCO 1.
    p.tuning[0] = s.osc1.octave == 0 ? t("Grundton", "root") : t("Grundton, ", "root, ") + whole((float) s.osc1.octave, octaves, true);
    const int interval = 12 * (s.osc2.octave - s.osc1.octave) + s.osc2.semitones;
    p.tuning[1] = interval == 0 ? t("wie VCO 1", "as VCO 1") : whole((float) interval, semitones, true);
    if (std::abs(s.osc2.fine) >= 0.5f)
        p.tuning[1] += ", " + whole(s.osc2.fine, "ct", true);
    p.sync = s.sync;
    const OscSettings* osc[2] { &s.osc1, &s.osc2 };
    for (int n = 0; n < 2; ++n)
    {
        p.pulseWidth[(size_t) n] = { std::clamp((osc[n]->width - 0.1f) / 0.8f, 0.0f, 1.0f), percentOf(osc[n]->width) };
        const float level = osc[n]->level;
        if (osc[n]->wave == Wave::pulse)
            p.mixer[(size_t) n] = { 0.5f + level / 2.0f, t("Puls ", "Pulse ") + percentOf(level) };
        else
            p.mixer[(size_t) n] = { 0.5f - level / 2.0f, t("Säge ", "Saw ") + percentOf(level) };
        if (osc[n]->wave == Wave::both)
            note("VCO " + std::to_string(n + 1)
                 + t(" spielt Säge und Puls zugleich; der Mixer nimmt eins: Säge.",
                     " plays saw and pulse at once; the mixer takes one: saw."));
    }
    p.ext = { 0.5f, off };

    // VCO modulation: one knob and one source per VCO. The filter envelope's sweep goes to VCO 2, the LFO's
    // vibrato or pulse width modulation to whichever VCO is still free.
    for (int n = 0; n < 2; ++n)
    {
        auto& knob = p.vcoModulation[(size_t) n];
        auto& from = p.vcoSource[(size_t) n];
        if (n == 1 && std::abs(s.envPitch) > 0.0f)
        {
            knob = { 0.5f - std::min(std::abs(s.envPitch) / (12.0f * kVcoOctaves), 1.0f) / 2.0f,
                     "FREQ " + whole(s.envPitch, semitones, true) };
            from = VcoSource::env;
            if (s.envPitch < 0.0f)
                note(t("Die Hüllkurve zieht VCO 2 nach unten; am Modul geht sie nur nach oben.",
                       "The envelope pulls VCO 2 down; on the module it only goes up."));
            if (s.lfoPitch > 0.0f || s.lfoPwm > 0.0f)
                note(t("VCO 2 nimmt die Hüllkurve, das LFO nur VCO 1 (ein Modulationsregler je VCO).",
                       "VCO 2 takes the envelope, the LFO only VCO 1 (one modulation knob per VCO)."));
        }
        else if (s.lfoPitch > 0.0f)
        {
            knob = { std::min(0.5f - std::min(s.lfoPitch / kVibratoCents, 1.0f) / 2.0f, 0.475f),
                     "FREQ ±" + whole(s.lfoPitch, "ct") };
            from = VcoSource::lfo;
            if (s.lfoPwm > 0.0f)
                note(t("Vibrato und PWM zugleich geht nicht (ein Modulationsregler je VCO): Vibrato.",
                       "Vibrato and PWM at once is not possible (one modulation knob per VCO): vibrato."));
        }
        else if (s.lfoPwm > 0.0f)
        {
            knob = { 0.5f + s.lfoPwm / 2.0f, "PW " + percentOf(s.lfoPwm) };
            from = VcoSource::lfo;
        }
        else
            knob = { 0.5f, off };
    }
    if (s.lfoPitch > 0.0f && s.lfoDelay > 0.0f)
        note(t("Das Vibrato setzt im Plugin nach ", "In the plugin the vibrato sets in after ") + whole(s.lfoDelay * 1000.0f, "ms")
             + t(" ein; das Modul hat keine Verzögerung.", "; the module has no delay."));

    // Filter.
    p.cutoff = { logPosition(s.cutoff, kCutoffLow, kCutoffHigh), whole(s.cutoff, "Hz") };
    p.resonance = { s.resonance, percentOf(s.resonance) };
    p.bandpass = s.bandpass;
    if (s.bandpass)
        p.notch = { 0.5f, "BP" };
    else if (s.mode < 0.005f)
        p.notch = { 0.0f, "LP" };
    else if (s.mode > 0.995f)
        p.notch = { 1.0f, "HP" };
    else if (std::abs(s.mode - 0.5f) < 0.005f)
        p.notch = { 0.5f, "Notch" };
    else
        p.notch = { s.mode, percentOf(s.mode) + t(" zu HP", " to HP") };
    if (std::abs(s.envCutoff) > 0.0f)
    {
        p.vcfModulation = { 0.5f + std::clamp(s.envCutoff * 6.0f / kVcfOctaves, -1.0f, 1.0f) / 2.0f,
                            tenths(s.envCutoff * 6.0f, octaves, true, english) };
        p.vcfSource = VcfSource::env;
        if (s.lfoCutoff > 0.0f)
            note(t("Hüllkurve und LFO bewegen das Filter; das Modul nimmt eine Quelle: die Hüllkurve.",
                   "Envelope and LFO move the filter; the module takes one source: the envelope."));
    }
    else if (s.lfoCutoff > 0.0f)
    {
        p.vcfModulation = { 0.5f + std::min(s.lfoCutoff * 3.0f / kVcfOctaves, 1.0f) / 2.0f,
                            "±" + tenths(s.lfoCutoff * 3.0f, octaves, false, english) };
        p.vcfSource = VcfSource::lfo;
    }
    else
        p.vcfModulation = { 0.5f, off };
    if (std::abs(s.keytrack - 0.5f) > 0.01f)
        note(t("Keytracking ", "Key tracking ") + percentOf(s.keytrack)
             + t(": am Modul über die VCF-CV-Buchse, sonst fest.", ": on the module through the VCF CV jack, else fixed."));

    // LFO, a triangle on the hardware.
    p.lfoRate = { logPosition(s.lfoRate, kLfoLow, kLfoHigh), tenths(s.lfoRate, "Hz", false, english) };
    if (s.lfoSync)
        note(t("Das LFO läuft im Plugin im Songtempo; das Modul frei.", "The plugin's LFO follows the song tempo; the module's runs free."));
    if ((s.lfoPitch > 0.0f || s.lfoPwm > 0.0f || s.lfoCutoff > 0.0f) && s.lfoShape != LfoShape::triangle)
        note(t("Das LFO des Moduls ist ein Dreieck, das Plugin spielt eine andere Form.",
               "The module's LFO is a triangle, the plugin plays another shape."));

    // Envelopes: attack, decay, sustain; the hardware's release runs at the decay's pace.
    auto envelope = [&](std::array<Knob, 3>& knobs, const EnvSettings& e, const char* name) {
        knobs[0] = { logPosition(e.attack * 1000.0f, kAttackLow, kAttackHigh), whole(e.attack * 1000.0f, "ms") };
        knobs[1] = { logPosition(e.decay * 1000.0f, kDecayLow, kDecayHigh), whole(e.decay * 1000.0f, "ms") };
        knobs[2] = { e.sustain, percentOf(e.sustain) };
        if (e.sustain > 0.05f && std::abs(std::log(e.release / std::max(e.decay, 0.001f))) > std::log(2.0f))
            note(std::string(name) + t("-Release ", " release ") + whole(e.release * 1000.0f, "ms") + ", Decay "
                 + whole(e.decay * 1000.0f, "ms")
                 + t(": am Modul klingt der Ton mit dem Decay aus (fürs Ausklingen DECAY auf ",
                     ": on the module the note fades with the decay (for the fade set DECAY to ")
                 + clockOf(logPosition(e.release * 1000.0f, kDecayLow, kDecayHigh)) + t(" Uhr).", " o'clock)."));
    };
    envelope(p.ampEnvelope, s.ampEnv, "Amp");
    envelope(p.filterEnvelope, s.filterEnv, "Filter");

    // The two voices.
    switch (s.voiceMode)
    {
        case VoiceMode::poly: p.assign = Assign::duo; break;
        case VoiceMode::mono:
            p.assign = Assign::uni;
            p.secondSilent = true;
            break;
        case VoiceMode::unison:
            p.assign = Assign::uni;
            note(t("Unison: Modul 2 um ", "Unison: detune module 2 by ") + whole(s.unisonDetune / 2.0f, "ct")
                 + t(" gegen Modul 1 verstimmen.", " against module 1."));
            break;
    }
    const float spread = p.secondSilent ? 0.0f : s.spread / 2.0f;
    p.pan = { Knob { 0.5f - spread, percentOf(s.spread) }, Knob { 0.5f + spread, "" } };
    p.portamentoOn = s.glide > 0.0f;
    p.portamento = p.portamentoOn ? Knob { logPosition(s.glide * 1000.0f, kGlideLow, kGlideHigh), whole(s.glide * 1000.0f, "ms") }
                                  : Knob { 0.0f, off };
    if (s.voiceMode != VoiceMode::mono)
        note(t("Das Plugin spielt bis zu acht Stimmen, die Module zwei.", "The plugin plays up to eight voices, the modules two."));
    if (s.velocity > 0.55f)
        note(t("Anschlag ", "Velocity ") + percentOf(s.velocity)
             + t(" geht am Modul nur über eine Velocity-CV.", " reaches the module only through a velocity CV."));
    return p;
}
} // namespace tonwerkexpander::hardware

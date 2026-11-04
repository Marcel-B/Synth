#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

// A piece as Claude describes it: tracks, sections written once each, and a form that strings the sections together
// (A A B A), so a repeated chorus costs no tokens twice. Times are in quarter notes from the section's start, which is
// what a MIDI file counts in, whatever the metre.
namespace komponist
{
// juce::String reads a plain const char* as ASCII; German texts with umlauts go through here.
inline juce::String de(const char* text) { return juce::String::fromUTF8(text); }

enum class Role
{
    melody,
    chords,
    bass,
    drums,
    other
};

struct Note
{
    double start = 0.0;  // quarter notes from the section's start
    double length = 1.0; // quarter notes
    int pitch = 60;      // MIDI note, C4 = 60
    int velocity = 90;
};

struct Part
{
    int track = 0; // index into Arrangement::tracks
    std::vector<Note> notes;
};

struct Section
{
    juce::String name;
    int bars = 8;
    std::vector<Part> parts;
};

struct Track
{
    juce::String name;
    Role role = Role::other;
    int program = 0; // General MIDI program, 0 to 127
    int channel = 1; // 1 to 16; drums always 10
};

struct Arrangement
{
    juce::String title;
    double tempo = 120.0;
    int numerator = 4;
    int denominator = 4;
    juce::String tonic = "C";
    bool minor = false;
    std::vector<Track> tracks;
    std::vector<Section> sections;
    std::vector<int> form; // indices into sections, in playing order

    double barLength() const { return numerator * 4.0 / denominator; }
    int totalBars() const;
    double totalLength() const { return totalBars() * barLength(); }
    // Every note of a track over the whole form, in quarter notes from the start, sorted by time.
    std::vector<Note> notesOf(int track) const;
};

struct ParseResult
{
    std::optional<Arrangement> arrangement;
    juce::String error;
    juce::StringArray warnings; // what was repaired or dropped
};

// Reads Claude's JSON (the shape of `schema()` in Composer.h), clamps what is out of range and drops what cannot be
// played, so a slightly wrong answer still becomes a file.
ParseResult parseArrangement(const juce::var& json);
ParseResult parseArrangement(const juce::String& jsonText);

// "D4", "F#3", "Bb2", "C-1" or a plain MIDI number ("36"); C4 = 60. -1 when it is neither.
int parsePitch(const juce::String& text);
// Sharps (positive) or flats (negative) of a key, -7 to 7; the key's spelling decides (F# major 6, Gb major -6).
int keySignatureOf(const juce::String& tonic, bool minor);

constexpr int ticksPerQuarter = 480;
// A type 1 file: a conductor track (title, tempo, metre, key), then one track per instrument with its name and
// program, drums on channel 10. With `onlyTrack`, that instrument alone (after the conductor track).
juce::MidiFile toMidiFile(const Arrangement& arrangement, std::optional<int> onlyTrack = std::nullopt);
bool writeMidiFile(const Arrangement& arrangement, const juce::File& file, std::optional<int> onlyTrack = std::nullopt);

// Back to the JSON Claude wrote, for asking it to change the piece.
juce::var toJson(const Arrangement& arrangement);

juce::String roleName(Role role);
} // namespace komponist

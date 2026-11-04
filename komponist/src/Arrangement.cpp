#include "Arrangement.h"

#include <algorithm>
#include <cmath>

namespace komponist
{
namespace
{
constexpr int maxTracks = 15; // 16 channels, one of them the drums'
constexpr int maxBars = 512;

Role roleFrom(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t == "melody") return Role::melody;
    if (t == "chords") return Role::chords;
    if (t == "bass") return Role::bass;
    if (t == "drums") return Role::drums;
    return Role::other;
}

double numberOr(const juce::var& value, double fallback)
{
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
        return static_cast<double>(value);
    if (value.isString())
    {
        const auto text = value.toString().trim();
        if (text.containsOnly("0123456789.-+") && text.containsAnyOf("0123456789"))
            return text.getDoubleValue();
    }
    return fallback;
}

juce::Array<juce::var> arrayOf(const juce::var& value)
{
    if (const auto* array = value.getArray())
        return *array;
    return {};
}
} // namespace

int Arrangement::totalBars() const
{
    int bars = 0;
    for (const auto index : form)
        bars += sections[static_cast<size_t>(index)].bars;
    return bars;
}

std::vector<Note> Arrangement::notesOf(int track) const
{
    std::vector<Note> notes;
    double offset = 0.0;
    for (const auto index : form)
    {
        const auto& section = sections[static_cast<size_t>(index)];
        for (const auto& part : section.parts)
        {
            if (part.track != track)
                continue;
            for (auto note : part.notes)
            {
                note.start += offset;
                notes.push_back(note);
            }
        }
        offset += section.bars * barLength();
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

int parsePitch(const juce::String& raw)
{
    auto text = raw.trim();
    if (text.isEmpty())
        return -1;
    if (text.containsOnly("0123456789"))
    {
        const auto number = text.getIntValue();
        return number <= 127 ? number : -1;
    }
    static const int letters[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase(text[0]);
    if (letter < 'A' || letter > 'G')
        return -1;
    int pitchClass = letters[letter - 'A'];
    int i = 1;
    for (; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (c == '#' || c == 0x266F) ++pitchClass;      // # or ♯
        else if (c == 'b' || c == 0x266D) --pitchClass; // b or ♭
        else break;
    }
    const auto octaveText = text.substring(i).trim();
    if (octaveText.isEmpty() || !octaveText.containsOnly("-0123456789") || octaveText.lastIndexOfChar('-') > 0)
        return -1;
    const auto pitch = 12 * (octaveText.getIntValue() + 1) + pitchClass;
    return pitch >= 0 && pitch <= 127 ? pitch : -1;
}

int keySignatureOf(const juce::String& rawTonic, bool minor)
{
    // Spelled names first, so the file says what the key is called (D minor one flat, D# minor six sharps).
    static const char* const majors[] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
    static const char* const minors[] = { "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#" };
    auto tonic = rawTonic.trim().replace(juce::String::charToString(0x266F), "#").replace(juce::String::charToString(0x266D), "b");
    if (tonic.isNotEmpty())
        tonic = tonic.substring(0, 1).toUpperCase() + tonic.substring(1).toLowerCase();
    const auto* names = minor ? minors : majors;
    for (int i = 0; i < 15; ++i)
        if (tonic == names[i])
            return i - 7;
    // An odd spelling (E#, Fb): the key a semitone count gives, flats preferred.
    const auto pitch = parsePitch(tonic + "4");
    if (pitch < 0)
        return 0;
    for (int i = 1; i < 14; ++i)
        if (parsePitch(juce::String(names[i]) + "4") % 12 == pitch % 12)
            return i - 7;
    return 0;
}

juce::String roleName(Role role)
{
    switch (role)
    {
        case Role::melody: return "melody";
        case Role::chords: return "chords";
        case Role::bass: return "bass";
        case Role::drums: return "drums";
        case Role::other: break;
    }
    return "other";
}

ParseResult parseArrangement(const juce::String& jsonText)
{
    juce::var json;
    const auto result = juce::JSON::parse(jsonText, json);
    if (result.failed())
    {
        ParseResult failed;
        failed.error = "JSON: " + result.getErrorMessage();
        return failed;
    }
    return parseArrangement(json);
}

ParseResult parseArrangement(const juce::var& json)
{
    ParseResult result;
    if (!json.isObject())
    {
        result.error = de("Kein Stück in der Antwort.");
        return result;
    }
    auto& warnings = result.warnings;
    Arrangement a;
    a.title = json["title"].toString().trim();
    if (a.title.isEmpty())
        a.title = "Ohne Titel";

    a.tempo = numberOr(json["tempo"], 120.0);
    if (!(a.tempo >= 20.0 && a.tempo <= 300.0))
    {
        warnings.add("Tempo " + juce::String(a.tempo) + " auf 20 bis 300 BPM begrenzt.");
        a.tempo = juce::jlimit(20.0, 300.0, std::isfinite(a.tempo) ? a.tempo : 120.0);
    }

    const auto metre = json["timeSignature"];
    a.numerator = juce::jlimit(1, 32, static_cast<int>(numberOr(metre["numerator"], 4)));
    a.denominator = static_cast<int>(numberOr(metre["denominator"], 4));
    if (a.denominator != 1 && a.denominator != 2 && a.denominator != 4 && a.denominator != 8 && a.denominator != 16)
    {
        warnings.add(de("Taktart-Nenner ") + juce::String(a.denominator) + " durch 4 ersetzt.");
        a.denominator = 4;
    }

    const auto key = json["key"];
    a.tonic = key["tonic"].toString().trim();
    if (parsePitch(a.tonic + "4") < 0)
        a.tonic = "C";
    a.minor = key["mode"].toString().trim().equalsIgnoreCase("minor");

    // Tracks: drums on channel 10, the others on 1 to 16 around it, in the order given.
    int nextChannel = 1;
    for (const auto& t : arrayOf(json["tracks"]))
    {
        if (static_cast<int>(a.tracks.size()) >= maxTracks)
        {
            warnings.add("Mehr als " + juce::String(maxTracks) + " Spuren; der Rest fehlt.");
            break;
        }
        Track track;
        track.name = t["name"].toString().trim();
        if (track.name.isEmpty())
            track.name = "Spur " + juce::String(a.tracks.size() + 1);
        track.role = roleFrom(t["role"].toString());
        track.program = juce::jlimit(0, 127, static_cast<int>(numberOr(t["program"], 0)));
        if (track.role == Role::drums)
            track.channel = 10;
        else
        {
            if (nextChannel == 10)
                ++nextChannel;
            track.channel = nextChannel <= 16 ? nextChannel++ : 16;
        }
        a.tracks.push_back(track);
    }
    if (a.tracks.empty())
    {
        result.error = de("Das Stück hat keine Spuren.");
        return result;
    }
    const auto trackIndex = [&a](const juce::String& name) {
        for (size_t i = 0; i < a.tracks.size(); ++i)
            if (a.tracks[i].name.equalsIgnoreCase(name.trim()))
                return static_cast<int>(i);
        return -1;
    };

    const auto barLength = a.barLength();
    int dropped = 0;
    for (const auto& s : arrayOf(json["sections"]))
    {
        Section section;
        section.name = s["name"].toString().trim();
        if (section.name.isEmpty())
            section.name = juce::String::charToString(static_cast<juce::juce_wchar>('A' + a.sections.size() % 26));
        section.bars = juce::jlimit(1, maxBars, static_cast<int>(numberOr(s["bars"], 8)));
        const auto length = section.bars * barLength;
        for (const auto& p : arrayOf(s["parts"]))
        {
            const auto name = p["track"].toString();
            const auto track = trackIndex(name);
            if (track < 0)
            {
                warnings.add("Teil " + section.name + ": unbekannte Spur \"" + name + "\" weggelassen.");
                continue;
            }
            Part part;
            part.track = track;
            for (const auto& n : arrayOf(p["notes"]))
            {
                Note note;
                note.start = numberOr(n["start"], -1.0);
                note.length = numberOr(n["length"], 0.0);
                note.pitch = n["pitch"].isString() ? parsePitch(n["pitch"].toString()) : static_cast<int>(numberOr(n["pitch"], -1));
                note.velocity = juce::jlimit(1, 127, static_cast<int>(numberOr(n["velocity"], 90)));
                if (!(note.start >= 0.0 && note.start < length - 1.0e-6) || note.pitch < 0 || note.pitch > 127 || !(note.length > 0.0))
                {
                    ++dropped;
                    continue;
                }
                // A note ringing past the section's end would sound under the next one; cut it at the bar line.
                note.length = std::min(note.length, length - note.start);
                part.notes.push_back(note);
            }
            section.parts.push_back(std::move(part));
        }
        a.sections.push_back(std::move(section));
    }
    if (dropped > 0)
        warnings.add(juce::String(dropped) + (dropped == 1 ? " Note" : " Noten") + de(" außerhalb des Teils oder ohne Tonhöhe weggelassen."));
    if (a.sections.empty())
    {
        result.error = de("Das Stück hat keine Teile.");
        return result;
    }

    for (const auto& f : arrayOf(json["form"]))
    {
        const auto name = f.toString().trim();
        bool found = false;
        for (size_t i = 0; i < a.sections.size() && !found; ++i)
            if (a.sections[i].name.equalsIgnoreCase(name))
            {
                a.form.push_back(static_cast<int>(i));
                found = true;
            }
        if (!found)
            warnings.add("Form: unbekannter Teil \"" + name + "\" weggelassen.");
    }
    if (a.form.empty())
        for (size_t i = 0; i < a.sections.size(); ++i)
            a.form.push_back(static_cast<int>(i));
    while (a.totalBars() > maxBars)
    {
        a.form.pop_back();
        if (a.form.size() == 1)
            break;
    }

    result.arrangement = std::move(a);
    return result;
}

juce::MidiFile toMidiFile(const Arrangement& a, std::optional<int> onlyTrack)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(ticksPerQuarter);
    const auto ticks = [](double quarters) { return std::round(quarters * ticksPerQuarter); };

    juce::MidiMessageSequence conductor;
    auto name = juce::MidiMessage::textMetaEvent(3, a.title);
    name.setTimeStamp(0);
    conductor.addEvent(name);
    auto tempo = juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / a.tempo));
    tempo.setTimeStamp(0);
    conductor.addEvent(tempo);
    auto metre = juce::MidiMessage::timeSignatureMetaEvent(a.numerator, a.denominator);
    metre.setTimeStamp(0);
    conductor.addEvent(metre);
    auto key = juce::MidiMessage::keySignatureMetaEvent(keySignatureOf(a.tonic, a.minor), a.minor);
    key.setTimeStamp(0);
    conductor.addEvent(key);
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(ticks(a.totalLength()));
    conductor.addEvent(end);
    file.addTrack(conductor);

    for (size_t t = 0; t < a.tracks.size(); ++t)
    {
        if (onlyTrack && *onlyTrack != static_cast<int>(t))
            continue;
        const auto& track = a.tracks[t];
        juce::MidiMessageSequence sequence;
        auto trackName = juce::MidiMessage::textMetaEvent(3, track.name);
        trackName.setTimeStamp(0);
        sequence.addEvent(trackName);
        auto program = juce::MidiMessage::programChange(track.channel, track.program);
        program.setTimeStamp(0);
        sequence.addEvent(program);
        for (const auto& note : a.notesOf(static_cast<int>(t)))
        {
            const auto on = ticks(note.start);
            // At least one tick, so a very short drum hit still has its note off after its note on.
            const auto off = std::max(on + 1.0, ticks(note.start + note.length));
            sequence.addEvent(juce::MidiMessage::noteOn(track.channel, note.pitch, static_cast<juce::uint8>(note.velocity)), on);
            sequence.addEvent(juce::MidiMessage::noteOff(track.channel, note.pitch), off);
        }
        // Note offs before note ons at the same tick, so a repeated note is not cut by its predecessor's end.
        sequence.sort();
        sequence.updateMatchedPairs();
        auto trackEnd = juce::MidiMessage::endOfTrack();
        trackEnd.setTimeStamp(std::max(ticks(a.totalLength()), sequence.getEndTime()));
        sequence.addEvent(trackEnd);
        file.addTrack(sequence);
    }
    return file;
}

bool writeMidiFile(const Arrangement& arrangement, const juce::File& file, std::optional<int> onlyTrack)
{
    const auto midi = toMidiFile(arrangement, onlyTrack);
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream out(temporary.getFile());
        if (!out.openedOk() || !midi.writeTo(out, 1))
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}

juce::var toJson(const Arrangement& a)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("title", a.title);
    root->setProperty("tempo", a.tempo);
    auto* metre = new juce::DynamicObject();
    metre->setProperty("numerator", a.numerator);
    metre->setProperty("denominator", a.denominator);
    root->setProperty("timeSignature", juce::var(metre));
    auto* key = new juce::DynamicObject();
    key->setProperty("tonic", a.tonic);
    key->setProperty("mode", a.minor ? "minor" : "major");
    root->setProperty("key", juce::var(key));
    juce::Array<juce::var> tracks;
    for (const auto& t : a.tracks)
    {
        auto* track = new juce::DynamicObject();
        track->setProperty("name", t.name);
        track->setProperty("role", roleName(t.role));
        track->setProperty("program", t.program);
        tracks.add(juce::var(track));
    }
    root->setProperty("tracks", tracks);
    juce::Array<juce::var> sections;
    for (const auto& s : a.sections)
    {
        auto* section = new juce::DynamicObject();
        section->setProperty("name", s.name);
        section->setProperty("bars", s.bars);
        juce::Array<juce::var> parts;
        for (const auto& p : s.parts)
        {
            auto* part = new juce::DynamicObject();
            part->setProperty("track", a.tracks[static_cast<size_t>(p.track)].name);
            juce::Array<juce::var> notes;
            for (const auto& n : p.notes)
            {
                auto* note = new juce::DynamicObject();
                note->setProperty("start", n.start);
                note->setProperty("length", n.length);
                note->setProperty("pitch", juce::String(n.pitch));
                note->setProperty("velocity", n.velocity);
                notes.add(juce::var(note));
            }
            part->setProperty("notes", notes);
            parts.add(juce::var(part));
        }
        section->setProperty("parts", parts);
        sections.add(juce::var(section));
    }
    root->setProperty("sections", sections);
    juce::Array<juce::var> form;
    for (const auto index : a.form)
        form.add(a.sections[static_cast<size_t>(index)].name);
    root->setProperty("form", form);
    return juce::var(root);
}
} // namespace komponist

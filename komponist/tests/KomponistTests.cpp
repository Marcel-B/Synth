#include "Arrangement.h"
#include "Composer.h"

#include <cstdio>

using namespace komponist;

namespace
{
// A slow waltz in D minor as Claude would answer: four tracks, an A part twice around a B part.
const char* const waltz = R"({
  "title": "Langsamer Walzer",
  "tempo": 84,
  "timeSignature": {"numerator": 3, "denominator": 4},
  "key": {"tonic": "D", "mode": "minor"},
  "tracks": [
    {"name": "Melodie", "role": "melody", "program": 40},
    {"name": "Akkorde", "role": "chords", "program": 0},
    {"name": "Bass", "role": "bass", "program": 32},
    {"name": "Schlagzeug", "role": "drums", "program": 0}
  ],
  "sections": [
    {"name": "A", "bars": 2, "parts": [
      {"track": "Melodie", "notes": [{"start": 0, "length": 2, "pitch": "A4", "velocity": 80},
                                     {"start": 2, "length": 1, "pitch": "Bb4", "velocity": 70},
                                     {"start": 3, "length": 3, "pitch": "A4", "velocity": 75}]},
      {"track": "Akkorde", "notes": [{"start": 1, "length": 1, "pitch": "D4", "velocity": 60},
                                     {"start": 1, "length": 1, "pitch": "F4", "velocity": 60}]},
      {"track": "Bass", "notes": [{"start": 0, "length": 1, "pitch": "D2", "velocity": 90},
                                  {"start": 3, "length": 1, "pitch": "A1", "velocity": 85}]},
      {"track": "Schlagzeug", "notes": [{"start": 0, "length": 0.25, "pitch": "36", "velocity": 70}]}
    ]},
    {"name": "B", "bars": 1, "parts": [
      {"track": "Bass", "notes": [{"start": 0, "length": 3, "pitch": "G2", "velocity": 80}]}
    ]}
  ],
  "form": ["A", "B", "A"]
})";

int countNotes(const juce::MidiMessageSequence& sequence)
{
    int count = 0;
    for (const auto* event : sequence)
        count += event->message.isNoteOn() ? 1 : 0;
    return count;
}
} // namespace

class ArrangementTests : public juce::UnitTest
{
public:
    ArrangementTests() : juce::UnitTest("Arrangement", "Komponist") {}

    void runTest() override
    {
        beginTest("Note names, C4 = 60");
        expectEquals(parsePitch("C4"), 60);
        expectEquals(parsePitch("A4"), 69);
        expectEquals(parsePitch("F#3"), 54);
        expectEquals(parsePitch("Bb2"), 46);
        expectEquals(parsePitch("C-1"), 0);
        expectEquals(parsePitch("G9"), 127);
        expectEquals(parsePitch("36"), 36);
        expectEquals(parsePitch(juce::String::fromUTF8("E\xe2\x99\xad" "3")), 51); // E♭3
        expectEquals(parsePitch("H4"), -1);
        expectEquals(parsePitch("C"), -1);
        expectEquals(parsePitch("200"), -1);

        beginTest("Key signatures follow the key's spelling");
        expectEquals(keySignatureOf("D", true), -1);
        expectEquals(keySignatureOf("C", false), 0);
        expectEquals(keySignatureOf("F#", false), 6);
        expectEquals(keySignatureOf("Gb", false), -6);
        expectEquals(keySignatureOf("bb", true), -5);
        expectEquals(keySignatureOf("D#", true), 6);

        beginTest("A waltz becomes a type 1 file with a track per instrument");
        const auto parsed = parseArrangement(juce::String(waltz));
        expect(parsed.arrangement.has_value(), parsed.error);
        expect(parsed.warnings.isEmpty(), parsed.warnings.joinIntoString("; "));
        const auto& a = *parsed.arrangement;
        expectEquals(a.totalBars(), 5);
        expectEquals(a.barLength(), 3.0);
        expectEquals(static_cast<int>(a.tracks.size()), 4);
        expectEquals(a.tracks[0].channel, 1);
        expectEquals(a.tracks[2].channel, 3);
        expectEquals(a.tracks[3].channel, 10);

        const auto midi = toMidiFile(a);
        expectEquals(midi.getNumTracks(), 5);
        expectEquals(midi.getTimeFormat(), static_cast<short>(ticksPerQuarter));
        const auto* conductor = midi.getTrack(0);
        bool tempo = false, metre = false, key = false;
        for (const auto* event : *conductor)
        {
            const auto& m = event->message;
            if (m.isTempoMetaEvent())
                tempo = std::abs(60.0 / m.getTempoSecondsPerQuarterNote() - 84.0) < 0.01;
            if (m.isTimeSignatureMetaEvent())
            {
                int n = 0, d = 0;
                m.getTimeSignatureInfo(n, d);
                metre = n == 3 && d == 4;
            }
            if (m.isKeySignatureMetaEvent())
                key = m.getKeySignatureNumberOfSharpsOrFlats() == -1 && !m.isKeySignatureMajorKey();
        }
        expect(tempo, "tempo 84 BPM");
        expect(metre, "3/4");
        expect(key, "D minor, one flat");

        // The melody plays in both A parts: three notes, twice.
        const auto* melody = midi.getTrack(1);
        expectEquals(countNotes(*melody), 6);
        bool named = false, violin = false;
        for (const auto* event : *melody)
        {
            if (event->message.isTrackNameEvent())
                named = event->message.getTextFromTextMetaEvent() == "Melodie";
            if (event->message.isProgramChange())
                violin = event->message.getProgramChangeNumber() == 40 && event->message.getChannel() == 1;
        }
        expect(named, "track name");
        expect(violin, "program 40 on channel 1");

        // The second A starts after A (2 bars) and B (1 bar): 9 quarter notes in.
        double lastStart = -1.0;
        for (const auto* event : *melody)
            if (event->message.isNoteOn())
                lastStart = event->message.getTimeStamp();
        expectEquals(lastStart, (9.0 + 3.0) * ticksPerQuarter);

        // Bass: two notes in each A, one in B; B's G2 lasts its whole bar.
        expectEquals(countNotes(*midi.getTrack(3)), 5);
        const auto bassNotes = a.notesOf(2);
        expectEquals(bassNotes[2].start, 6.0);
        expectEquals(bassNotes[2].pitch, parsePitch("G2"));

        // Drums on channel 10.
        for (const auto* event : *midi.getTrack(4))
            if (event->message.isNoteOn())
                expectEquals(event->message.getChannel(), 10);

        beginTest("A single track goes alone after the conductor track");
        const auto bass = toMidiFile(a, 2);
        expectEquals(bass.getNumTracks(), 2);
        expectEquals(countNotes(*bass.getTrack(1)), 5);

        beginTest("The file reads back");
        juce::MemoryOutputStream out;
        expect(midi.writeTo(out, 1));
        juce::MidiFile back;
        juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
        expect(back.readFrom(in));
        expectEquals(back.getNumTracks(), 5);
        expectEquals(countNotes(*back.getTrack(2)), 4);

        beginTest("Repeated notes are not cut by their predecessor's end");
        {
            auto repeated = a;
            repeated.sections[0].parts[0].notes = { { 0.0, 1.0, 60, 90 }, { 1.0, 1.0, 60, 90 } };
            const auto file = toMidiFile(repeated);
            const auto* track = file.getTrack(1);
            juce::MidiMessageSequence copy(*track);
            copy.updateMatchedPairs();
            int paired = 0;
            for (const auto* event : copy)
                if (event->message.isNoteOn() && event->noteOffObject != nullptr)
                    paired += event->noteOffObject->message.getTimeStamp() > event->message.getTimeStamp() ? 1 : 0;
            expectEquals(paired, 4);
        }

        beginTest("Out-of-range answers are repaired, not refused");
        {
            auto json = juce::JSON::parse(juce::String(waltz));
            json.getDynamicObject()->setProperty("tempo", 900);
            json["timeSignature"].getDynamicObject()->setProperty("denominator", 5);
            auto* notes = json["sections"][0]["parts"][0]["notes"].getArray();
            notes->add(juce::JSON::parse(R"({"start": 99, "length": 1, "pitch": "C4", "velocity": 80})"));
            notes->add(juce::JSON::parse(R"({"start": 0, "length": 1, "pitch": "X9", "velocity": 80})"));
            notes->add(juce::JSON::parse(R"({"start": 5, "length": 8, "pitch": "C4", "velocity": 300})"));
            json["sections"][0]["parts"].getArray()->add(juce::JSON::parse(R"({"track": "Tuba", "notes": []})"));
            json["form"].getArray()->add("C");
            const auto repaired = parseArrangement(json);
            expect(repaired.arrangement.has_value());
            const auto& r = *repaired.arrangement;
            expectEquals(r.tempo, 300.0);
            expectEquals(r.denominator, 4);
            expectEquals(static_cast<int>(r.form.size()), 3);
            const auto& last = r.sections[0].parts[0].notes.back();
            // Still 3/4: two bars are 6 quarter notes, so the note from 5 is cut at 6.
            expectEquals(last.length, 1.0);
            expectEquals(last.velocity, 127);
            expectEquals(static_cast<int>(r.sections[0].parts[0].notes.size()), 4);
            expectGreaterOrEqual(repaired.warnings.size(), 4);
        }

        beginTest("Without tracks or sections there is no piece");
        expect(!parseArrangement(juce::String("{}")).arrangement.has_value());
        expect(!parseArrangement(juce::String("not json")).arrangement.has_value());
        expect(!parseArrangement(juce::String(R"({"tracks":[{"name":"x","role":"bass","program":0}],"sections":[]})")).arrangement.has_value());

        beginTest("Channels skip 10 for melodic tracks");
        {
            juce::String json = R"({"tracks":[)";
            for (int i = 0; i < 12; ++i)
                json << (i ? "," : "") << R"({"name":"T)" << i << R"(","role":"other","program":0})";
            json << R"(],"sections":[{"name":"A","bars":1,"parts":[]}],"form":["A"]})";
            const auto many = parseArrangement(json);
            expect(many.arrangement.has_value());
            for (const auto& track : many.arrangement->tracks)
                expect(track.channel != 10);
            expectEquals(many.arrangement->tracks[9].channel, 11);
        }

        beginTest("JSON round trip for a change request");
        const auto again = parseArrangement(toJson(a));
        expect(again.arrangement.has_value());
        expectEquals(again.arrangement->totalBars(), a.totalBars());
        expectEquals(static_cast<int>(again.arrangement->notesOf(0).size()), 6);
        expect(again.arrangement->minor);
    }
};

class ComposerTests : public juce::UnitTest
{
public:
    ComposerTests() : juce::UnitTest("Composer", "Komponist") {}

    void runTest() override
    {
        beginTest("The request asks for the schema, streamed, with a fallback");
        const auto body = juce::JSON::parse(requestBody("claude-opus-5-5", "Langsamer Walzer in Dm", nullptr));
        expectEquals(body["model"].toString(), juce::String("claude-opus-5-5"));
        expect(static_cast<bool>(body["stream"]));
        expectEquals(body["fallbacks"].toString(), juce::String("default"));
        expectEquals(body["output_config"]["format"]["type"].toString(), juce::String("json_schema"));
        expectEquals(body["messages"][0]["content"].toString(), juce::String("Langsamer Walzer in Dm"));
        expect(body["thinking"].isVoid(), "Opus 5.5 thinks on its own; no thinking setting");
        expect(body["temperature"].isVoid());

        beginTest("Every object of the schema is strict");
        std::function<void(const juce::var&)> check = [&](const juce::var& node) {
            if (node["type"].toString() == "object")
            {
                expect(node["additionalProperties"].isBool() && !static_cast<bool>(node["additionalProperties"]));
                const auto* props = node["properties"].getDynamicObject();
                expect(props != nullptr);
                expectEquals(node["required"].getArray()->size(), props->getProperties().size());
                for (const auto& p : props->getProperties())
                    check(p.value);
            }
            if (node["type"].toString() == "array")
                check(node["items"]);
        };
        check(schema());

        beginTest("A change sends the piece along");
        const auto parsed = parseArrangement(juce::String(waltz));
        const auto message = userMessage("Ohne Schlagzeug", &*parsed.arrangement);
        expect(message.contains("\"Schlagzeug\""));
        expect(message.endsWith("Ohne Schlagzeug"));

        beginTest("The stream's text deltas make the piece");
        const auto json = juce::JSON::toString(juce::var(juce::String(waltz)), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none));
        const auto half = json.length() / 2;
        // Split the JSON text into two deltas, as a stream would, each a JSON string of its own.
        const auto first = juce::JSON::toString(juce::var(juce::String(waltz).substring(0, half)), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none));
        const auto second = juce::JSON::toString(juce::var(juce::String(waltz).substring(half)), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none));
        StreamCollector stream;
        const juce::StringArray lines {
            "event: message_start",
            R"(data: {"type":"message_start","message":{"model":"claude-opus-5-5"}})",
            "",
            ": ping",
            "event: content_block_start",
            R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}})",
            "",
            R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"abc"}})",
            "",
            R"(data: {"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":)" + first + "}}",
            "",
            R"(data: {"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":)" + second + "}}\r",
            "\r",
            R"(data: {"type":"message_delta","delta":{"stop_reason":"end_turn"}})",
            "",
            R"(data: {"type":"message_stop"})",
            "",
        };
        for (const auto& l : lines)
            stream.line(l);
        expect(stream.finished());
        expectEquals(stream.model(), juce::String("claude-opus-5-5"));
        const auto outcome = outcomeOf(stream);
        expect(outcome.parsed.arrangement.has_value(), outcome.errorTitle + ": " + outcome.parsed.error);
        if (outcome.parsed.arrangement)
            expectEquals(outcome.parsed.arrangement->title, juce::String("Langsamer Walzer"));

        beginTest("Refusals, cut-off answers and errors become messages");
        {
            StreamCollector refused;
            refused.line(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"{\"title\""}})");
            refused.line("");
            refused.line(R"(data: {"type":"message_delta","delta":{"stop_reason":"refusal"}})");
            refused.line("");
            refused.line(R"(data: {"type":"message_stop"})");
            refused.finish();
            const auto o = outcomeOf(refused);
            expect(!o.parsed.arrangement.has_value());
            expectEquals(o.errorTitle, juce::String("Claude hat abgelehnt"));
        }
        {
            StreamCollector cut;
            cut.line(R"(data: {"type":"message_delta","delta":{"stop_reason":"max_tokens"}})");
            cut.line("");
            cut.line(R"(data: {"type":"message_stop"})");
            cut.line("");
            expectEquals(outcomeOf(cut).errorTitle, juce::String("Zu lang"));
        }
        {
            StreamCollector failed;
            failed.line(R"(data: {"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})");
            failed.finish();
            const auto o = outcomeOf(failed);
            expectEquals(o.parsed.error, juce::String("Overloaded"));
        }
        {
            StreamCollector broken;
            broken.line(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"{\"ti"}})");
            broken.line("");
            expectEquals(outcomeOf(broken).errorTitle, juce::String("Abgebrochen"));
        }
    }
};

// With KOMPONIST_API_URL set (tests/fake-api.py serves one), the whole way through HTTP, streamed.
class ComposeTests : public juce::UnitTest
{
public:
    ComposeTests() : juce::UnitTest("Compose over HTTP", "Komponist") {}

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable("KOMPONIST_API_URL", {}).isEmpty())
            return;
        beginTest("A streamed answer arrives as a piece");
        std::atomic<bool> cancelled { false };
        int lastProgress = 0;
        const auto outcome = compose("test-key", "claude-opus-5-5", "Langsamer Walzer in Dm", nullptr, [&](int chars) { lastProgress = chars; }, cancelled);
        expect(outcome.parsed.arrangement.has_value(), outcome.errorTitle + ": " + outcome.parsed.error);
        expectGreaterThan(lastProgress, 100);
        beginTest("A wrong key is said so");
        const auto refused = compose("wrong-key", "claude-opus-5-5", "Walzer", nullptr, {}, cancelled);
        expect(!refused.parsed.arrangement.has_value());
        expect(refused.errorTitle.contains("API"), refused.errorTitle);
        expectEquals(refused.parsed.error, juce::String("invalid x-api-key"));
    }
};

static ArrangementTests arrangementTests;
static ComposeTests composeTests;
static ComposerTests composerTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;
    std::printf("%s\n", failures == 0 ? "All tests passed." : "Some tests FAILED.");
    return failures == 0 ? 0 : 1;
}

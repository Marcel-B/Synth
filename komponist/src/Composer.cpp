#include "Composer.h"

namespace komponist
{
namespace
{
juce::DynamicObject* object(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        o->setProperty(name, value);
    return o;
}

juce::var type(const char* name) { return juce::var(object({ { "type", name } })); }

// Structured outputs want every property required and nothing else allowed.
juce::var strictObject(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* props = new juce::DynamicObject();
    juce::Array<juce::var> required;
    for (const auto& [name, value] : properties)
    {
        props->setProperty(name, value);
        required.add(name);
    }
    return juce::var(object({ { "type", "object" },
                              { "properties", juce::var(props) },
                              { "required", required },
                              { "additionalProperties", false } }));
}

juce::var arrayOf(const juce::var& items) { return juce::var(object({ { "type", "array" }, { "items", items } })); }

juce::var enumOf(std::initializer_list<const char*> values)
{
    juce::Array<juce::var> list;
    for (const auto* v : values)
        list.add(v);
    return juce::var(object({ { "type", "string" }, { "enum", list } }));
}
} // namespace

const std::vector<Model>& models()
{
    static const std::vector<Model> list {
        { "claude-opus-5-5", "Claude Opus 5.5", true },
        { "claude-sonnet-5-5", "Claude Sonnet 5.5 (schneller)", true },
        { "claude-fable-5-1", "Claude Fable 5.1 (am stärksten)", true },
    };
    return list;
}

juce::var schema()
{
    const auto note = strictObject({ { "start", type("number") },
                                     { "length", type("number") },
                                     { "pitch", type("string") },
                                     { "velocity", type("integer") } });
    const auto part = strictObject({ { "track", type("string") }, { "notes", arrayOf(note) } });
    const auto section = strictObject({ { "name", type("string") }, { "bars", type("integer") }, { "parts", arrayOf(part) } });
    const auto track = strictObject({ { "name", type("string") },
                                      { "role", enumOf({ "melody", "chords", "bass", "drums", "other" }) },
                                      { "program", type("integer") } });
    return strictObject({ { "title", type("string") },
                          { "tempo", type("number") },
                          { "timeSignature", strictObject({ { "numerator", type("integer") }, { "denominator", type("integer") } }) },
                          { "key", strictObject({ { "tonic", type("string") }, { "mode", enumOf({ "major", "minor" }) } }) },
                          { "tracks", arrayOf(track) },
                          { "sections", arrayOf(section) },
                          { "form", arrayOf(type("string")) } });
}

juce::String systemPrompt()
{
    return R"(You are a composer and arranger. The user describes a piece of music, usually in German, sometimes in English, and you write it as a MIDI arrangement they will drag into Logic Pro or Ableton Live and play with their own instruments. Answer with the piece in the JSON format you are given and nothing else.

How the format works:
- Times are in quarter notes. A note's "start" counts from the beginning of its section, "length" is its duration; decimals are fine (0.5 is an eighth, 0.25 a sixteenth, 1/3 = 0.3333 for triplets). A bar is numerator * 4 / denominator quarter notes: 3/4 has 3, 4/4 has 4, 6/8 has 3. Every note must start inside its section.
- "tempo" is in quarter notes per minute (6/8 felt at 60 dotted quarters per minute is tempo 90).
- "pitch" is a note name with C4 as middle C (MIDI 60): "D4", "F#3", "Bb2". Drum hits are General MIDI drum numbers as text, e.g. "36".
- Write every distinct section once, then put the order into "form"; repeating a name repeats the section ("Intro", "A", "A", "B", "A", "Outro"). A section has one part per track that plays in it; a silent track has no part there. Use the track names exactly as in "tracks".
- "program" is the General MIDI program number counted from 0: 0 grand piano, 4 electric piano, 6 harpsichord, 11 vibraphone, 19 church organ, 21 accordion, 24 nylon guitar, 25 steel guitar, 32 acoustic bass, 33 finger bass, 35 fretless bass, 38 synth bass, 40 violin, 41 viola, 42 cello, 43 contrabass, 45 pizzicato strings, 46 harp, 48 string ensemble, 49 slow strings, 52 choir aahs, 56 trumpet, 57 trombone, 60 french horn, 61 brass section, 65 alto sax, 68 oboe, 70 bassoon, 71 clarinet, 73 flute, 80 square lead, 81 saw lead, 88 new age pad, 89 warm pad, 95 sweep pad. Drum tracks use program 0.
- Drums follow the General MIDI drum map: 35/36 kick, 37 side stick, 38 snare, 40 electric snare, 39 clap, 42 closed hi-hat, 44 pedal hi-hat, 46 open hi-hat, 49 crash, 51 ride, 53 ride bell, 41/43/45/47/48/50 toms low to high, 54 tambourine, 56 cowbell, 69 cabasa, 70 maracas, 75 claves.

What to write:
- Unless the user asks for something else: four tracks, a melody, chords, a bass and drums (roles "melody", "chords", "bass", "drums"), with short names in the user's language (German: "Melodie", "Akkorde", "Bass", "Schlagzeug"). If the user wants no drums, leave the drum track out. If they name instruments, use them and pick fitting programs.
- Unless a length is given, about 24 to 40 bars in all, for example an intro of 4 bars, an A part of 8, a B part of 8, the A part again and a short ending.
- Make it sound like the genre the user asks for, the way a good session musician would play it: a waltz has the bass on beat one and the chords on two and three; a ballad holds its chords; a funk bass is syncopated. Melodies move in phrases of 4 and 8 bars, have a clear motif that comes back and varied, breathe with rests, and end their phrases on cadences. Chords are voiced in the middle (about C3 to G4) with smooth voice leading; the bass stays between E1 and D3; the melody lies above the chords, about C4 to C6. Shape the dynamics with velocities between about 45 and 115: accents on strong beats, phrases that rise and fall, softer accompaniment under the melody. In a genre that has no drums of its own (a slow waltz, a chorale), keep the drums soft and sparse, such as a kick on one and a quiet hi-hat or side stick on two and three.
- The key and mode come from the description: "tonic" spelled as a musician would ("D", "F#", "Bb"), "mode" "major" or "minor" (for modal music, the closer of the two). Use the key's own notes and harmony, spelled to fit it.
- "title" is a short title in the user's language.

When you are given an existing piece and a change, return the whole piece with that change made and everything else as it was.)";
}

juce::String userMessage(const juce::String& wish, const Arrangement* current)
{
    if (current == nullptr)
        return wish.trim();
    return "Here is the current piece:\n\n" + juce::JSON::toString(toJson(*current), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none))
           + "\n\nChange it as follows:\n\n" + wish.trim();
}

juce::String requestBody(const juce::String& modelId, const juce::String& wish, const Arrangement* current)
{
    bool fallback = false;
    for (const auto& m : models())
        if (modelId == m.id)
            fallback = m.serverFallback;

    juce::Array<juce::var> messages;
    messages.add(juce::var(object({ { "role", "user" }, { "content", userMessage(wish, current) } })));
    auto* format = object({ { "type", "json_schema" }, { "schema", schema() } });
    // Medium effort: a piece needs thought, but the high levels mostly lengthen the wait.
    auto* outputConfig = object({ { "effort", "medium" }, { "format", juce::var(format) } });
    auto* body = object({ { "model", modelId },
                          { "max_tokens", 64000 },
                          { "stream", true },
                          { "system", systemPrompt() },
                          { "messages", messages },
                          { "output_config", juce::var(outputConfig) } });
    if (fallback)
        body->setProperty("fallbacks", "default");
    return juce::JSON::toString(juce::var(body), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none));
}

void StreamCollector::line(const juce::String& raw)
{
    const auto line = raw.trimCharactersAtEnd("\r\n");
    if (line.isEmpty())
    {
        event();
        return;
    }
    if (line.startsWith("data:"))
    {
        if (data.isNotEmpty())
            data << "\n";
        data << line.substring(5).trimStart();
    }
    // "event:" names repeat the data's "type"; comments (":") and ids are of no use here.
}

void StreamCollector::finish() { event(); }

void StreamCollector::event()
{
    if (data.isEmpty())
        return;
    const auto json = juce::JSON::parse(data);
    data.clear();
    const auto type = json["type"].toString();
    if (type == "message_start")
        model_ = json["message"]["model"].toString();
    else if (type == "content_block_delta")
    {
        // Only text: thinking and its signature are not part of the piece. After a server-side fallback the new
        // model continues the text it was handed, so the deltas still join up into one answer.
        const auto delta = json["delta"];
        if (delta["type"].toString() == "text_delta")
            text_ << delta["text"].toString();
    }
    else if (type == "message_delta")
    {
        const auto reason = json["delta"]["stop_reason"].toString();
        if (reason.isNotEmpty())
            stopReason_ = reason;
    }
    else if (type == "message_stop")
        stopped = true;
    else if (type == "error")
        error_ = json["error"]["message"].toString().isNotEmpty() ? json["error"]["message"].toString() : data;
}

Outcome outcomeOf(const StreamCollector& stream)
{
    Outcome outcome;
    outcome.model = stream.model();
    if (stream.error().isNotEmpty())
    {
        outcome.errorTitle = "Claude meldet einen Fehler";
        outcome.parsed.error = stream.error();
        return outcome;
    }
    // A refusal can come after part of the answer; that part is no piece.
    if (stream.stopReason() == "refusal")
    {
        outcome.errorTitle = "Claude hat abgelehnt";
        outcome.parsed.error = de("Claude wollte dieses Stück nicht schreiben. Eine andere Beschreibung hilft vielleicht.");
        return outcome;
    }
    if (stream.stopReason() == "max_tokens")
    {
        outcome.errorTitle = "Zu lang";
        outcome.parsed.error = de("Das Stück passte nicht in eine Antwort. Wünsch dir ein kürzeres (weniger Takte oder Spuren).");
        return outcome;
    }
    if (!stream.finished() || stream.text().isEmpty())
    {
        outcome.errorTitle = "Abgebrochen";
        outcome.parsed.error = de("Die Verbindung brach ab, bevor das Stück fertig war.");
        return outcome;
    }
    outcome.parsed = parseArrangement(stream.text());
    if (!outcome.parsed.arrangement)
        outcome.errorTitle = de("Unbrauchbare Antwort");
    return outcome;
}

juce::String findApiKey(const juce::String& stored)
{
    if (stored.trim().isNotEmpty())
        return stored.trim();
    const auto environment = juce::SystemStats::getEnvironmentVariable("ANTHROPIC_API_KEY", {}).trim();
    if (environment.isNotEmpty())
        return environment;
    const auto file = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".config/tonwerk/anthropic.key");
    return file.existsAsFile() ? file.loadFileAsString().trim() : juce::String();
}

Outcome compose(const juce::String& apiKey, const juce::String& modelId, const juce::String& wish,
                const Arrangement* current, const std::function<void(int)>& progress, std::atomic<bool>& cancelled)
{
    Outcome outcome;
    juce::String headers;
    headers << "x-api-key: " << apiKey << "\r\n"
            << "anthropic-version: 2026-10-21\r\n"
            << "anthropic-beta: server-side-fallback-2026-07-01\r\n"
            << "content-type: application/json\r\n"
            << "accept: text/event-stream\r\n";
    int status = 0;
    juce::StringPairArray responseHeaders;
    // KOMPONIST_API_URL points the app at a stand-in for the API (the tests' fake server).
    auto address = juce::SystemStats::getEnvironmentVariable("KOMPONIST_API_URL", "https://api.anthropic.com/v1/messages");
    const auto url = juce::URL(address).withPOSTData(requestBody(modelId, wish, current));
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                             .withExtraHeaders(headers)
                             .withConnectionTimeoutMs(5 * 60 * 1000)
                             .withStatusCode(&status)
                             .withResponseHeaders(&responseHeaders)
                             .withHttpRequestCmd("POST");
    auto stream = url.createInputStream(options);
    if (stream == nullptr)
    {
        outcome.errorTitle = "Keine Verbindung";
        outcome.parsed.error = "api.anthropic.com antwortet nicht. Ist das Netz da?";
        return outcome;
    }
    if (status != 200)
    {
        const auto body = stream->readEntireStreamAsString();
        const auto message = juce::JSON::parse(body)["error"]["message"].toString();
        outcome.errorTitle = status == 401 ? de("API-Schlüssel abgelehnt") : "Fehler " + juce::String(status);
        outcome.parsed.error = message.isNotEmpty() ? message : body.substring(0, 300);
        return outcome;
    }
    StreamCollector collector;
    while (!stream->isExhausted() && !cancelled.load())
    {
        collector.line(stream->readNextLine());
        if (progress)
            progress(collector.text().length());
        if (collector.finished())
            break;
    }
    collector.finish();
    if (cancelled.load())
    {
        outcome.errorTitle = "Abgebrochen";
        outcome.parsed.error = "Du hast abgebrochen.";
        return outcome;
    }
    return outcomeOf(collector);
}
} // namespace komponist

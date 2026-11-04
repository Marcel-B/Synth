#pragma once

#include "Arrangement.h"

#include <atomic>
#include <functional>

// Asks Claude (the Messages API over HTTPS, streamed) for a piece as JSON in the shape of `schema()`, which structured
// outputs hold it to. There is no official C++ SDK, so the request is plain HTTP through juce::URL.
namespace komponist
{
struct Model
{
    const char* id;
    const char* label;
    bool serverFallback; // takes `fallbacks: "default"`, so a declined request runs again on another model
};

// The first is the default.
const std::vector<Model>& models();

juce::var schema();
juce::String systemPrompt();
// The user's wish; with `current`, a change to that piece (sent as its JSON, so the request stays one turn).
juce::String userMessage(const juce::String& wish, const Arrangement* current);
juce::String requestBody(const juce::String& modelId, const juce::String& wish, const Arrangement* current);

// Collects a server-sent event stream of the Messages API line by line: the text of every text block, the stop
// reason and an error event.
class StreamCollector
{
public:
    void line(const juce::String& line);
    void finish(); // a last event without a closing blank line

    const juce::String& text() const { return text_; }
    const juce::String& stopReason() const { return stopReason_; }
    const juce::String& error() const { return error_; }
    const juce::String& model() const { return model_; }
    bool finished() const { return stopped; }

private:
    void event();

    juce::String data, text_, stopReason_, error_, model_;
    bool stopped = false;
};

struct Outcome
{
    ParseResult parsed;
    juce::String model;      // the model that answered
    juce::String errorTitle; // empty when parsed holds the piece
};

// Turns a finished stream into a piece or a message for the user.
Outcome outcomeOf(const StreamCollector& stream);

// The API key: the app's own setting, else ANTHROPIC_API_KEY, else ~/.config/tonwerk/anthropic.key (the file
// Tonwerk-AI reads too).
juce::String findApiKey(const juce::String& stored);

// Runs the request on the calling thread (a background one); `progress` gets the characters received so far.
Outcome compose(const juce::String& apiKey, const juce::String& modelId, const juce::String& wish,
                const Arrangement* current, const std::function<void(int)>& progress, std::atomic<bool>& cancelled);
} // namespace komponist

#pragma once

#include "dsp/Patch.h"

#include <juce_core/juce_core.h>

namespace tonwerk
{
/**
 * A patch out of Tonwerk's JSON, made playable the way its `normalizePatch` does: anything missing or broken takes
 * the melody's default of the patch's engine, anything out of range is clamped. The engine the JSON does not name
 * keeps what `base` has, so importing an FM sound leaves the analog settings as they were.
 */
Patch patchFromJson(const juce::var& json, const Patch& base = {});

/** The patch's sound as Tonwerk stores it: the playing engine's fields and the effects, nothing of the other one. */
juce::var patchToJson(const Patch& patch);

/** A named sound, as a file holds it or Tonwerk's `GET /api/logic/synths/presets` lists it. */
struct NamedPatch
{
    juce::String name;
    juce::var patch;
};

/**
 * Every sound in a JSON text: Tonwerk's preset list (`[{ "name", "patch" }, …]`), one such entry, or a bare patch,
 * which takes `fallbackName`. Entries without a usable patch are left out.
 */
juce::Array<NamedPatch> namedPatchesFromJson(const juce::String& text, const juce::String& fallbackName);
} // namespace tonwerk

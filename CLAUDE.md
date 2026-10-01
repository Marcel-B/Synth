# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Tonwerk Synth: the two browser synthesizers of Tonwerk (repository `Marcel-B/YuE-UI`, `src/YueUI.Api/ClientApp/src/logic/synth.ts`, `fm.ts`, `envelope.ts`, `effects.ts`) as a JUCE instrument plugin for Logic Pro. Audio Unit (v2 component, the format Logic loads), VST3 and a standalone app from one CMake project. README.md (German) is the user documentation.

## Commands

Requires CMake 3.22+ and a C++20 compiler; JUCE 8 comes through `FetchContent` (pinned tag in `CMakeLists.txt`). On Linux, JUCE needs the packages listed in `.github/workflows/build.yml`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug     # add -DFETCHCONTENT_SOURCE_DIR_JUCE=<clone> to reuse a local JUCE
cmake --build build --target TonwerkSynthTests && ctest --test-dir build --output-on-failure
cmake --build build --target TonwerkSynth_Standalone      # also TonwerkSynth_VST3, TonwerkSynth_AU (macOS only)
scripts/install-macos.sh                                  # on the Mac: Release build, ad-hoc signing, install, auval
```

The Mac has only the Command Line Tools, no Xcode: keep the build on CMake's default generators, never require the Xcode generator, an AUv3 app extension or a signing identity. CI (`build.yml`) runs the tests on Linux and on macOS, builds the universal AU there, signs it ad hoc and runs `auval -strict -v aumu Twsy Bvlp`. Warnings come from `juce_recommended_warning_flags`; keep the build free of them.

## Architecture

- `src/dsp/` is plain C++ without JUCE: `Patch.h` (the patch as Tonwerk's JSON describes it, both engines plus effects, and the TX81Z algorithm table), `Defaults.cpp` (Tonwerk's starting sound per track kind), `Ranges.h` (every range, the same as Tonwerk's `RANGES`, `FM_RANGES`, `EFFECT_RANGES`), `Envelope.h`, `Oscillators.h` (PolyBLEP oscillators, LFO, Web Audio's biquad, noise), `AnalogVoice.h`, `FmVoice.h`.
- The aim is to sound like the browser. Each DSP class says in its comment which Web Audio behaviour it reproduces (the pulse built from two sawtooths is half as loud as the plain square, lowpass/highpass Q is decibels, the envelope's time constants, FM in Hz with `kMaxIndex` 8, feedback averaged over two samples, reverb level 0.15). When Tonwerk's synths change, change both, and keep `PatchJson.cpp` reading their JSON.
- `PatchJson.cpp` reads and writes Tonwerk's patch JSON the way its `normalizePatch` does (missing fields take the melody's default, ranges clamp, `engine` absent means analog). Importing one engine keeps the other's settings.
- `Parameters.cpp` has one table (`descriptors()`) of every patch field as a host parameter; layout, reading a `Patch` from the parameters each block (`ParameterReader`) and writing an imported patch (`writePatch`) all come from it. A new field is one entry there. Parameter ids are part of saved Logic projects: never rename or remove one; a new parameter gets version hint 2.
- `PluginProcessor.cpp`: a `juce::Synthesiser` with 16 `SynthVoice`s rendering mono into a buffer, then `EffectsChain` (delay, `juce::dsp::Convolution` reverb) into stereo. Voices read the shared patch every block, so knobs act on sounding notes. The reverb's impulse is made on the message thread (timer), never on the audio thread.
- `PresetLibrary.cpp`: factory sounds (the host's programs) and imported ones in `presets.json` under Application Support, plus Tonwerk's address in `settings.json`. `fetchFromTonwerk` calls Tonwerk's `GET /api/logic/synths/presets` (no login, reached over Tailscale).
- `PluginEditor.cpp`: sections of knobs per engine, the engine switch, the preset menu and the import/export buttons, an on-screen keyboard. Strings with umlauts go through `utf8()` / `String::fromUTF8`, since `juce::String` reads a plain `const char*` as ASCII.

## Conventions

- Commit messages and branch names are German; code and comments English; UI strings German.
- Comments explain why. Tests (`tests/`, JUCE `UnitTest`s run by `TonwerkSynthTests`) come with every change; DSP tests measure the sound (levels, duty cycle, envelope timing), not implementation details.
- Nothing here has been heard in Logic on Marcel's Mac yet unless a commit or the README says so; say what was and was not tested.

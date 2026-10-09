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

The Mac has only the Command Line Tools, no Xcode: keep the build on CMake's default generators, never require the Xcode generator, an AUv3 app extension or a signing identity. Releases: pushing a tag `v*` runs `release.yml` (or starting it by hand with a version, which then makes the tag; the cloud sessions cannot push tags), which builds with `-DTONWERK_VERSION` from the tag, signs ad hoc, runs auval and attaches a zip (plugins, app, `scripts/install-release.sh` as `install.sh`, which removes the quarantine flag) to a GitHub release. CI (`build.yml`) runs the tests on Linux and on macOS, builds the universal AU there, signs it ad hoc and runs `auval -strict -v aumu Twsy Bvlp`. Warnings come from `juce_recommended_warning_flags`; keep the build free of them.

## Architecture

- `src/dsp/` is plain C++ without JUCE: `Patch.h` (the patch as Tonwerk's JSON describes it, both engines plus effects, and the TX81Z algorithm table), `Defaults.cpp` (Tonwerk's starting sound per track kind), `Ranges.h` (every range, the same as Tonwerk's `RANGES`, `FM_RANGES`, `EFFECT_RANGES`), `Envelope.h`, `Oscillators.h` (PolyBLEP oscillators, LFO, Web Audio's biquad, noise), `AnalogVoice.h`, `FmVoice.h`.
- The aim is to sound like the browser. Each DSP class says in its comment which Web Audio behaviour it reproduces (the pulse built from two sawtooths is half as loud as the plain square, lowpass/highpass Q is decibels, the envelope's time constants, FM in Hz with `kMaxIndex` 8, feedback averaged over two samples, reverb level 0.15). When Tonwerk's synths change, change both, and keep `PatchJson.cpp` reading their JSON.
- `PatchJson.cpp` reads and writes Tonwerk's patch JSON the way its `normalizePatch` does (missing fields take the melody's default, ranges clamp, `engine` absent means analog). Importing one engine keeps the other's settings.
- `Parameters.cpp` has one table (`descriptors()`) of every patch field as a host parameter; layout, reading a `Patch` from the parameters each block (`ParameterReader`) and writing an imported patch (`writePatch`) all come from it. A new field is one entry there. Parameter ids are part of saved Logic projects: never rename or remove one, and append new ones with the version hint of the release they first appear in (2 for 0.2.0, 3 for 0.3.0).
- `PluginProcessor.cpp`: a `juce::Synthesiser` with 16 `SynthVoice`s rendering mono into a buffer, then `EffectsChain` (delay, `juce::dsp::Convolution` reverb) into stereo. Voices read the shared patch every block, so knobs act on sounding notes. The reverb's impulse is made on the message thread (timer), never on the audio thread.
- Wavefolder and sample and hold (`AnalogPatch::fold`, `sampleHold`; `Wavefolder`, `SampleHold` in `Oscillators.h`) are the plugin's own, not Tonwerk's: the folder sits between the oscillator mix and the filter, the sample and hold moves cutoff and pitch per voice. Both are off at their defaults, `patchToJson` writes them only when on, and a Tonwerk sound imports with them off.
- Tempo sync (`dsp/Tempo.h`, `withTempo`): the processor turns a synced LFO's or sample and hold's note value into a rate and a synced delay's into a time, from the play head's BPM (120 without one), before the voices and effects see the patch; the DSP knows only rates and times. Sync and division are this plugin's own fields; Tonwerk's JSON lacks them, `patchToJson` writes them only when sync is on.
- `ScopeBuffer` (processor) holds the last 4096 output samples as atomics; `ScopeView` in the editor reads them at 30 Hz and triggers on a rising zero crossing, as Tonwerk's `SynthScope.vue` does.
- `PresetLibrary.cpp`: factory sounds (the host's programs) and imported ones in `presets.json` under Application Support, plus Tonwerk's address in `settings.json`. `fetchFromTonwerk` calls Tonwerk's `GET /api/logic/synths/presets` (no login, reached over Tailscale).
- `PluginEditor.cpp`: sections of knobs per engine, the engine switch, the preset menu and the import/export buttons, an on-screen keyboard. Strings with umlauts go through `utf8()` / `String::fromUTF8`, since `juce::String` reads a plain `const char*` as ASCII.

## Chrome Glitch

A second plugin in the same CMake project: `ChromeGlitch` (audio effect, `aufx ChGl Bvlp`, sources in `glitch/src/`, tests in `tests/GlitchTests.cpp` inside `TonwerkSynthTests`). It is built, signed, validated with auval, installed and released alongside the synth. `GlitchEngine.h` is plain C++: ticks (grid lines of the host tempo in sync, random 40-250 ms gaps otherwise, the chaos share of those also in sync) roll for a stutter (replays the last slice, later repeats maybe pitched, never wrapping inside a repeat) or a dropout; every change crossfades over 1.5 ms; the crusher is full during a glitch and light between. The dice reseed on each transport start so bounces repeat. Its parameter ids follow the same never-rename rule; they start at version hint 1. The test app defines `CHROME_GLITCH_NO_ENTRY` because both plugins define `createPluginFilter`. The editor uses Cyberpunk 2077's colours (yellow, cyan, red on near black); its `GlitchMonitor` reads `glitching`, `glitchStarted` (set by the processor whenever `GlitchEngine::eventsStarted()` grows, taken back by the editor's 30 Hz timer, so a glitch shorter than a frame still shows) and `lastEvent`.

## Tonwerk Distortion

A third plugin in the same CMake project: `TonwerkDistortion` (audio effect, `aufx TwDs Bvlp`, sources in `distortion/src/`, tests in `tests/DistortionTests.cpp` inside `TonwerkSynthTests`), built, signed, validated, installed and released like the other two. `Ds1Circuit.h` is plain C++: BOSS DS-1's circuit with ElectroSmash's part values, in volts (1.0 = 1 V at the jack). `Drive` (booster, op-amp stage, rails, diode clipper from a Shockley table) runs 4x oversampled through `juce::dsp::Oversampling` (polyphase IIR) with first-order ADAA on both clippers; `Voice` (tone blend, level, DC block) runs at the host rate. The op-amp's slew rate is deliberately left out (it aliased more than all the clipping). Keep CPU low: the test logs the real-time factor. Parameter ids start at version hint 1; the test app defines `TONWERK_DISTORTION_NO_ENTRY`.

## Conventions

- Commit messages and branch names are German; code and comments English; UI strings German.
- Comments explain why. Tests (`tests/`, JUCE `UnitTest`s run by `TonwerkSynthTests`) come with every change; DSP tests measure the sound (levels, duty cycle, envelope timing), not implementation details.
- 0.2.0 runs in Logic on Marcel's MacBook (his screenshot, 2026-10-01); anything newer has not been heard there unless a commit or the README says so. Say what was and was not tested.

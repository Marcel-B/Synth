#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

/**
 * Tonwerk Arp: an arpeggiator and step sequencer that turns held keys into notes on a grid. Plain C++, so the tests
 * run it without a host.
 *
 * One step pattern drives both modes: each step can rest, move the note by semitones, set its velocity and tie over
 * from the step before. In the arpeggiator the steps that sound walk through the held keys in the chosen order; in
 * the sequencer every step plays the held keys together (one key: a bass line on that key; a chord: the chord in
 * that rhythm). While the host plays, steps count from the song's beat 0, so the pattern sits on its bars wherever
 * playback starts or loops; otherwise the grid starts on the first key.
 */
namespace tonwerkarp
{
inline constexpr int kSteps = 16;
inline constexpr int kMaxKeys = 32;

/** Menu orders are saved in projects: append only. */
enum class Mode
{
    arpeggio,
    sequence
};

enum class Order
{
    up,
    down,
    upDown,
    downUp,
    played,
    random
};

enum class Division
{
    whole,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    quarterTriplet,
    eighthTriplet,
    sixteenthTriplet,
    quarterDotted,
    eighthDotted,
    sixteenthDotted
};

enum class VelocitySource
{
    played,
    fixed
};

/** A step's length in beats (quarter notes). */
inline double divisionBeats(Division d)
{
    switch (d)
    {
        case Division::whole: return 4.0;
        case Division::half: return 2.0;
        case Division::quarter: return 1.0;
        case Division::eighth: return 0.5;
        case Division::sixteenth: return 0.25;
        case Division::thirtySecond: return 0.125;
        case Division::quarterTriplet: return 2.0 / 3.0;
        case Division::eighthTriplet: return 1.0 / 3.0;
        case Division::sixteenthTriplet: return 1.0 / 6.0;
        case Division::quarterDotted: return 1.5;
        case Division::eighthDotted: return 0.75;
        case Division::sixteenthDotted: return 0.375;
    }
    return 0.25;
}

struct Step
{
    bool on = true;
    /** Semitones added to the note. */
    int note = 0;
    /** 0 to 1, scales the velocity. */
    float velocity = 1.0f;
    /** Holds the step before through this one instead of playing a new note. */
    bool tie = false;
};

struct Settings
{
    Mode mode = Mode::arpeggio;
    Order order = Order::up;
    /** The arpeggio climbs this many octaves; the sequence moves up an octave each time round, up to this many. */
    int octaves = 1;
    Division division = Division::sixteenth;
    /** A note's length as a share of its step; over 1 the next note starts under it (legato on a mono synth). */
    float gate = 0.5f;
    /** Where the second step of each pair falls within the pair: 0.5 straight, 0.75 dotted (as the Groovebox). */
    float swing = 0.5f;
    int length = kSteps;
    /** Keys stay after they are let go, until a new chord starts with no key down. */
    bool latch = false;
    VelocitySource velocitySource = VelocitySource::played;
    int fixedVelocity = 100;
    int transpose = 0;
    std::array<Step, kSteps> steps {};
};

/** Where the host is: `ppq` in beats from the song's start, only meaningful while `playing`. */
struct Position
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
};

/** A key going down (`velocity` > 0) or up (0), `offset` samples into the block; `note` -1 for all notes off. */
struct KeyEvent
{
    int offset = 0;
    int note = 60;
    int velocity = 0;
    int channel = 1;
};

/**
 * The step clock: which steps start in a stretch of beats. Swing delays every second step; step `n` counts from beat
 * 0, so step 0 of a pattern `length` long falls on every multiple of `length` steps.
 */
struct Clock
{
    static double lateness(double step, float swing) { return (std::clamp((double) swing, 0.5, 0.75) - 0.5) * 2.0 * step; }

    static double stepStart(long long n, double step, double late)
    {
        return (double) n * step + ((n & 1) != 0 ? late : 0.0);
    }

    /** A step's sample, counted from where `base` beats fall, rounded to the nearest. */
    static long long sampleOf(double beat, double base, double samplesPerBeat)
    {
        return (long long) std::floor((beat - base) * samplesPerBeat + 0.5);
    }

    /**
     * Calls `fire(offset, n)` for each step `n` whose rounded sample lies in [from, to), in order; sample 0 is at
     * `base` beats. Deciding by the rounded sample, not the beat, puts a step on a block's seam in exactly one block.
     */
    template <typename Fire>
    static void steps(double base, int from, int to, double samplesPerBeat, double step, float swing, Fire&& fire)
    {
        const double late = lateness(step, swing);
        const double first = base + (from - 1) / samplesPerBeat, last = base + (to + 1) / samplesPerBeat;
        for (auto n = (long long) std::floor((first - late) / step) - 1; (double) n * step < last; ++n)
        {
            const auto at = sampleOf(stepStart(n, step, late), base, samplesPerBeat);
            if (at >= from && at < to)
                fire((int) at, n);
        }
    }
};

class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        reset();
    }

    /** Forgets keys and sounding notes without sending anything; for a fresh start. */
    void reset()
    {
        keyCount = 0;
        down.fill(false);
        ends.fill(-1);
        now = 0;
        freeOrigin = 0.0;
        wasPlaying = false;
        lastEnd = 0.0;
        lastStep = std::numeric_limits<long long>::min();
        arpIndex = 0;
        sequenceDirty = true;
        shownStep = -1;
        shownNote = -1;
        random = 0x9e3779b9u;
    }

    void update(const Settings& s)
    {
        const bool latchWasOn = settings.latch;
        if (s.order != settings.order || s.octaves != settings.octaves)
            sequenceDirty = true;
        settings = s;
        settings.length = std::clamp(settings.length, 1, kSteps);
        settings.octaves = std::clamp(settings.octaves, 1, 4);
        // Latch off: only the keys still down stay.
        if (latchWasOn && ! settings.latch)
        {
            int kept = 0;
            for (int i = 0; i < keyCount; ++i)
                if (down[(std::size_t) keys[(std::size_t) i].note])
                    keys[(std::size_t) kept++] = keys[(std::size_t) i];
            keyCount = kept;
            sequenceDirty = true;
        }
    }

    /** Ends every sounding note at `offset` and forgets the keys (MIDI's all notes off). */
    template <typename Emit>
    void panic(int offset, Emit&& emit)
    {
        endAll(offset, emit);
        keyCount = 0;
        down.fill(false);
        sequenceDirty = true;
    }

    /**
     * Runs `count` samples: the keys in `events` (sorted by offset) come in, the notes go out through
     * `emit(offset, note, velocity, channel)`, velocity 0 for a note's end, in time order.
     */
    template <typename Emit>
    void process(int count, const Position& position, const KeyEvent* events, int eventCount, Emit&& emit)
    {
        const double samplesPerBeat = rate * 60.0 / std::clamp(position.bpm, 20.0, 999.0);
        // The transport stopping ends the notes; the grid starts again on the keys.
        if (wasPlaying && ! position.playing)
        {
            endAll(0, emit);
            freeOrigin = 0.0;
            lastStep = std::numeric_limits<long long>::min();
        }
        if (position.playing && ! wasPlaying)
        {
            // A new take: the random order repeats from here, so a bounce sounds like playback.
            random = 0x9e3779b9u;
            arpIndex = 0;
        }
        double base = position.playing ? position.ppq : freeOrigin;
        // Hosts round the position; a block that starts within half a sample of where the last ended continues it,
        // so a step on the seam plays once.
        if (position.playing && wasPlaying && std::abs(base - lastEnd) < 0.5 / samplesPerBeat)
            base = lastEnd;
        else if (position.playing)
            lastStep = std::numeric_limits<long long>::min(); // a start, a jump or a loop: the count begins anew

        int from = 0;
        for (int e = 0; e < eventCount; ++e)
        {
            const auto& event = events[e];
            const int at = std::clamp(event.offset, from, count);
            run(from, at, base, samplesPerBeat, emit);
            from = at;
            if (event.note < 0)
                panic(at, emit);
            else if (key(event) && ! position.playing)
            {
                // The first key starts the grid at its own sample, so the first note sounds as it is played.
                base = -(double) at / samplesPerBeat;
                lastStep = std::numeric_limits<long long>::min();
            }
        }
        run(from, count, base, samplesPerBeat, emit);

        lastEnd = base + count / samplesPerBeat;
        if (! position.playing)
            freeOrigin = lastEnd;
        wasPlaying = position.playing;
        now += count;
    }

    /** The pattern's step that last sounded, -1 while nothing plays; for the editor. */
    int currentStep() const { return shownStep; }
    /** The last note started, -1 before any. */
    int lastNote() const { return shownNote; }
    int heldCount() const { return keyCount; }
    int heldNote(int i) const { return keys[(std::size_t) i].note; }

private:
    struct Key
    {
        int note;
        int velocity;
        long long order;
    };

    /** Takes a key in or out; true when it starts the arpeggio from no keys. */
    bool key(const KeyEvent& event)
    {
        const int note = std::clamp(event.note, 0, 127);
        if (event.velocity > 0)
        {
            channel = event.channel;
            const bool anyDown = std::find(down.begin(), down.end(), true) != down.end();
            const bool starts = keyCount == 0 || (settings.latch && ! anyDown);
            // With latch, a key pressed while none is down begins a new chord.
            if (settings.latch && ! anyDown)
                keyCount = 0;
            down[(std::size_t) note] = true;
            remove(note);
            if (keyCount < kMaxKeys)
                keys[(std::size_t) keyCount++] = { note, event.velocity, ++played };
            sequenceDirty = true;
            if (starts)
                arpIndex = 0;
            return starts;
        }
        down[(std::size_t) note] = false;
        if (! settings.latch)
        {
            remove(note);
            sequenceDirty = true;
        }
        return false;
    }

    void remove(int note)
    {
        int kept = 0;
        for (int i = 0; i < keyCount; ++i)
            if (keys[(std::size_t) i].note != note)
                keys[(std::size_t) kept++] = keys[(std::size_t) i];
        keyCount = kept;
    }

    template <typename Emit>
    void endAll(int offset, Emit&& emit)
    {
        for (int note = 0; note < 128; ++note)
            if (ends[(std::size_t) note] >= 0)
            {
                emit(offset, note, 0, channel);
                ends[(std::size_t) note] = -1;
            }
        shownStep = -1;
    }

    /** Ends the notes due before sample `until` (relative to the block). */
    template <typename Emit>
    void endDue(long long until, Emit&& emit)
    {
        // Several can end in one stretch; out in time order.
        for (;;)
        {
            int first = -1;
            for (int note = 0; note < 128; ++note)
                if (ends[(std::size_t) note] >= 0 && ends[(std::size_t) note] < now + until
                    && (first < 0 || ends[(std::size_t) note] < ends[(std::size_t) first]))
                    first = note;
            if (first < 0)
                return;
            emit((int) (ends[(std::size_t) first] - now), first, 0, channel);
            ends[(std::size_t) first] = -1;
        }
    }

    template <typename Emit>
    void run(int from, int to, double base, double samplesPerBeat, Emit&& emit)
    {
        if (to <= from)
            return;
        const double step = divisionBeats(settings.division);
        Clock::steps(base, from, to, samplesPerBeat, step, settings.swing, [&](int offset, long long n) {
            // Hosts round their position; a step the last block played is not played again.
            if (n <= lastStep)
                return;
            lastStep = n;
            endDue(offset + 1, emit);
            fire(offset, n, step, samplesPerBeat, emit);
        });
        endDue(to, emit);
    }

    void buildSequence()
    {
        sequenceDirty = false;
        std::array<Key, kMaxKeys> sorted {};
        std::copy(keys.begin(), keys.begin() + keyCount, sorted.begin());
        if (settings.order == Order::played)
            std::sort(sorted.begin(), sorted.begin() + keyCount, [](const Key& a, const Key& b) { return a.order < b.order; });
        else
            std::sort(sorted.begin(), sorted.begin() + keyCount, [](const Key& a, const Key& b) { return a.note < b.note; });
        int n = 0;
        for (int o = 0; o < settings.octaves; ++o)
            for (int i = 0; i < keyCount; ++i)
                sequence[(std::size_t) n++] = { sorted[(std::size_t) i].note + 12 * o, sorted[(std::size_t) i].velocity, 0 };
        if (settings.order == Order::down)
            std::reverse(sequence.begin(), sequence.begin() + n);
        // Up and down turn at the ends without playing the top or bottom note twice.
        if ((settings.order == Order::upDown || settings.order == Order::downUp) && n > 2)
        {
            if (settings.order == Order::downUp)
                std::reverse(sequence.begin(), sequence.begin() + n);
            for (int i = n - 2; i >= 1; --i)
                sequence[(std::size_t) (2 * n - 2 - i)] = sequence[(std::size_t) i];
            n = 2 * n - 2;
        }
        else if (settings.order == Order::downUp)
            std::reverse(sequence.begin(), sequence.begin() + n);
        sequenceLength = n;
    }

    unsigned nextRandom()
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    }

    int velocityOf(int keyVelocity, float stepVelocity) const
    {
        const int source = settings.velocitySource == VelocitySource::played ? keyVelocity : settings.fixedVelocity;
        return std::clamp((int) std::lround((double) source * stepVelocity), 1, 127);
    }

    template <typename Emit>
    void start(int offset, int note, int velocity, long long length, Emit&& emit)
    {
        if (note < 0 || note > 127)
            return;
        // The same note still sounding ends first, so its end does not cut the new one.
        if (ends[(std::size_t) note] >= 0)
            emit(offset, note, 0, channel);
        emit(offset, note, velocity, channel);
        ends[(std::size_t) note] = now + offset + std::max(1LL, length);
        shownNote = note;
    }

    template <typename Emit>
    void fire(int offset, long long n, double step, double samplesPerBeat, Emit&& emit)
    {
        const int length = settings.length;
        const int index = (int) (((n % length) + length) % length);
        if (keyCount == 0)
        {
            shownStep = -1;
            return;
        }
        shownStep = index;
        const auto& s = settings.steps[(std::size_t) index];
        if (! s.on)
            return;

        // A note held into a tied step reaches to the next one; the gate ends the last of a tie.
        const double late = Clock::lateness(step, settings.swing);
        const auto& next = settings.steps[(std::size_t) ((index + 1) % length)];
        const double toNext = Clock::stepStart(n + 1, step, late) - Clock::stepStart(n, step, late);
        const double beats = next.on && next.tie ? toNext + 0.05 * step : settings.gate * toNext;
        const auto samples = (long long) std::llround(beats * samplesPerBeat);

        if (s.tie)
        {
            bool held = false;
            for (auto& end : ends)
                if (end >= now + offset)
                {
                    end = now + offset + std::max(1LL, samples);
                    held = true;
                }
            if (held)
                return;
        }

        if (sequenceDirty)
            buildSequence();
        const int shift = s.note + settings.transpose;
        if (settings.mode == Mode::sequence)
        {
            // Each time round the pattern climbs an octave, up to the octaves set.
            const long long round = (n >= 0 ? n : n - length + 1) / length;
            const int octave = (int) (((round % settings.octaves) + settings.octaves) % settings.octaves) * 12;
            for (int i = 0; i < keyCount; ++i)
                start(offset, keys[(std::size_t) i].note + shift + octave,
                      velocityOf(keys[(std::size_t) i].velocity, s.velocity), samples, emit);
            return;
        }

        int at;
        if (settings.order == Order::random)
        {
            at = (int) (nextRandom() % (unsigned) sequenceLength);
            // No note twice in a row when there is a choice: that would read as a stuck key.
            if (sequenceLength > 1 && sequence[(std::size_t) at].note + shift == shownNote)
                at = (at + 1 + (int) (nextRandom() % (unsigned) (sequenceLength - 1))) % sequenceLength;
        }
        else
            at = (int) (arpIndex % sequenceLength);
        ++arpIndex;
        const auto& picked = sequence[(std::size_t) at];
        start(offset, picked.note + shift, velocityOf(picked.velocity, s.velocity), samples, emit);
    }

    double rate = 48000.0;
    Settings settings;
    std::array<Key, kMaxKeys> keys {};
    int keyCount = 0;
    long long played = 0;
    std::array<bool, 128> down {};
    /** When each sounding note ends, in samples since the start; -1 for silent. */
    std::array<long long, 128> ends {};
    std::array<Key, kMaxKeys * 4 * 2> sequence {};
    int sequenceLength = 0;
    bool sequenceDirty = true;
    long long arpIndex = 0;
    long long now = 0;
    double freeOrigin = 0.0;
    bool wasPlaying = false;
    double lastEnd = 0.0;
    /** The last step played, so a step is never played twice. */
    long long lastStep = std::numeric_limits<long long>::min();
    int channel = 1;
    int shownStep = -1;
    int shownNote = -1;
    unsigned random = 0x9e3779b9u;
};
} // namespace tonwerkarp

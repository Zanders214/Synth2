#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "RtSafety.h"

/**
    Arp

    The arpeggiator: while keys are held, it plays a pattern drawn on a grid
    instead of the keys themselves, one step at a time, in time with the
    host (or on its own clock, at the host's last tempo, when the host isn't
    playing).

    A pattern doesn't hold notes but places in the chord: row 0 is the
    lowest key held, row 1 the next one up, and so on, climbing on into the
    octaves above once the chord runs out (a triad's row 3 is its lowest
    note an octave up). So one pattern plays any chord, and follows the
    hands as they change it.

    Each step plays any of the rows (none is a rest), at its own velocity
    (times the velocity its key was played with), for its own length: short,
    half or most of the step, or tied, held until the next step and, if that
    plays the same note, on through it without starting again. SWING makes
    every second step late, by up to half a step. LATCH keeps the chord
    playing after the keys go up, until the next one is pressed. The sustain
    pedal holds the keys let go while it is down, as it would without the arp.

    Kept free of the plugin (juce_audio_basics only), like the engine, so
    the tests can play it on its own.
*/
namespace Arp
{
    inline constexpr int maxSteps = 16;
    inline constexpr int numRows  = 8;

    /** The room a block's MIDI is given (MidiBuffer::ensureSize) so that
        gathering it never allocates; addIfRoom leaves out what won't fit. */
    inline constexpr int midiRoom = 32 * 1024;

    inline void addIfRoom (juce::MidiBuffer& midi, const juce::uint8* data, int numBytes, int samplePosition) noexcept
    {
        constexpr int header = (int) (sizeof (std::int32_t) + sizeof (std::uint16_t));
        if (midi.data.size() + numBytes + header <= midiRoom)
            midi.addEvent (data, numBytes, samplePosition);
    }

    /** How long a step's notes sound: part of the step, or tied into the next. */
    enum class Length { shortNote, half, longNote, tie };
    inline constexpr int numLengths = 4;
    inline constexpr std::array<const char*, numLengths> lengthNames { "Short", "Half", "Long", "Tie" };

    /** The part of its step a note sounds for (a tie, all of it and on). */
    inline constexpr double gateOf (Length length) noexcept
    {
        switch (length)
        {
            case Length::shortNote: return 0.25;
            case Length::half:      return 0.5;
            case Length::longNote:  return 0.9;
            case Length::tie:       break;
        }
        return 1.0;
    }

    struct Step
    {
        std::uint8_t rows     = 0;      // bit r: row r plays
        float        velocity = 0.8f;   // 0..1
        Length       length   = Length::half;

        bool plays (int row) const noexcept { return ((rows >> row) & 1u) != 0; }
        bool isRest() const noexcept        { return rows == 0; }

        bool operator== (const Step& other) const noexcept
        {
            return rows == other.rows && length == other.length
                && juce::roundToInt (velocity * 127.0f) == juce::roundToInt (other.velocity * 127.0f);
        }
        bool operator!= (const Step& other) const noexcept { return ! operator== (other); }
    };

    /** A step in 32 bits, to hand to the audio thread in an atomic: its
        rows, then its velocity in MIDI's steps, then its length. */
    inline std::uint32_t encode (const Step& step) noexcept
    {
        const auto velocity = (std::uint32_t) juce::jlimit (1, 127, juce::roundToInt (step.velocity * 127.0f));
        return (std::uint32_t) step.rows | (velocity << 8) | ((std::uint32_t) step.length << 15);
    }

    inline Step decode (std::uint32_t bits) noexcept
    {
        Step step;
        step.rows     = (std::uint8_t) (bits & 0xffu);
        step.velocity = (float) juce::jlimit (1u, 127u, (bits >> 8) & 0x7fu) / 127.0f;
        step.length   = (Length) ((bits >> 15) & 0x3u);
        return step;
    }

    struct Pattern
    {
        std::array<Step, maxSteps> steps {};
        int length = 8;   // how many of the steps play, 1..maxSteps

        const Step& operator[] (int index) const noexcept { return steps[(size_t) index]; }
        Step&       operator[] (int index) noexcept       { return steps[(size_t) index]; }

        bool operator== (const Pattern& other) const noexcept { return length == other.length && steps == other.steps; }
        bool operator!= (const Pattern& other) const noexcept { return ! operator== (other); }
    };

    /** The step lengths RATE can be, in beats (a quarter note). */
    struct Rate
    {
        const char* name;
        double      beats;
    };
    inline constexpr std::array<Rate, 8> rates {{
        { "1/4",   1.0 },
        { "1/8D",  0.75 },
        { "1/8",   0.5 },
        { "1/8T",  1.0 / 3.0 },
        { "1/16D", 0.375 },
        { "1/16",  0.25 },
        { "1/16T", 1.0 / 6.0 },
        { "1/32",  0.125 } }};
    inline constexpr int sixteenth = 5;   // RATE to start with

    // ---- Patterns to start from -------------------------------------------------

    /** The patterns a click fills the grid with. */
    enum class Shape { up, down, upDown, random, chord };
    inline constexpr std::array<const char*, 5> shapeNames { "Up", "Down", "Up-Down", "Random", "Chord" };

    // A step playing just row `row`.
    inline Step single (int row, float velocity, Length length = Length::half) noexcept
    {
        return { (std::uint8_t) (1u << juce::jlimit (0, numRows - 1, row)), velocity, length };
    }

    /** `shape` over `length` steps, climbing (or falling) through the chord's
        four lowest places (a triad's root again on top), the first of each
        four accented. Random picks a row anywhere in the grid's lower
        six, with the odd rest and accent; Chord stabs the three lowest
        together on an off-beat rhythm. */
    inline Pattern make (Shape shape, int length, juce::Random& random)
    {
        Pattern pattern;
        pattern.length = juce::jlimit (1, maxSteps, length);
        constexpr std::array<int, 6> upDown { 0, 1, 2, 3, 2, 1 };
        constexpr std::array<bool, 16> stabs { true, false, false, true, false, false, true, false,
                                               false, false, true, false, true, false, false, false };
        for (int i = 0; i < maxSteps; ++i)
        {
            const float accent = i % 4 == 0 ? 1.0f : 0.7f;
            switch (shape)
            {
                case Shape::up:     pattern[i] = single (i % 4, accent); break;
                case Shape::down:   pattern[i] = single (3 - i % 4, accent); break;
                case Shape::upDown: pattern[i] = single (upDown[(size_t) (i % 6)], accent); break;
                case Shape::random:
                    pattern[i] = random.nextInt (8) == 0 ? Step {} : single (random.nextInt (6), random.nextInt (3) == 0 ? 1.0f : 0.65f,
                                                                             random.nextInt (4) == 0 ? Length::longNote : Length::half);
                    break;
                case Shape::chord:
                    pattern[i] = stabs[(size_t) i] ? Step { 0b111, accent, Length::shortNote } : Step {};
                    break;
            }
        }
        return pattern;
    }

    /** The pattern a new instance starts with: up through the chord, in eighths of a bar. */
    inline Pattern defaultPattern()
    {
        juce::Random unused;
        return make (Shape::up, 8, unused);
    }

    /** `pattern` with a step or two changed, for a new idea close to it: a
        note moved up or down a row, a rest filled or a note taken out, an
        accent moved, or a note held longer or cut shorter. Always changes
        something. */
    inline Pattern vary (const Pattern& pattern, juce::Random& random)
    {
        auto varied = pattern;
        const int changes = 1 + random.nextInt (2);
        for (int change = 0; change < changes || varied == pattern; ++change)
        {
            auto& step = varied[random.nextInt (varied.length)];
            switch (random.nextInt (4))
            {
                case 0:   // a note up or down a row
                    if (! step.isRest())
                    {
                        const bool up = (step.rows & 0x80u) == 0 && (random.nextBool() || (step.rows & 1u) != 0);
                        step.rows = (std::uint8_t) (up ? step.rows << 1 : step.rows >> 1);
                        break;
                    }
                    [[fallthrough]];
                case 1:   // a rest filled, or a note taken out
                    step = step.isRest() ? single (random.nextInt (5), 0.75f) : Step {};
                    break;
                case 2:   // an accent
                    step.velocity = step.velocity > 0.85f ? 0.65f : 1.0f;
                    break;
                default:  // longer or shorter
                    step.length = (Length) ((((int) step.length) + 1 + random.nextInt (numLengths - 1)) % numLengths);
                    break;
            }
        }
        return varied;
    }

    // ---- Playing ------------------------------------------------------------------

    struct Settings
    {
        bool    on    = false;
        int     rate  = sixteenth;   // into rates
        float   swing = 0.0f;        // 0..1: every second step late by up to half a step
        bool    latch = false;
        Pattern pattern {};
    };

    /** Where the host is: its tempo, and, while it plays, the beat it is on
        at the block's start. */
    struct Transport
    {
        double bpm     = 120.0;
        bool   playing = false;
        double ppq     = 0.0;
    };

    /** Turns the keys played into the pattern's notes, block by block.
        Audio thread, but for prepare(). */
    class Player
    {
    public:
        static constexpr int outputChannel = 1;

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;
            reset();
        }

        /** Forgets every key and stops, without a note-off for what sounds. */
        void reset() noexcept
        {
            held = {};
            latched = {};
            pedals = 0;
            sounding = {};
            running = false;
            wasOn = false;
            wasLatched = false;
            physicallyDown = 0;
            publish (-1);
        }

        /** Plays the block's MIDI, `in`, into `out` (cleared first): with the
            arp off, as it is; with it on, the keys become the pattern's notes
            on channel 1, and the other messages pass through. `out` needs
            midiRoom set aside for this not to allocate. */
        void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples,
                      const Settings& settings, const Transport& transport) noexcept SYNTH2_RT_NONBLOCKING
        {
            out.clear();
            target  = &out;
            current = &settings;
            const double beats = rates[(size_t) juce::jlimit (0, (int) rates.size() - 1, settings.rate)].beats;
            stepSamples = sampleRate * 60.0 / juce::jlimit (20.0, 999.0, transport.bpm) * beats;
            blockPhase  = phase;
            if (transport.playing)
                followTransport (transport.ppq / beats);
            else
                hostPlaying = false;

            switchLatch (settings.on && settings.latch);
            switchOnOrOff (settings.on);

            int done = 0;
            for (const auto event : in)
            {
                const int when = juce::jlimit (done, juce::jmax (done, numSamples - 1), event.samplePosition);
                if (settings.on)
                    playUntil (done, when);
                handle (event.data, event.numBytes, when);
                done = when;
            }
            if (settings.on)
                playUntil (done, numSamples);

            phase = blockPhase + (double) numSamples / stepSamples;
            target = nullptr;
            current = nullptr;
        }

        /** The pattern's step playing (-1 for none), for the grid's playhead. Any thread. */
        int playingStep() const noexcept { return shownStep.load (std::memory_order_relaxed); }

        /** The note each row plays with the keys held now (-1 past the
            notes), for the grid's labels; all -1 with no keys. Any thread. */
        std::array<int, numRows> rowNotes() const noexcept
        {
            const auto packed = shownRows.load (std::memory_order_relaxed);
            std::array<int, numRows> notes {};
            for (int r = 0; r < numRows; ++r)
            {
                const auto note = (int) ((packed >> (8 * r)) & 0xffu);
                notes[(size_t) r] = note == 0xff ? -1 : note;
            }
            return notes;
        }

    private:
        static constexpr int maxKeys  = 32;
        static constexpr int maxNotes = 24;

        struct Key
        {
            int   note      = -1;    // -1: an empty slot
            int   channel   = 1;
            float velocity  = 0.0f;
            bool  sustained = false; // let go while the pedal was down
        };
        using Keys = std::array<Key, maxKeys>;

        struct Note
        {
            int    pitch = -1;       // -1: an empty slot
            double offAt = 0.0;      // in steps
            bool   tied  = false;    // held on into the next step instead
        };

        // The chord as the rows see it: unique notes, lowest first, each with
        // the velocity of the key that played it.
        struct Chord
        {
            std::array<int, maxKeys>   notes {};
            std::array<float, maxKeys> velocities {};
            int size = 0;

            int pitchOf (int row) const noexcept
            {
                return notes[(size_t) (row % size)] + 12 * (row / size);
            }
        };

        // ---- Keys ----

        void handle (const juce::uint8* data, int numBytes, int when) noexcept
        {
            const int status  = numBytes > 0 ? (data[0] & 0xf0) : 0;
            const int channel = numBytes > 0 ? (data[0] & 0x0f) + 1 : 1;
            const int first   = numBytes > 1 ? data[1] & 0x7f : 0;
            const int second  = numBytes > 2 ? data[2] & 0x7f : 0;
            const bool on     = current->on;

            if (status == 0x90 && second > 0)
            {
                press (channel, first, (float) second / 127.0f, when);
                if (on)
                    return;
            }
            else if (status == 0x80 || status == 0x90)
            {
                lift (channel, first, when);
                if (on)
                    return;
            }
            else if (status == 0xb0 && first == 64)
            {
                setPedal (channel, second >= 64, when);
                if (on)
                    return;
            }
            else if (status == 0xb0 && (first == 120 || first == 123) && on)
            {
                stop (when);
                held = {};
                latched = {};
                physicallyDown = 0;
            }
            add (data, numBytes, when);
        }

        void press (int channel, int note, float velocity, int when) noexcept
        {
            if (current->on && current->latch && physicallyDown == 0)
                latched = {};   // a new chord to latch
            ++physicallyDown;
            put (held, { note, channel, velocity, false });
            if (current->on && current->latch)
                put (latched, { note, channel, velocity, false });
            if (current->on && ! running)
                start (when);
        }

        void lift (int channel, int note, int when) noexcept
        {
            for (auto& key : held)
                if (key.note == note && key.channel == channel && ! key.sustained)
                {
                    physicallyDown = juce::jmax (0, physicallyDown - 1);
                    if (pedalDown (channel))
                        key.sustained = true;
                    else
                        key = {};
                }
            stopIfNoChord (when);
        }

        void setPedal (int channel, bool down, int when) noexcept
        {
            const auto bit = 1u << (channel - 1);
            pedals = down ? (pedals | bit) : (pedals & ~bit);
            if (down)
                return;
            for (auto& key : held)
                if (key.sustained && key.channel == channel)
                    key = {};
            stopIfNoChord (when);
        }

        bool pedalDown (int channel) const noexcept { return (pedals & (1u << (channel - 1))) != 0; }

        static void put (Keys& keys, const Key& key) noexcept
        {
            for (auto& slot : keys)
                if (slot.note == key.note && slot.channel == key.channel)
                {
                    slot = key;
                    return;
                }
            for (auto& slot : keys)
                if (slot.note < 0)
                {
                    slot = key;
                    return;
                }
        }

        Chord chord() const noexcept
        {
            Chord chord;
            const auto& keys = current->latch ? latched : held;
            for (const auto& key : keys)
            {
                if (key.note < 0)
                    continue;
                const auto end = chord.notes.begin() + chord.size;
                if (std::find (chord.notes.begin(), end, key.note) != end)
                    continue;
                int i = chord.size++;
                for (; i > 0 && chord.notes[(size_t) i - 1] > key.note; --i)
                {
                    chord.notes[(size_t) i]      = chord.notes[(size_t) i - 1];
                    chord.velocities[(size_t) i] = chord.velocities[(size_t) i - 1];
                }
                chord.notes[(size_t) i]      = key.note;
                chord.velocities[(size_t) i] = key.velocity;
            }
            return chord;
        }

        // LATCH switched on, the keys held are the chord it keeps; switched
        // off, the chord is the keys held again (none: the pattern stops).
        void switchLatch (bool latch) noexcept
        {
            if (latch == wasLatched)
                return;
            wasLatched = latch;
            latched = {};
            if (latch)
            {
                for (const auto& key : held)
                    if (key.note >= 0 && ! key.sustained)
                        put (latched, key);
                return;
            }
            stopIfNoChord (0);
        }

        void stopIfNoChord (int when) noexcept
        {
            if (current->on && running && chord().size == 0)
                stop (when);
        }

        // ---- The arp switched on or off ----

        // Switched on, the keys sounding by themselves stop (and the pedal
        // lifts, the arp holding them now) and the pattern takes over;
        // switched off, the pattern stops and the keys held sound again.
        void switchOnOrOff (bool on) noexcept
        {
            if (on == wasOn)
                return;
            wasOn = on;
            if (on)
            {
                for (const auto& key : held)
                    if (key.note >= 0)
                        add (juce::MidiMessage::noteOff (key.channel, key.note), 0);
                for (int channel = 1; channel <= 16; ++channel)
                    if (pedalDown (channel))
                        add (juce::MidiMessage::controllerEvent (channel, 64, 0), 0);
                if (chord().size > 0)
                    start (0);
                return;
            }

            stop (0);
            for (int channel = 1; channel <= 16; ++channel)
                if (pedalDown (channel))
                    add (juce::MidiMessage::controllerEvent (channel, 64, 127), 0);
            for (const auto& key : held)
                if (key.note >= 0)
                {
                    add (juce::MidiMessage::noteOn (key.channel, key.note, key.velocity), 0);
                    if (key.sustained)
                        add (juce::MidiMessage::noteOff (key.channel, key.note), 0);
                }
        }

        // ---- The pattern ----

        // Where step `k` (counted from the pattern's start) begins, in steps.
        double startOf (std::int64_t k) const noexcept
        {
            return (double) k + ((k & 1) != 0 ? 0.5 * (double) juce::jlimit (0.0f, 1.0f, current->swing) : 0.0);
        }

        double phaseAt (int sample) const noexcept { return blockPhase + (double) sample / stepSamples; }

        // The first key of a chord: the step it lands in plays at once.
        void start (int when) noexcept
        {
            running = true;
            if (! hostPlaying)
            {
                // On its own clock, the pattern starts afresh here.
                blockPhase = -(double) when / stepSamples;
                nextStep = 0;
                return;
            }
            nextStep = (std::int64_t) std::floor (phaseAt (when));
        }

        void stop (int when) noexcept
        {
            for (auto& note : sounding)
                if (note.pitch >= 0)
                {
                    add (juce::MidiMessage::noteOff (outputChannel, note.pitch), when);
                    note = {};
                }
            running = false;
            publish (-1);
        }

        // While the host plays, the steps fall on its beats: the pattern
        // starts on the bar's first, and a jump (a loop, a click on the
        // timeline) carries on from where the host went, the step under
        // the playhead playing if the jump landed near its start. (A drift
        // smaller than that, a change of tempo, is just followed.)
        void followTransport (double hostPhase) noexcept
        {
            if (running && (! hostPlaying || std::abs (hostPhase - blockPhase) > 0.25))
            {
                const auto k = (std::int64_t) std::floor (hostPhase);
                nextStep = hostPhase - startOf (k) < 0.1 ? k : k + 1;
            }
            // The notes sounding end when they would have.
            for (auto& note : sounding)
                if (note.pitch >= 0 && ! note.tied)
                    note.offAt += hostPhase - blockPhase;
            blockPhase  = hostPhase;
            hostPlaying = true;
        }

        void playUntil (int from, int to) noexcept
        {
            if (! current->on)
                return;
            for (;;)
            {
                bool   ending = false;
                double offAt  = 0.0;
                for (const auto& note : sounding)
                    if (note.pitch >= 0 && ! note.tied && (! ending || note.offAt < offAt))
                    {
                        ending = true;
                        offAt  = note.offAt;
                    }
                if (! ending && ! running)
                    return;
                const double stepAt = running ? startOf (nextStep) : offAt + 1.0;
                const double at = ending ? juce::jmin (offAt, stepAt) : stepAt;
                const double sample = (at - blockPhase) * stepSamples + 1.0e-6;   // (on the sample, not just before it)
                if (sample >= (double) to)
                    return;
                const int when = juce::jlimit (from, juce::jmax (from, to - 1), (int) sample);
                if (ending && offAt <= stepAt)
                    endNotesDue (offAt, when);
                else
                    playStep (when);
            }
        }

        void endNotesDue (double at, int when) noexcept
        {
            for (auto& note : sounding)
                if (note.pitch >= 0 && ! note.tied && note.offAt <= at)
                {
                    add (juce::MidiMessage::noteOff (outputChannel, note.pitch), when);
                    note = {};
                }
        }

        void playStep (int when) noexcept
        {
            const auto& pattern = current->pattern;
            const int   length  = juce::jlimit (1, maxSteps, pattern.length);
            const auto  k       = nextStep++;
            const auto& step    = pattern[(int) (k % length)];
            const auto  heard   = chord();
            const double from   = startOf (k);
            const bool   tied   = step.length == Length::tie;
            const double offAt  = from + gateOf (step.length) * (startOf (k + 1) - from);

            // Tied notes run on into this step if it plays them too, and end here if not.
            std::array<int, numRows> pitches {};
            pitches.fill (-1);
            if (heard.size > 0)
                for (int r = 0; r < numRows; ++r)
                    if (step.plays (r) && heard.pitchOf (r) <= 127)
                        pitches[(size_t) r] = heard.pitchOf (r);
            for (auto& note : sounding)
                if (note.pitch >= 0 && note.tied)
                {
                    if (std::find (pitches.begin(), pitches.end(), note.pitch) == pitches.end())
                    {
                        add (juce::MidiMessage::noteOff (outputChannel, note.pitch), when);
                        note = {};
                    }
                    else
                    {
                        note.offAt = offAt;
                        note.tied  = tied;
                    }
                }

            for (int r = 0; r < numRows; ++r)
            {
                const int pitch = pitches[(size_t) r];
                if (pitch < 0 || isSounding (pitch))
                    continue;
                for (auto& note : sounding)
                    if (note.pitch < 0)
                    {
                        const float velocity = juce::jlimit (1.0f / 127.0f, 1.0f, step.velocity * heard.velocities[(size_t) (r % heard.size)]);
                        add (juce::MidiMessage::noteOn (outputChannel, pitch, velocity), when);
                        note = { pitch, offAt, tied };
                        break;
                    }
            }
            publish ((int) (k % length), heard);
        }

        bool isSounding (int pitch) const noexcept
        {
            return std::any_of (sounding.begin(), sounding.end(), [pitch] (const Note& note) { return note.pitch == pitch; });
        }

        // ---- Out ----

        void add (const juce::MidiMessage& message, int when) noexcept
        {
            add (message.getRawData(), message.getRawDataSize(), when);
        }

        void add (const juce::uint8* data, int numBytes, int when) noexcept
        {
            addIfRoom (*target, data, numBytes, when);
        }

        void publish (int step) noexcept { publish (step, Chord()); }

        void publish (int step, const Chord& heard) noexcept
        {
            shownStep.store (step, std::memory_order_relaxed);
            std::uint64_t packed = ~std::uint64_t {};
            if (step >= 0 && heard.size > 0)
                for (int r = 0; r < numRows; ++r)
                    if (const int pitch = heard.pitchOf (r); pitch <= 127)
                        packed = (packed & ~(std::uint64_t { 0xff } << (8 * r))) | ((std::uint64_t) pitch << (8 * r));
            shownRows.store (packed, std::memory_order_relaxed);
        }

        double sampleRate  = 44100.0;
        double stepSamples = 5512.5;
        double phase       = 0.0;      // in steps, at the start of the next block
        double blockPhase  = 0.0;      // and of this one
        std::int64_t nextStep = 0;     // the next step to play, counted from the pattern's start
        bool   running     = false;    // playing the pattern (a chord is held, or latched)
        bool   hostPlaying = false;    // following the host's beats
        bool   wasOn       = false;
        bool   wasLatched  = false;
        int    physicallyDown = 0;     // keys down now, the pedal aside
        std::uint32_t pedals = 0;      // bit c: channel c + 1's sustain pedal is down
        Keys   held {};
        Keys   latched {};
        std::array<Note, maxNotes> sounding {};

        const Settings*   current = nullptr;
        juce::MidiBuffer* target  = nullptr;

        std::atomic<int>           shownStep { -1 };
        std::atomic<std::uint64_t> shownRows { ~std::uint64_t {} };
    };
}

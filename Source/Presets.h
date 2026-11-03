#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "ArpModel.h"
#include "Bench.h"
#include "EffectSettings.h"
#include "ParamIDs.h"
#include "Rack.h"

/**
    Presets

    Synth2's factory sounds. Each one says which oscillators are on the bench
    and how they are set: each with its own unison stack, and most with their
    own DAHDSR envelope, some through their own swept filter, and the effects
    it goes through (the pads and keys mostly). Everything a preset doesn't
    mention goes back to its default, so it sounds the same whatever was
    playing before it. Loading one sets the bench out afresh: its oscillators
    along the top row, each one's filter under it and its envelope under
    that (FILTER n and ENV n take OSC n), in a fixed order with no cables; and
    the EFFECTS tab too: just its effects, side by side, the drive, then the
    delay, then the reverb. A preset also says how its notes play: its
    GLIDE, whether it is MONO, and whether the arp plays them, and how. One
    without an arp switches it off but leaves its pattern as it was drawn,
    to try with the next sound.

    Each preset belongs to a category, which the preset tray groups them by;
    their program numbers stay in the order they were added, whatever their
    category, so hosts' saved programs keep finding them.

    The first, Empty Bench, is the bench a new instance opens on.
*/
namespace Presets
{
    /** An oscillator's unison stack: how many copies play each note, how far
        apart they are tuned and how wide they spread. */
    struct Unison
    {
        int   voices      = 1;
        float detuneCents = 15.0f;
        float width       = 0.5f;
    };

    /** A DAHDSR envelope (see Envelope.h): its times in seconds, its
        sustain level, 0..1, and how its ramps bend (-1..1, 0 straight). */
    struct Envelope
    {
        float delaySeconds   = 0.0f;
        float attackSeconds  = 0.005f;
        float holdSeconds    = 0.0f;
        float decaySeconds   = 0.3f;
        float sustainLevel   = 1.0f;
        float releaseSeconds = 0.3f;
        float attackCurve    = 0.0f;
        float decayCurve     = 0.0f;
        float releaseCurve   = 0.0f;
    };

    /** An envelope that fades in and stays up while the key is held. */
    inline Envelope ar (float attack, float release) { return { 0.0f, attack, 0.0f, 0.3f, 1.0f, release }; }

    /** One that falls to `sustain` after its attack. */
    inline Envelope adsr (float attack, float decay, float sustain, float release) { return { 0.0f, attack, 0.0f, decay, sustain, release }; }

    /** A filter (see Filter.h): its type (Filter::Type's order), its cutoff
        in Hz, its RESONANCE, how far its sweep moves the cutoff, in
        semitones, and the sweep itself. */
    struct Filter
    {
        int      type      = 0;
        float    cutoffHz  = 1200.0f;
        float    resonance = 0.2f;
        float    envAmount = 24.0f;
        Envelope sweep {};
    };

    /** One oscillator on the bench: its shape (Waveform's order), its
        transposition in semitones, its fine tune in cents, its level, its
        unison stack, the envelope under it (none: it just starts and stops
        with its key) and the filter it goes through (none: unfiltered). */
    struct Oscillator
    {
        int   wave      = 2;
        int   semitones = 0;
        float fineCents = 0.0f;
        float level     = 1.0f;
        Unison unison {};
        std::optional<Envelope> envelope {};
        std::optional<Filter>   filter {};
    };

    /** The delay, following the tempo: its note length (an index into
        Effects::divisions), FEEDBACK, TONE, MIX and whether it ping-pongs. */
    struct Delay
    {
        int   division = Effects::dottedEighth;
        float feedback = 0.35f;
        float tone     = 0.6f;
        float mix      = 0.25f;
        bool  pingPong = false;
    };

    /** The reverb: SIZE (0.5 rings about 2 s), DAMPING, WIDTH, MIX and
        PRE-DELAY in milliseconds. */
    struct Reverb
    {
        float size       = 0.5f;
        float damping    = 0.4f;
        float width      = 1.0f;
        float mix        = 0.25f;
        float preDelayMs = 20.0f;
    };

    /** The drive: DRIVE, its shape (an Effects::DriveShape), TONE and MIX. */
    struct Drive
    {
        float amount = 0.4f;
        int   shape  = 0;
        float tone   = 0.7f;
        float mix    = 1.0f;
    };

    /** The EQ: as the engine has it (see Effects::EqSettings), its switch aside. */
    using Eq = Effects::EqSettings;

    /** What kind of sound a preset is, in the order the preset tray lists them. */
    enum class Category { init, bass, lead, arp, pluck, keys, pads, strings, brass, sfx, drums };
    inline constexpr std::array<const char*, 11> categoryNames {
        "Init", "Bass", "Lead", "Arp", "Pluck", "Keys", "Pads", "Strings", "Brass", "SFX", "Drums" };
    inline constexpr int numCategories = (int) categoryNames.size();

    inline const char* nameOf (Category category) { return categoryNames[(size_t) category]; }

    struct Preset
    {
        Category    category;
        const char* name;
        const char* detail;   // one line for the preset tray
        std::array<std::optional<Oscillator>, Bench::numOscillators> oscillators;   // empty: off the bench
        float outputDb = 0.0f;
        std::optional<Delay>  delay {};    // empty: out of the chain
        std::optional<Reverb> reverb {};
        std::optional<Drive>  drive {};
        float glideSeconds = 0.0f;   // each note's slide from the one before
        bool  mono = false;          // one note at a time, legato
        std::optional<Eq> eq {};     // empty: out of the chain
        std::optional<Arp::Settings> arp {};   // empty: the arp off (its own switch aside)
    };

    /** An arp in sixteenths, its pattern written a step to a word: the rows
        it plays ("0" the lowest key held, "02" two at once), or "." for a
        rest, then "!" for an accent, "," short, "-" long or "_" tied. */
    inline Arp::Settings arpOf (const char* steps, int rate = Arp::sixteenth, float swing = 0.0f)
    {
        Arp::Settings arp;
        arp.on    = true;
        arp.rate  = rate;
        arp.swing = swing;
        const auto words = juce::StringArray::fromTokens (steps, false);
        arp.pattern.length = juce::jlimit (1, Arp::maxSteps, words.size());
        for (int i = 0; i < arp.pattern.length; ++i)
        {
            Arp::Step step { 0, 0.7f, Arp::Length::half };
            for (const auto c : words[i])
            {
                if (c >= '0' && c < '0' + Arp::numRows) step.setRow (c - '0', true);
                else if (c == '!') step.velocity = 1.0f;
                else if (c == ',') step.length = Arp::Length::shortNote;
                else if (c == '-') step.length = Arp::Length::longNote;
                else if (c == '_') step.length = Arp::Length::tie;
            }
            arp.pattern[i] = step;
        }
        return arp;
    }

    // The arp's RATEs, by name (their place in Arp::rates), besides Arp::sixteenth.
    inline constexpr int arpQuarter      = 0;
    inline constexpr int arpThirtySecond = 7;

    // Note lengths, by name (their place in Effects::divisions).
    inline constexpr int sixteenth    = 2;
    inline constexpr int eighth       = 5;
    inline constexpr int dottedEighth = 6;
    inline constexpr int quarter      = 8;
    inline constexpr int half         = 10;

    // The wave shapes, by name (their place in Waveform).
    inline constexpr int sine     = (int) Waveform::sine;
    inline constexpr int triangle = (int) Waveform::triangle;
    inline constexpr int saw      = (int) Waveform::saw;
    inline constexpr int square   = (int) Waveform::square;
    inline constexpr int noise    = (int) Waveform::noise;

    // The filter types, by name (their place in Filter::Type).
    inline constexpr int low24  = (int) ::Filter::Type::low24;
    inline constexpr int band   = (int) ::Filter::Type::band;
    inline constexpr int high12 = (int) ::Filter::Type::high12;
    inline constexpr int high24 = (int) ::Filter::Type::high24;

    // The drive's shapes, by name (their place in Effects::DriveShape).
    inline constexpr int hard = (int) Effects::DriveShape::hard;

    inline const std::array<Preset, 36> factory {{
        { Category::init, "Empty Bench", "Nothing on it yet: build your own", {}, 0.0f },

        { Category::pads, "Supersaw Pad", "Nine wide saws, an octave blooming on top",
          { Oscillator { saw, 0,  0.0f, 1.0f,  { 9, 28.0f, 0.9f }, ar (0.6f, 2.5f) },
            Oscillator { saw, 12, 7.0f, 0.45f, { 5, 20.0f, 1.0f }, Envelope { 0.4f, 1.2f, 0.0f, 0.3f, 1.0f, 2.5f } } },
          -7.0f, Delay { quarter, 0.3f, 0.5f, 0.15f, true }, Reverb { 0.72f, 0.45f, 1.0f, 0.3f, 30.0f } },
        { Category::strings, "Warm Strings", "Soft saw section with a low triangle",
          { Oscillator { saw, 0, 0.0f, 0.8f,        { 6, 14.0f, 0.75f }, adsr (0.45f, 1.5f, 0.85f, 1.2f) },
            Oscillator { saw, 12, -6.0f, 0.3f,      { 4, 12.0f, 0.9f },  adsr (0.6f, 1.5f, 0.85f, 1.2f) },
            Oscillator { triangle, -12, 0.0f, 0.4f, { 1, 15.0f, 0.5f },  ar (0.3f, 1.0f) } },
          -6.0f, std::nullopt, Reverb { 0.65f, 0.5f, 1.0f, 0.25f, 25.0f } },
        { Category::pads, "Slow Swell", "A shimmering stack, its layers arriving in turn",
          { Oscillator { triangle, 0, 0.0f, 1.0f, { 8, 18.0f, 0.8f }, ar (1.5f, 6.0f) },
            Oscillator { sine, 12, 4.0f, 0.5f,    { 6, 22.0f, 1.0f }, Envelope { 1.0f, 2.0f, 0.0f, 0.3f, 1.0f, 6.0f } },
            Oscillator { sine, 19, 0.0f, 0.25f,   { 4, 25.0f, 1.0f }, Envelope { 2.0f, 2.5f, 0.0f, 0.3f, 1.0f, 7.0f } },
            Oscillator { saw, -12, 0.0f, 0.2f,    { 3, 10.0f, 0.4f }, ar (1.5f, 5.0f) } },
          -9.0f, Delay { half, 0.35f, 0.4f, 0.12f, true }, Reverb { 0.8f, 0.4f, 1.0f, 0.3f, 40.0f } },

        { Category::lead, "Trance Lead", "Bright detuned saw and square",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 7, 18.0f, 0.7f }, adsr (0.003f, 0.4f, 0.7f, 0.25f) },
            Oscillator { square, 0, -8.0f, 0.5f, { 3, 10.0f, 0.5f }, adsr (0.003f, 0.4f, 0.7f, 0.25f) } },
          -6.0f, Delay { dottedEighth, 0.4f, 0.55f, 0.22f, true }, Reverb { 0.55f, 0.45f, 1.0f, 0.18f, 20.0f } },
        { Category::lead, "8-Bit Lead", "Bare squares, straight from the console",
          { Oscillator { square, 0, 0.0f, 1.0f,  {}, adsr (0.001f, 0.15f, 0.6f, 0.04f) },
            Oscillator { square, 12, 0.0f, 0.3f, {}, adsr (0.001f, 0.08f, 0.0f, 0.04f) } },
          -9.0f },
        { Category::lead, "Hollow Flute", "Breathy triangle, its octave breathing in late",
          { Oscillator { triangle, 0, 0.0f, 1.0f, { 2, 5.0f, 0.3f }, adsr (0.08f, 0.2f, 0.85f, 0.35f) },
            Oscillator { sine, 12, 0.0f, 0.25f,   {},                Envelope { 0.12f, 0.25f, 0.0f, 0.3f, 1.0f, 0.3f } } },
          -3.0f, std::nullopt, Reverb { 0.5f, 0.55f, 0.8f, 0.2f, 15.0f } },

        { Category::arp, "Saw Pluck", "A short, bright pluck for arpeggios",
          { Oscillator { saw, 0, 0.0f, 1.0f,   { 3, 8.0f, 0.6f }, adsr (0.001f, 0.35f, 0.0f, 0.3f) },
            Oscillator { sine, 12, 0.0f, 0.4f, {},                adsr (0.001f, 0.12f, 0.0f, 0.1f) } },
          -5.0f, Delay { dottedEighth, 0.35f, 0.6f, 0.2f, true }, Reverb { 0.45f, 0.5f, 1.0f, 0.15f, 10.0f } },
        { Category::pluck, "Wood Mallet", "A soft mallet that dies away",
          { Oscillator { sine, 0, 0.0f, 1.0f,     {}, adsr (0.001f, 0.6f, 0.0f, 0.5f) },
            Oscillator { triangle, 24, 0.0f, 0.3f, {}, adsr (0.001f, 0.06f, 0.0f, 0.06f) } },
          -3.0f, std::nullopt, Reverb { 0.45f, 0.5f, 1.0f, 0.18f, 10.0f } },
        { Category::keys, "Chord Stab", "Held for a beat, then gone: house chords",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 5, 14.0f, 0.8f }, Envelope { 0.0f, 0.002f, 0.12f, 0.1f, 0.0f, 0.1f } },
            Oscillator { square, -12, 0.0f, 0.4f, {},                 Envelope { 0.0f, 0.002f, 0.12f, 0.1f, 0.0f, 0.1f } } },
          -7.0f, std::nullopt, Reverb { 0.4f, 0.5f, 1.0f, 0.15f, 10.0f } },

        { Category::keys, "Drawbar Organ", "Sines at 16', 8' and 4', with a percussive click",
          { Oscillator { sine, 0, 0.0f, 1.0f,   { 2, 4.0f, 0.4f }, ar (0.004f, 0.06f) },
            Oscillator { sine, 12, 0.0f, 0.6f,  { 2, 4.0f, 0.4f }, ar (0.004f, 0.06f) },
            Oscillator { sine, 19, 0.0f, 0.5f,  {},                adsr (0.002f, 0.25f, 0.0f, 0.06f) },
            Oscillator { sine, -12, 0.0f, 0.7f, {},                ar (0.004f, 0.06f) } },
          -8.0f, std::nullopt, Reverb { 0.4f, 0.6f, 0.8f, 0.15f, 15.0f } },
        { Category::keys, "Glass Keys", "Struck sines, the overtones dying first",
          { Oscillator { sine, 0, 0.0f, 1.0f,       { 3, 6.0f, 0.6f }, adsr (0.001f, 2.0f, 0.25f, 1.5f) },
            Oscillator { sine, 19, 0.0f, 0.35f,     { 2, 4.0f, 0.8f }, adsr (0.001f, 0.8f, 0.0f, 1.0f) },
            Oscillator { triangle, 24, 3.0f, 0.15f, {},                adsr (0.001f, 0.3f, 0.0f, 0.4f) } },
          -5.0f, std::nullopt, Reverb { 0.6f, 0.35f, 1.0f, 0.25f, 20.0f } },
        { Category::brass, "Brass Section", "Biting saws that settle, a square swelling in",
          { Oscillator { saw, 0, 0.0f, 1.0f,      { 4, 9.0f, 0.5f }, adsr (0.06f, 0.3f, 0.75f, 0.25f) },
            Oscillator { saw, 0, 10.0f, 0.8f,     { 4, 9.0f, 0.5f }, adsr (0.06f, 0.3f, 0.75f, 0.25f) },
            Oscillator { square, -12, 0.0f, 0.3f, {},                Envelope { 0.05f, 0.15f, 0.0f, 0.3f, 1.0f, 0.3f } } },
          -7.0f, std::nullopt, Reverb { 0.5f, 0.5f, 1.0f, 0.18f, 20.0f }, Drive { 0.25f, 1, 0.65f, 1.0f } },
        { Category::keys, "Fifth Stack", "Root, fifth and sub for big chords",
          { Oscillator { saw, 0, 0.0f, 1.0f,      { 5, 16.0f, 0.6f }, adsr (0.01f, 0.6f, 0.75f, 0.4f) },
            Oscillator { saw, 7, 0.0f, 0.7f,      { 5, 16.0f, 0.6f }, adsr (0.01f, 0.6f, 0.75f, 0.4f) },
            Oscillator { square, -12, 0.0f, 0.6f, {},                 ar (0.01f, 0.4f) } },
          -9.0f, std::nullopt, Reverb { 0.55f, 0.45f, 1.0f, 0.18f, 20.0f } },

        { Category::bass, "Reese Bass", "Two beating saws an octave down, warmly driven",
          { Oscillator { saw, -12, 0.0f, 1.0f,  { 3, 12.0f, 0.3f }, adsr (0.005f, 0.5f, 0.8f, 0.2f) },
            Oscillator { saw, -12, 18.0f, 0.9f, { 3, 12.0f, 0.3f }, adsr (0.005f, 0.5f, 0.8f, 0.2f) } },
          -7.5f, std::nullopt, std::nullopt, Drive { 0.35f, 0, 0.55f, 1.0f } },
        { Category::bass, "Sub Bass", "A clean sine sub with a plucked edge",
          { Oscillator { sine, -12, 0.0f, 1.0f,    {}, ar (0.002f, 0.12f) },
            Oscillator { triangle, 0, 0.0f, 0.35f, {}, adsr (0.002f, 0.2f, 0.3f, 0.08f) } },
          -4.0f },

        // Added later, at the end, so the presets before them keep the
        // program numbers hosts have saved.
        { Category::pluck, "Filter Pluck", "A saw whose low-pass snaps shut as it rings",
          { Oscillator { saw, 0, 0.0f, 1.0f, { 3, 10.0f, 0.6f }, adsr (0.002f, 1.2f, 0.3f, 0.35f),
                         Filter { low24, 260.0f, 0.35f, 36.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.45f, 0.0f, 0.3f, 0.0f, -0.6f, 0.0f } } } },
          -4.0f, Delay { dottedEighth, 0.35f, 0.55f, 0.2f, true }, Reverb { 0.5f, 0.5f, 1.0f, 0.15f, 10.0f } },
        { Category::bass, "Acid Bass", "A squelchy square, its resonant filter swept short",
          { Oscillator { square, -12, 0.0f, 1.0f, {}, adsr (0.002f, 0.3f, 0.7f, 0.08f),
                         Filter { low24, 180.0f, 0.72f, 30.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.22f, 0.1f, 0.1f, 0.0f, -0.5f, 0.0f } } } },
          -8.0f, std::nullopt, std::nullopt, Drive { 0.3f, 1, 0.6f, 1.0f } },
        { Category::lead, "Slide Lead", "Mono supersaw that slides from note to note",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 9, 22.0f, 0.75f }, adsr (0.002f, 0.5f, 0.8f, 0.18f),
                         Filter { low24, 2400.0f, 0.25f, 18.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.35f, 0.3f, 0.2f } } },
            Oscillator { saw, 12, 5.0f, 0.4f,    { 5, 16.0f, 0.9f },  adsr (0.002f, 0.5f, 0.8f, 0.18f) },
            Oscillator { square, -12, 0.0f, 0.35f, {},                adsr (0.002f, 0.5f, 0.9f, 0.18f) } },
          -8.0f, Delay { dottedEighth, 0.35f, 0.55f, 0.2f, true }, Reverb { 0.6f, 0.45f, 1.0f, 0.2f, 20.0f },
          Drive { 0.2f, 0, 0.7f, 1.0f }, 0.12f, true },

        // The arp's, after them.
        { Category::arp, "Neon Arp", "Hold a chord: it climbs and turns in sixteenths",
          { Oscillator { saw, 0, 0.0f, 1.0f,    { 3, 9.0f, 0.7f }, adsr (0.001f, 0.22f, 0.15f, 0.12f),
                         Filter { low24, 900.0f, 0.3f, 30.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.2f, 0.0f, 0.15f } } },
            Oscillator { square, 12, 0.0f, 0.3f, {},                adsr (0.001f, 0.1f, 0.0f, 0.08f) } },
          -6.0f, Delay { dottedEighth, 0.38f, 0.55f, 0.22f, true }, Reverb { 0.55f, 0.45f, 1.0f, 0.2f, 15.0f },
          std::nullopt, 0.0f, false, std::nullopt, arpOf ("0! 1 2 3 4! 3 2 1 0! 2 1 3 2! 4 3 5") },
        { Category::bass, "Rolling Bass", "Hold a note: it rolls in octaves, the off-beats lifting",
          { Oscillator { saw, -12, 0.0f, 1.0f,  { 2, 8.0f, 0.2f }, adsr (0.002f, 0.18f, 0.4f, 0.06f),
                         Filter { low24, 320.0f, 0.4f, 24.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.16f, 0.1f, 0.08f } } },
            Oscillator { sine, -24, 0.0f, 0.6f, {},                ar (0.002f, 0.06f) } },
          -4.5f, std::nullopt, std::nullopt, Drive { 0.25f, 0, 0.6f, 1.0f }, 0.0f, false, std::nullopt,
          arpOf ("0! 0, 1 0, 0! 0, 1- 0,") },
        { Category::keys, "Swing Stabs", "Hold a chord: house stabs on a swung off-beat rhythm",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 5, 14.0f, 0.8f }, adsr (0.002f, 0.2f, 0.3f, 0.12f) },
            Oscillator { square, -12, 0.0f, 0.35f, {},                adsr (0.002f, 0.2f, 0.3f, 0.12f) } },
          -8.0f, Delay { eighth, 0.25f, 0.5f, 0.15f, true }, Reverb { 0.45f, 0.5f, 1.0f, 0.18f, 10.0f }, std::nullopt, 0.0f, false,
          std::nullopt, arpOf ("012! . . 012, . . 012 . . . 012! . 012, . . .", Arp::sixteenth, 0.35f) },

        // The drums: noise for the rattle and the sizzle, each burst shaped
        // short by its envelope and filtered to its place in the spectrum.
        { Category::drums, "Noise Snare", "A short triangle body under a crack of noise",
          { Oscillator { triangle, 0, 0.0f, 0.75f, {}, Envelope { 0.0f, 0.001f, 0.0f, 0.09f, 0.0f, 0.06f, 0.0f, -0.4f, 0.0f } },
            Oscillator { noise, 0, 0.0f, 0.9f,     {}, Envelope { 0.0f, 0.001f, 0.0f, 0.22f, 0.0f, 0.12f, 0.0f, -0.6f, 0.0f },
                         Filter { high12, 1200.0f, 0.1f, 0.0f } } },
          -1.0f, std::nullopt, Reverb { 0.3f, 0.5f, 1.0f, 0.12f, 5.0f } },
        { Category::drums, "Hand Clap", "Three quick bursts of noise, then the tail",
          { Oscillator { noise, 0, 0.0f, 1.0f,  {}, Envelope { 0.0f, 0.001f, 0.0f, 0.011f, 0.0f, 0.01f, 0.0f, -0.5f, 0.0f },
                         Filter { band, 1100.0f, 0.0f, 0.0f } },
            Oscillator { noise, 0, 0.0f, 1.0f,  {}, Envelope { 0.011f, 0.001f, 0.0f, 0.011f, 0.0f, 0.01f, 0.0f, -0.5f, 0.0f },
                         Filter { band, 1100.0f, 0.0f, 0.0f } },
            Oscillator { noise, 0, 0.0f, 1.0f,  {}, Envelope { 0.022f, 0.001f, 0.0f, 0.011f, 0.0f, 0.01f, 0.0f, -0.5f, 0.0f },
                         Filter { band, 1100.0f, 0.0f, 0.0f } },
            Oscillator { noise, 0, 0.0f, 0.9f,  {}, Envelope { 0.033f, 0.001f, 0.0f, 0.25f, 0.0f, 0.15f, 0.0f, -0.6f, 0.0f },
                         Filter { band, 1100.0f, 0.0f, 0.0f } } },
          6.0f, std::nullopt, Reverb { 0.35f, 0.5f, 1.0f, 0.15f, 8.0f } },
        { Category::drums, "Closed Hat", "A tight tick of high noise, a little metal in it",
          { Oscillator { noise, 0, 0.0f, 1.0f,     {},                  Envelope { 0.0f, 0.001f, 0.0f, 0.06f, 0.0f, 0.04f, 0.0f, -0.4f, 0.0f },
                         Filter { high12, 5500.0f, 0.1f, 0.0f } },
            Oscillator { square, 24, 37.0f, 0.25f, { 6, 50.0f, 1.0f }, adsr (0.001f, 0.035f, 0.0f, 0.03f),
                         Filter { high24, 7000.0f, 0.2f, 0.0f } } },
          3.0f },
        { Category::drums, "Open Hat", "The closed hat let ring, a sizzle that fades",
          { Oscillator { noise, 0, 0.0f, 1.0f,     {},                  Envelope { 0.0f, 0.001f, 0.0f, 0.5f, 0.0f, 0.25f, 0.0f, -0.5f, 0.0f },
                         Filter { high12, 6000.0f, 0.1f, 0.0f } },
            Oscillator { square, 24, 37.0f, 0.2f,  { 6, 50.0f, 1.0f }, Envelope { 0.0f, 0.001f, 0.0f, 0.4f, 0.0f, 0.2f, 0.0f, -0.5f, 0.0f },
                         Filter { high24, 6500.0f, 0.2f, 0.0f } } },
          0.0f, std::nullopt, Reverb { 0.35f, 0.4f, 1.0f, 0.1f, 5.0f } },
        { Category::drums, "Shaker", "Noise that swells in and is gone: a shaken seed pod",
          { Oscillator { noise, 0, 0.0f, 1.0f, {}, Envelope { 0.0f, 0.025f, 0.0f, 0.07f, 0.0f, 0.05f, 0.4f, -0.5f, 0.0f },
                         Filter { high12, 5000.0f, 0.2f, 0.0f } } },
          -3.0f, std::nullopt, Reverb { 0.3f, 0.5f, 1.0f, 0.1f, 5.0f } },
        { Category::drums, "Cowbell", "Two squares a sour fifth apart, through a band-pass",
          { Oscillator { square, 0, 0.0f, 1.0f,   {}, Envelope { 0.0f, 0.001f, 0.0f, 0.35f, 0.0f, 0.1f, 0.0f, -0.6f, 0.0f },
                         Filter { band, 900.0f, 0.45f, 0.0f } },
            Oscillator { square, 7, -20.0f, 0.9f, {}, Envelope { 0.0f, 0.001f, 0.0f, 0.35f, 0.0f, 0.1f, 0.0f, -0.6f, 0.0f },
                         Filter { band, 900.0f, 0.45f, 0.0f } } },
          1.0f },

        // The effects. The coin and the power-up play their tunes from one
        // key, each note an oscillator whose envelope waits its turn.
        { Category::sfx, "Laser Zap", "A resonant filter diving through a saw: pew",
          { Oscillator { saw, 0, 0.0f, 1.0f, { 3, 12.0f, 0.6f }, adsr (0.001f, 0.3f, 0.0f, 0.1f),
                         Filter { low24, 150.0f, 0.9f, 48.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.25f, 0.0f, 0.1f, 0.0f, -0.3f, 0.0f } } } },
          -8.0f, Delay { eighth, 0.3f, 0.5f, 0.18f, true } },
        { Category::sfx, "Riser", "Hold it: wide saws and a hiss whose filters climb for five seconds",
          { Oscillator { saw, 0, 0.0f, 1.0f,  { 9, 40.0f, 1.0f }, ar (4.0f, 1.5f),
                         Filter { low24, 120.0f, 0.55f, 48.0f, Envelope { 0.0f, 5.0f, 0.0f, 0.3f, 1.0f, 1.5f, 0.5f, 0.0f, 0.0f } } },
            Oscillator { saw, 12, 0.0f, 0.5f, { 7, 45.0f, 1.0f }, ar (5.0f, 1.5f),
                         Filter { low24, 120.0f, 0.55f, 48.0f, Envelope { 0.0f, 5.0f, 0.0f, 0.3f, 1.0f, 1.5f, 0.5f, 0.0f, 0.0f } } },
            Oscillator { noise, 0, 0.0f, 0.5f, {}, ar (5.0f, 1.5f),
                         Filter { band, 300.0f, 0.4f, 48.0f, Envelope { 0.0f, 5.0f, 0.0f, 0.3f, 1.0f, 1.5f, 0.5f, 0.0f, 0.0f } } } },
          -10.0f, Delay { quarter, 0.45f, 0.5f, 0.2f, true }, Reverb { 0.8f, 0.4f, 1.0f, 0.3f, 30.0f } },
        { Category::sfx, "Siren", "Hold one key: it wails up an octave and back",
          { Oscillator { square, 0, 0.0f, 1.0f, {}, ar (0.02f, 0.3f),
                         Filter { low24, 2500.0f, 0.15f, 0.0f } },
            Oscillator { saw, 0, 8.0f, 0.5f,    {}, ar (0.02f, 0.3f) } },
          -3.0f, std::nullopt, Reverb { 0.5f, 0.5f, 1.0f, 0.15f, 20.0f }, std::nullopt, 0.35f, true, std::nullopt,
          arpOf ("0_ 1_", arpQuarter) },
        { Category::sfx, "Coin Pickup", "Two quick squares a fourth apart, from one key",
          { Oscillator { square, 0, 0.0f, 0.8f, {}, Envelope { 0.0f, 0.001f, 0.06f, 0.01f, 0.0f, 0.01f } },
            Oscillator { square, 5, 0.0f, 0.8f, {}, Envelope { 0.07f, 0.001f, 0.0f, 0.35f, 0.0f, 0.2f, 0.0f, -0.4f, 0.0f } } },
          -3.0f },
        { Category::sfx, "Power Up", "A major arpeggio climbing from one key",
          { Oscillator { square, 0, 0.0f, 0.7f,  {}, Envelope { 0.0f, 0.001f, 0.05f, 0.01f, 0.0f, 0.01f } },
            Oscillator { square, 4, 0.0f, 0.7f,  {}, Envelope { 0.06f, 0.001f, 0.05f, 0.01f, 0.0f, 0.01f } },
            Oscillator { square, 7, 0.0f, 0.7f,  {}, Envelope { 0.12f, 0.001f, 0.05f, 0.01f, 0.0f, 0.01f } },
            Oscillator { square, 12, 0.0f, 0.7f, {}, Envelope { 0.18f, 0.001f, 0.0f, 0.5f, 0.0f, 0.3f, 0.0f, -0.4f, 0.0f } } },
          -3.0f, Delay { sixteenth, 0.3f, 0.6f, 0.15f, false } },
        { Category::sfx, "Computer Bleeps", "Hold a chord: a machine thinking out loud",
          { Oscillator { sine, 12, 0.0f, 1.0f,  {}, adsr (0.001f, 0.06f, 0.0f, 0.03f) },
            Oscillator { square, 24, 0.0f, 0.25f, {}, adsr (0.001f, 0.03f, 0.0f, 0.02f) } },
          0.0f, Delay { sixteenth, 0.4f, 0.7f, 0.2f, true }, Reverb { 0.4f, 0.5f, 1.0f, 0.12f, 10.0f }, std::nullopt, 0.0f, false,
          std::nullopt, arpOf ("0, 3, . 1, 5, 2, . 4, 0, . 6, 3, 1, . 7, 2,", arpThirtySecond) },
        { Category::sfx, "Explosion", "A boom of low noise and a sine thud, rolling away",
          { Oscillator { noise, 0, 0.0f, 1.0f,  {}, Envelope { 0.0f, 0.002f, 0.0f, 2.5f, 0.0f, 1.5f, 0.0f, -0.7f, 0.0f },
                         Filter { low24, 120.0f, 0.3f, 36.0f, Envelope { 0.0f, 0.001f, 0.0f, 1.2f, 0.0f, 1.0f, 0.0f, -0.8f, 0.0f } } },
            Oscillator { sine, -24, 0.0f, 0.8f, {}, Envelope { 0.0f, 0.002f, 0.0f, 0.8f, 0.0f, 0.5f, 0.0f, -0.6f, 0.0f } } },
          0.0f, std::nullopt, Reverb { 0.75f, 0.6f, 1.0f, 0.3f, 15.0f }, Drive { 0.5f, 0, 0.4f, 1.0f } },
        { Category::sfx, "Wind", "Hold it: noise through a narrow band that gusts and dies down",
          { Oscillator { noise, 0, 0.0f, 1.0f, {}, ar (1.5f, 3.0f),
                         Filter { band, 350.0f, 0.55f, 24.0f, Envelope { 0.0f, 2.5f, 0.0f, 3.0f, 0.35f, 3.0f, 0.3f, 0.2f, 0.0f } } },
            Oscillator { noise, 0, 0.0f, 0.6f, {}, ar (2.5f, 3.0f),
                         Filter { band, 900.0f, 0.5f, 19.0f, Envelope { 1.0f, 3.0f, 0.0f, 2.5f, 0.3f, 3.0f, 0.3f, 0.2f, 0.0f } } } },
          6.0f, std::nullopt, Reverb { 0.7f, 0.5f, 1.0f, 0.3f, 20.0f } },
    }};

    inline constexpr int numPresets = (int) std::tuple_size_v<decltype (factory)>;

    /** Parameter values, by ID. */
    using Values = std::vector<std::pair<const char*, float>>;

    /** The values of envelope `envelope`'s stages, as `stages` (an envelope's
        or a filter's sweep). */
    inline void addStages (Values& values, const ParamID::Envelope& stages, const Envelope& envelope)
    {
        values.emplace_back (stages.delay,        envelope.delaySeconds);
        values.emplace_back (stages.attack,       envelope.attackSeconds);
        values.emplace_back (stages.hold,         envelope.holdSeconds);
        values.emplace_back (stages.decay,        envelope.decaySeconds);
        values.emplace_back (stages.sustain,      envelope.sustainLevel);
        values.emplace_back (stages.release,      envelope.releaseSeconds);
        values.emplace_back (stages.attackCurve,  envelope.attackCurve);
        values.emplace_back (stages.decayCurve,   envelope.decayCurve);
        values.emplace_back (stages.releaseCurve, envelope.releaseCurve);
    }

    /** The values oscillator `k` (`osc`, empty: off the bench) sets: whether
        it, its envelope and its filter are on the bench, and how they're set. */
    inline void addOscillatorValues (Values& values, size_t k, const std::optional<Oscillator>& osc)
    {
        const auto& ids = ParamID::oscillators[k];
        const auto& env = ParamID::envelopes[k];
        const auto& flt = ParamID::filters[k];
        const bool  shaped   = osc.has_value() && osc->envelope.has_value();
        const bool  filtered = osc.has_value() && osc->filter.has_value();
        values.emplace_back (ids.on, osc.has_value() ? 1.0f : 0.0f);
        values.emplace_back (env.on, shaped ? 1.0f : 0.0f);
        values.emplace_back (flt.on, filtered ? 1.0f : 0.0f);
        if (! osc.has_value())
            return;

        values.emplace_back (ids.wave,   (float) osc->wave);
        values.emplace_back (ids.pitch,  (float) osc->semitones);
        values.emplace_back (ids.fine,   osc->fineCents);
        values.emplace_back (ids.level,  osc->level);
        values.emplace_back (ids.voices, (float) osc->unison.voices);
        values.emplace_back (ids.detune, osc->unison.detuneCents);
        values.emplace_back (ids.width,  osc->unison.width);
        if (shaped)
            addStages (values, env, *osc->envelope);
        if (filtered)
        {
            values.emplace_back (flt.type,      (float) osc->filter->type);
            values.emplace_back (flt.cutoff,    osc->filter->cutoffHz);
            values.emplace_back (flt.resonance, osc->filter->resonance);
            values.emplace_back (flt.envAmount, osc->filter->envAmount);
            addStages (values, flt.sweep, osc->filter->sweep);
        }
    }

    /** The values `preset`'s effects set: whether each is in the chain, and
        how the ones that are are set. */
    inline void addEffectValues (Values& values, const Preset& preset)
    {
        values.emplace_back (ParamID::driveInChain, preset.drive.has_value() ? 1.0f : 0.0f);
        values.emplace_back (ParamID::driveOn, preset.drive.has_value() ? 1.0f : 0.0f);
        if (preset.drive.has_value())
        {
            values.emplace_back (ParamID::driveAmount, preset.drive->amount);
            values.emplace_back (ParamID::driveShape,  (float) preset.drive->shape);
            values.emplace_back (ParamID::driveTone,   preset.drive->tone);
            values.emplace_back (ParamID::driveMix,    preset.drive->mix);
        }
        values.emplace_back (ParamID::delayInChain, preset.delay.has_value() ? 1.0f : 0.0f);
        values.emplace_back (ParamID::delayOn, preset.delay.has_value() ? 1.0f : 0.0f);
        if (preset.delay.has_value())
        {
            values.emplace_back (ParamID::delaySync,     1.0f);
            values.emplace_back (ParamID::delayNote,     (float) preset.delay->division);
            values.emplace_back (ParamID::delayFeedback, preset.delay->feedback);
            values.emplace_back (ParamID::delayTone,     preset.delay->tone);
            values.emplace_back (ParamID::delayMix,      preset.delay->mix);
            values.emplace_back (ParamID::delayPingPong, preset.delay->pingPong ? 1.0f : 0.0f);
        }
        values.emplace_back (ParamID::reverbInChain, preset.reverb.has_value() ? 1.0f : 0.0f);
        values.emplace_back (ParamID::reverbOn, preset.reverb.has_value() ? 1.0f : 0.0f);
        if (preset.reverb.has_value())
        {
            values.emplace_back (ParamID::reverbSize,    preset.reverb->size);
            values.emplace_back (ParamID::reverbDamping, preset.reverb->damping);
            values.emplace_back (ParamID::reverbWidth,   preset.reverb->width);
            values.emplace_back (ParamID::reverbMix,     preset.reverb->mix);
            values.emplace_back (ParamID::reverbPreDelay, preset.reverb->preDelayMs);
        }
        values.emplace_back (ParamID::eqInChain, preset.eq.has_value() ? 1.0f : 0.0f);
        values.emplace_back (ParamID::eqOn, preset.eq.has_value() ? 1.0f : 0.0f);
        if (preset.eq.has_value())
        {
            const auto& eq = *preset.eq;
            for (const auto& [id, value] : { std::pair (ParamID::eqLowCut, eq.lowCutHz), std::pair (ParamID::eqHighCut, eq.highCutHz),
                                             std::pair (ParamID::eqSlope, (float) eq.slope), std::pair (ParamID::eqLowGain, eq.lowGainDb),
                                             std::pair (ParamID::eqLowFreq, eq.lowHz), std::pair (ParamID::eqMidGain, eq.midGainDb),
                                             std::pair (ParamID::eqMidFreq, eq.midHz), std::pair (ParamID::eqMidQ, eq.midQ),
                                             std::pair (ParamID::eqHighGain, eq.highGainDb), std::pair (ParamID::eqHighFreq, eq.highHz) })
                values.emplace_back (id, value);
        }
    }

    /** The parameter values `preset` sets, by ID: whether each oscillator,
        envelope and filter is on the bench and each effect in the chain, and the
        settings of the ones that are. */
    inline Values valuesOf (const Preset& preset)
    {
        Values values;
        for (size_t k = 0; k < preset.oscillators.size(); ++k)
            addOscillatorValues (values, k, preset.oscillators[k]);
        addEffectValues (values, preset);
        values.emplace_back (ParamID::output, preset.outputDb);
        values.emplace_back (ParamID::glide,  preset.glideSeconds);
        values.emplace_back (ParamID::mono,   preset.mono ? 1.0f : 0.0f);
        values.emplace_back (ParamID::arpOn,  preset.arp.has_value() ? 1.0f : 0.0f);
        if (preset.arp.has_value())
        {
            values.emplace_back (ParamID::arpRate,  (float) preset.arp->rate);
            values.emplace_back (ParamID::arpSwing, preset.arp->swing);
            values.emplace_back (ParamID::arpLatch, preset.arp->latch ? 1.0f : 0.0f);
            values.emplace_back (ParamID::arpOctaves, (float) (preset.arp->octaves - 1));
        }
        return values;
    }

    /** What a saved state (the processor's, as getStateInformation writes it)
        holds, as a preset: its oscillators on the bench, with their envelopes
        and filters, the effects in its chain, its glide, mono and output. It
        has no name or category of its own; the browser shows user presets
        with it. Whatever the state doesn't say reads as the preset's default. */
    inline Preset summaryOf (const juce::ValueTree& state)
    {
        const auto value = [&state] (const char* id, float fallback)
        {
            const auto param = state.getChildWithProperty ("id", juce::String (id));
            return param.isValid() ? (float) param.getProperty ("value", fallback) : fallback;
        };
        const auto on = [&value] (const char* id) { return value (id, 0.0f) > 0.5f; };
        const auto stagesOf = [&value] (const ParamID::Envelope& stages)
        {
            const Envelope d;
            return Envelope { value (stages.delay, d.delaySeconds),     value (stages.attack, d.attackSeconds),
                              value (stages.hold, d.holdSeconds),       value (stages.decay, d.decaySeconds),
                              value (stages.sustain, d.sustainLevel),   value (stages.release, d.releaseSeconds),
                              value (stages.attackCurve, d.attackCurve), value (stages.decayCurve, d.decayCurve),
                              value (stages.releaseCurve, d.releaseCurve) };
        };

        Preset preset { Category::lead, "", "", {} };
        for (size_t k = 0; k < preset.oscillators.size(); ++k)
        {
            const auto& ids = ParamID::oscillators[k];
            if (! on (ids.on))
                continue;

            const Oscillator d;
            Oscillator osc;
            osc.wave      = juce::roundToInt (value (ids.wave, (float) d.wave));
            osc.semitones = juce::roundToInt (value (ids.pitch, (float) d.semitones));
            osc.fineCents = value (ids.fine, d.fineCents);
            osc.level     = value (ids.level, d.level);
            osc.unison    = { juce::roundToInt (value (ids.voices, (float) d.unison.voices)),
                              value (ids.detune, d.unison.detuneCents), value (ids.width, d.unison.width) };
            if (on (ParamID::envelopes[k].on))
                osc.envelope = stagesOf (ParamID::envelopes[k]);
            if (const auto& flt = ParamID::filters[k]; on (flt.on))
            {
                const Filter f;
                osc.filter = Filter { juce::roundToInt (value (flt.type, (float) f.type)), value (flt.cutoff, f.cutoffHz),
                                      value (flt.resonance, f.resonance), value (flt.envAmount, f.envAmount), stagesOf (flt.sweep) };
            }
            preset.oscillators[k] = osc;
        }

        if (on (ParamID::driveInChain) && on (ParamID::driveOn))
        {
            const Drive d;
            preset.drive = Drive { value (ParamID::driveAmount, d.amount), juce::roundToInt (value (ParamID::driveShape, (float) d.shape)),
                                   value (ParamID::driveTone, d.tone), value (ParamID::driveMix, d.mix) };
        }
        if (on (ParamID::delayInChain) && on (ParamID::delayOn))
        {
            const Delay d;
            preset.delay = Delay { juce::roundToInt (value (ParamID::delayNote, (float) d.division)), value (ParamID::delayFeedback, d.feedback),
                                   value (ParamID::delayTone, d.tone), value (ParamID::delayMix, d.mix), on (ParamID::delayPingPong) };
        }
        if (on (ParamID::reverbInChain) && on (ParamID::reverbOn))
        {
            const Reverb d;
            preset.reverb = Reverb { value (ParamID::reverbSize, d.size), value (ParamID::reverbDamping, d.damping),
                                     value (ParamID::reverbWidth, d.width), value (ParamID::reverbMix, d.mix),
                                     value (ParamID::reverbPreDelay, d.preDelayMs) };
        }
        if (on (ParamID::eqInChain) && on (ParamID::eqOn))
        {
            const Eq d;
            Eq eq;
            eq.on         = true;
            eq.lowCutHz   = value (ParamID::eqLowCut, d.lowCutHz);
            eq.highCutHz  = value (ParamID::eqHighCut, d.highCutHz);
            eq.slope      = juce::roundToInt (value (ParamID::eqSlope, (float) d.slope));
            eq.lowGainDb  = value (ParamID::eqLowGain, d.lowGainDb);
            eq.lowHz      = value (ParamID::eqLowFreq, d.lowHz);
            eq.midGainDb  = value (ParamID::eqMidGain, d.midGainDb);
            eq.midHz      = value (ParamID::eqMidFreq, d.midHz);
            eq.midQ       = value (ParamID::eqMidQ, d.midQ);
            eq.highGainDb = value (ParamID::eqHighGain, d.highGainDb);
            eq.highHz     = value (ParamID::eqHighFreq, d.highHz);
            preset.eq = eq;
        }
        preset.outputDb     = value (ParamID::output, 0.0f);
        preset.glideSeconds = value (ParamID::glide, 0.0f);
        preset.mono         = on (ParamID::mono);
        if (on (ParamID::arpOn))
        {
            Arp::Settings arp;
            arp.on      = true;
            arp.rate    = juce::roundToInt (value (ParamID::arpRate, (float) arp.rate));
            arp.swing   = value (ParamID::arpSwing, arp.swing);
            arp.latch   = on (ParamID::arpLatch);
            arp.octaves = juce::roundToInt (value (ParamID::arpOctaves, 0.0f)) + 1;
            arp.pattern = Arp::patternIn (state.getChildWithName (Arp::arpType));
            preset.arp  = arp;
        }
        return preset;
    }

    /** Loads `preset` into `state`: every parameter back to its default
        (but how sound flows, which stays as it was picked), then the
        preset's own values and its arp's pattern, if it has one, and the
        bench and the effects set out afresh. Message thread. */
    inline void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset)
    {
        const auto set = [] (juce::RangedAudioParameter& parameter, float normalised)
        {
            if (juce::exactlyEqual (parameter.getValue(), normalised))
                return;
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (normalised);
            parameter.endChangeGesture();
        };

        const auto values = valuesOf (preset);
        for (auto* p : state.processor.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p); parameter != nullptr
                                                                                   && parameter->getParameterID() != ParamID::signalFlow)
            {
                const auto given = std::find_if (values.begin(), values.end(),
                                                 [parameter] (const auto& v) { return parameter->getParameterID() == v.first; });
                set (*parameter, given != values.end() ? parameter->convertTo0to1 (given->second) : parameter->getDefaultValue());
            }

        if (preset.arp.has_value())
            Arp::Model (state).setPattern (preset.arp->pattern);
        Bench::Model (state).layOut();
        Rack::Model (state).layOut();
    }
}

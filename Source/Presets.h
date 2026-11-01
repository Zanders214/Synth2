#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

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
    delay, then the reverb.

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

    struct Preset
    {
        const char* name;
        const char* detail;   // one line for the preset tray
        std::array<std::optional<Oscillator>, Bench::numOscillators> oscillators;   // empty: off the bench
        float outputDb = 0.0f;
        std::optional<Delay>  delay {};    // empty: out of the chain
        std::optional<Reverb> reverb {};
        std::optional<Drive>  drive {};
    };

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

    // The filter types, by name (their place in Filter::Type).
    inline constexpr int low24 = (int) ::Filter::Type::low24;

    inline const std::array<Preset, 18> factory {{
        { "Empty Bench", "Nothing on it yet: build your own", {}, 0.0f },

        // Pads
        { "Supersaw Pad", "Nine wide saws, an octave blooming on top",
          { Oscillator { saw, 0,  0.0f, 1.0f,  { 9, 28.0f, 0.9f }, ar (0.6f, 2.5f) },
            Oscillator { saw, 12, 7.0f, 0.45f, { 5, 20.0f, 1.0f }, Envelope { 0.4f, 1.2f, 0.0f, 0.3f, 1.0f, 2.5f } } },
          -7.0f, Delay { quarter, 0.3f, 0.5f, 0.15f, true }, Reverb { 0.72f, 0.45f, 1.0f, 0.3f, 30.0f } },
        { "Warm Strings", "Soft saw section with a low triangle",
          { Oscillator { saw, 0, 0.0f, 0.8f,        { 6, 14.0f, 0.75f }, adsr (0.45f, 1.5f, 0.85f, 1.2f) },
            Oscillator { saw, 12, -6.0f, 0.3f,      { 4, 12.0f, 0.9f },  adsr (0.6f, 1.5f, 0.85f, 1.2f) },
            Oscillator { triangle, -12, 0.0f, 0.4f, { 1, 15.0f, 0.5f },  ar (0.3f, 1.0f) } },
          -6.0f, std::nullopt, Reverb { 0.65f, 0.5f, 1.0f, 0.25f, 25.0f } },
        { "Slow Swell", "A shimmering stack, its layers arriving in turn",
          { Oscillator { triangle, 0, 0.0f, 1.0f, { 8, 18.0f, 0.8f }, ar (1.5f, 6.0f) },
            Oscillator { sine, 12, 4.0f, 0.5f,    { 6, 22.0f, 1.0f }, Envelope { 1.0f, 2.0f, 0.0f, 0.3f, 1.0f, 6.0f } },
            Oscillator { sine, 19, 0.0f, 0.25f,   { 4, 25.0f, 1.0f }, Envelope { 2.0f, 2.5f, 0.0f, 0.3f, 1.0f, 7.0f } },
            Oscillator { saw, -12, 0.0f, 0.2f,    { 3, 10.0f, 0.4f }, ar (1.5f, 5.0f) } },
          -9.0f, Delay { half, 0.35f, 0.4f, 0.12f, true }, Reverb { 0.8f, 0.4f, 1.0f, 0.3f, 40.0f } },

        // Leads
        { "Trance Lead", "Bright detuned saw and square",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 7, 18.0f, 0.7f }, adsr (0.003f, 0.4f, 0.7f, 0.25f) },
            Oscillator { square, 0, -8.0f, 0.5f, { 3, 10.0f, 0.5f }, adsr (0.003f, 0.4f, 0.7f, 0.25f) } },
          -6.0f, Delay { dottedEighth, 0.4f, 0.55f, 0.22f, true }, Reverb { 0.55f, 0.45f, 1.0f, 0.18f, 20.0f } },
        { "8-Bit Lead", "Bare squares, straight from the console",
          { Oscillator { square, 0, 0.0f, 1.0f,  {}, adsr (0.001f, 0.15f, 0.6f, 0.04f) },
            Oscillator { square, 12, 0.0f, 0.3f, {}, adsr (0.001f, 0.08f, 0.0f, 0.04f) } },
          -9.0f },
        { "Hollow Flute", "Breathy triangle, its octave breathing in late",
          { Oscillator { triangle, 0, 0.0f, 1.0f, { 2, 5.0f, 0.3f }, adsr (0.08f, 0.2f, 0.85f, 0.35f) },
            Oscillator { sine, 12, 0.0f, 0.25f,   {},                Envelope { 0.12f, 0.25f, 0.0f, 0.3f, 1.0f, 0.3f } } },
          -3.0f, std::nullopt, Reverb { 0.5f, 0.55f, 0.8f, 0.2f, 15.0f } },

        // Plucks
        { "Saw Pluck", "A short, bright pluck for arpeggios",
          { Oscillator { saw, 0, 0.0f, 1.0f,   { 3, 8.0f, 0.6f }, adsr (0.001f, 0.35f, 0.0f, 0.3f) },
            Oscillator { sine, 12, 0.0f, 0.4f, {},                adsr (0.001f, 0.12f, 0.0f, 0.1f) } },
          -5.0f, Delay { dottedEighth, 0.35f, 0.6f, 0.2f, true }, Reverb { 0.45f, 0.5f, 1.0f, 0.15f, 10.0f } },
        { "Wood Mallet", "A soft mallet that dies away",
          { Oscillator { sine, 0, 0.0f, 1.0f,     {}, adsr (0.001f, 0.6f, 0.0f, 0.5f) },
            Oscillator { triangle, 24, 0.0f, 0.3f, {}, adsr (0.001f, 0.06f, 0.0f, 0.06f) } },
          -3.0f, std::nullopt, Reverb { 0.45f, 0.5f, 1.0f, 0.18f, 10.0f } },
        { "Chord Stab", "Held for a beat, then gone: house chords",
          { Oscillator { saw, 0, 0.0f, 1.0f,     { 5, 14.0f, 0.8f }, Envelope { 0.0f, 0.002f, 0.12f, 0.1f, 0.0f, 0.1f } },
            Oscillator { square, -12, 0.0f, 0.4f, {},                 Envelope { 0.0f, 0.002f, 0.12f, 0.1f, 0.0f, 0.1f } } },
          -7.0f, std::nullopt, Reverb { 0.4f, 0.5f, 1.0f, 0.15f, 10.0f } },

        // Keys
        { "Drawbar Organ", "Sines at 16', 8' and 4', with a percussive click",
          { Oscillator { sine, 0, 0.0f, 1.0f,   { 2, 4.0f, 0.4f }, ar (0.004f, 0.06f) },
            Oscillator { sine, 12, 0.0f, 0.6f,  { 2, 4.0f, 0.4f }, ar (0.004f, 0.06f) },
            Oscillator { sine, 19, 0.0f, 0.5f,  {},                adsr (0.002f, 0.25f, 0.0f, 0.06f) },
            Oscillator { sine, -12, 0.0f, 0.7f, {},                ar (0.004f, 0.06f) } },
          -8.0f, std::nullopt, Reverb { 0.4f, 0.6f, 0.8f, 0.15f, 15.0f } },
        { "Glass Keys", "Struck sines, the overtones dying first",
          { Oscillator { sine, 0, 0.0f, 1.0f,       { 3, 6.0f, 0.6f }, adsr (0.001f, 2.0f, 0.25f, 1.5f) },
            Oscillator { sine, 19, 0.0f, 0.35f,     { 2, 4.0f, 0.8f }, adsr (0.001f, 0.8f, 0.0f, 1.0f) },
            Oscillator { triangle, 24, 3.0f, 0.15f, {},                adsr (0.001f, 0.3f, 0.0f, 0.4f) } },
          -5.0f, std::nullopt, Reverb { 0.6f, 0.35f, 1.0f, 0.25f, 20.0f } },
        { "Brass Section", "Biting saws that settle, a square swelling in",
          { Oscillator { saw, 0, 0.0f, 1.0f,      { 4, 9.0f, 0.5f }, adsr (0.06f, 0.3f, 0.75f, 0.25f) },
            Oscillator { saw, 0, 10.0f, 0.8f,     { 4, 9.0f, 0.5f }, adsr (0.06f, 0.3f, 0.75f, 0.25f) },
            Oscillator { square, -12, 0.0f, 0.3f, {},                Envelope { 0.05f, 0.15f, 0.0f, 0.3f, 1.0f, 0.3f } } },
          -7.0f, std::nullopt, Reverb { 0.5f, 0.5f, 1.0f, 0.18f, 20.0f }, Drive { 0.25f, 1, 0.65f, 1.0f } },
        { "Fifth Stack", "Root, fifth and sub for big chords",
          { Oscillator { saw, 0, 0.0f, 1.0f,      { 5, 16.0f, 0.6f }, adsr (0.01f, 0.6f, 0.75f, 0.4f) },
            Oscillator { saw, 7, 0.0f, 0.7f,      { 5, 16.0f, 0.6f }, adsr (0.01f, 0.6f, 0.75f, 0.4f) },
            Oscillator { square, -12, 0.0f, 0.6f, {},                 ar (0.01f, 0.4f) } },
          -9.0f, std::nullopt, Reverb { 0.55f, 0.45f, 1.0f, 0.18f, 20.0f } },

        // Bass
        { "Reese Bass", "Two beating saws an octave down, warmly driven",
          { Oscillator { saw, -12, 0.0f, 1.0f,  { 3, 12.0f, 0.3f }, adsr (0.005f, 0.5f, 0.8f, 0.2f) },
            Oscillator { saw, -12, 18.0f, 0.9f, { 3, 12.0f, 0.3f }, adsr (0.005f, 0.5f, 0.8f, 0.2f) } },
          -7.5f, std::nullopt, std::nullopt, Drive { 0.35f, 0, 0.55f, 1.0f } },
        { "Sub Bass", "A clean sine sub with a plucked edge",
          { Oscillator { sine, -12, 0.0f, 1.0f,    {}, ar (0.002f, 0.12f) },
            Oscillator { triangle, 0, 0.0f, 0.35f, {}, adsr (0.002f, 0.2f, 0.3f, 0.08f) } },
          -4.0f },

        // Added later, at the end, so the presets before them keep the
        // program numbers hosts have saved.
        { "Filter Pluck", "A saw whose low-pass snaps shut as it rings",
          { Oscillator { saw, 0, 0.0f, 1.0f, { 3, 10.0f, 0.6f }, adsr (0.002f, 1.2f, 0.3f, 0.35f),
                         Filter { low24, 260.0f, 0.35f, 36.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.45f, 0.0f, 0.3f, 0.0f, -0.6f, 0.0f } } } },
          -4.0f, Delay { dottedEighth, 0.35f, 0.55f, 0.2f, true }, Reverb { 0.5f, 0.5f, 1.0f, 0.15f, 10.0f } },
        { "Acid Bass", "A squelchy square, its resonant filter swept short",
          { Oscillator { square, -12, 0.0f, 1.0f, {}, adsr (0.002f, 0.3f, 0.7f, 0.08f),
                         Filter { low24, 180.0f, 0.72f, 30.0f, Envelope { 0.0f, 0.001f, 0.0f, 0.22f, 0.1f, 0.1f, 0.0f, -0.5f, 0.0f } } } },
          -8.0f, std::nullopt, std::nullopt, Drive { 0.3f, 1, 0.6f, 1.0f } },
    }};

    inline constexpr int numPresets = (int) std::tuple_size_v<decltype (factory)>;

    /** The parameter values `preset` sets, by ID: whether each oscillator,
        envelope and filter is on the bench and each effect in the chain, and the
        settings of the ones that are. */
    inline std::vector<std::pair<const char*, float>> valuesOf (const Preset& preset)
    {
        std::vector<std::pair<const char*, float>> values;
        for (size_t k = 0; k < preset.oscillators.size(); ++k)
        {
            const auto& ids = ParamID::oscillators[k];
            const auto& env = ParamID::envelopes[k];
            const auto& flt = ParamID::filters[k];
            const auto& osc = preset.oscillators[k];
            const bool  shaped   = osc.has_value() && osc->envelope.has_value();
            const bool  filtered = osc.has_value() && osc->filter.has_value();
            values.emplace_back (ids.on, osc.has_value() ? 1.0f : 0.0f);
            values.emplace_back (env.on, shaped ? 1.0f : 0.0f);
            values.emplace_back (flt.on, filtered ? 1.0f : 0.0f);
            const auto addStages = [&values] (const ParamID::Envelope& stages, const Envelope& envelope)
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
            };
            if (osc.has_value())
            {
                values.emplace_back (ids.wave,   (float) osc->wave);
                values.emplace_back (ids.pitch,  (float) osc->semitones);
                values.emplace_back (ids.fine,   osc->fineCents);
                values.emplace_back (ids.level,  osc->level);
                values.emplace_back (ids.voices, (float) osc->unison.voices);
                values.emplace_back (ids.detune, osc->unison.detuneCents);
                values.emplace_back (ids.width,  osc->unison.width);
            }
            if (shaped)
                addStages (env, *osc->envelope);
            if (filtered)
            {
                values.emplace_back (flt.type,      (float) osc->filter->type);
                values.emplace_back (flt.cutoff,    osc->filter->cutoffHz);
                values.emplace_back (flt.resonance, osc->filter->resonance);
                values.emplace_back (flt.envAmount, osc->filter->envAmount);
                addStages (flt.sweep, osc->filter->sweep);
            }
        }
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
        values.emplace_back (ParamID::output, preset.outputDb);
        return values;
    }

    /** Loads `preset` into `state`: every parameter back to its default
        (but how sound flows, which stays as it was picked), then the
        preset's own values, and the bench and the effects set out afresh.
        Message thread. */
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

        Bench::Model (state).layOut();
        Rack::Model (state).layOut();
    }
}

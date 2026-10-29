#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "Bench.h"
#include "ParamIDs.h"

/**
    Presets

    Synth2's factory sounds. Each one says which modules are on the bench and
    how they are set; everything it doesn't mention goes back to its default,
    so a preset sounds the same whatever was playing before it. Loading one
    sets the bench out afresh: its oscillators along the top row, unison and
    the envelope below, in a fixed order with no cables.

    The first, Empty Bench, is the bench a new instance opens on.
*/
namespace Presets
{
    /** One oscillator on the bench: its shape (Waveform's order), its
        transposition in semitones, its fine tune in cents and its level. */
    struct Oscillator
    {
        int   wave      = 2;
        int   semitones = 0;
        float fineCents = 0.0f;
        float level     = 1.0f;
    };

    struct Unison
    {
        int   voices      = 1;
        float detuneCents = 15.0f;
        float width       = 0.5f;
    };

    struct Envelope
    {
        float attackSeconds  = 0.005f;
        float releaseSeconds = 0.3f;
    };

    struct Preset
    {
        const char* name;
        const char* detail;   // one line for the preset tray
        std::array<std::optional<Oscillator>, Bench::numOscillators> oscillators;   // empty: off the bench
        std::optional<Unison>   unison;
        std::optional<Envelope> envelope;
        float outputDb = 0.0f;
    };

    enum Wave { sine, triangle, saw, square };

    inline const std::array<Preset, 13> factory {{
        { "Empty Bench", "Nothing on it yet: build your own", {}, {}, {}, 0.0f },

        // Pads
        { "Supersaw Pad", "Nine wide saws with an octave on top",
          { Oscillator { saw, 0, 0.0f, 1.0f }, Oscillator { saw, 12, 7.0f, 0.45f } },
          Unison { 9, 28.0f, 0.9f }, Envelope { 0.6f, 2.5f }, -7.0f },
        { "Warm Strings", "Soft saw section with a low triangle",
          { Oscillator { saw, 0, 0.0f, 0.8f }, Oscillator { saw, 12, -6.0f, 0.3f }, Oscillator { triangle, -12, 0.0f, 0.4f } },
          Unison { 6, 14.0f, 0.75f }, Envelope { 0.45f, 1.2f }, -6.0f },
        { "Slow Swell", "A shimmering stack that takes its time",
          { Oscillator { triangle, 0, 0.0f, 1.0f }, Oscillator { sine, 12, 4.0f, 0.5f },
            Oscillator { sine, 19, 0.0f, 0.25f }, Oscillator { saw, -12, 0.0f, 0.2f } },
          Unison { 12, 22.0f, 1.0f }, Envelope { 3.0f, 6.0f }, -6.0f },

        // Leads
        { "Trance Lead", "Bright detuned saw and square",
          { Oscillator { saw, 0, 0.0f, 1.0f }, Oscillator { square, 0, -8.0f, 0.5f } },
          Unison { 7, 18.0f, 0.7f }, Envelope { 0.003f, 0.25f }, -7.0f },
        { "8-Bit Lead", "Bare squares, straight from the console",
          { Oscillator { square, 0, 0.0f, 1.0f }, Oscillator { square, 12, 0.0f, 0.3f } },
          {}, Envelope { 0.001f, 0.04f }, -9.0f },
        { "Hollow Flute", "Breathy triangle with a soft octave",
          { Oscillator { triangle, 0, 0.0f, 1.0f }, Oscillator { sine, 12, 0.0f, 0.25f } },
          Unison { 2, 5.0f, 0.3f }, Envelope { 0.08f, 0.35f }, -3.0f },

        // Keys
        { "Drawbar Organ", "Sines at 16', 8', 4' and 2 2/3'",
          { Oscillator { sine, 0, 0.0f, 1.0f }, Oscillator { sine, 12, 0.0f, 0.6f },
            Oscillator { sine, 19, 0.0f, 0.4f }, Oscillator { sine, -12, 0.0f, 0.7f } },
          Unison { 2, 4.0f, 0.4f }, Envelope { 0.004f, 0.06f }, -8.0f },
        { "Glass Keys", "Clear sines with a long ring",
          { Oscillator { sine, 0, 0.0f, 1.0f }, Oscillator { sine, 19, 0.0f, 0.35f }, Oscillator { triangle, 24, 3.0f, 0.15f } },
          Unison { 3, 6.0f, 0.6f }, Envelope { 0.001f, 3.5f }, -6.0f },
        { "Brass Section", "Two saws and a square below, punchy",
          { Oscillator { saw, 0, 0.0f, 1.0f }, Oscillator { saw, 0, 10.0f, 0.8f }, Oscillator { square, -12, 0.0f, 0.3f } },
          Unison { 4, 9.0f, 0.5f }, Envelope { 0.06f, 0.25f }, -8.0f },
        { "Fifth Stack", "Root, fifth and sub for big chords",
          { Oscillator { saw, 0, 0.0f, 1.0f }, Oscillator { saw, 7, 0.0f, 0.7f }, Oscillator { square, -12, 0.0f, 0.6f } },
          Unison { 5, 16.0f, 0.6f }, Envelope { 0.01f, 0.4f }, -9.0f },

        // Bass
        { "Reese Bass", "Two beating saws an octave down",
          { Oscillator { saw, -12, 0.0f, 1.0f }, Oscillator { saw, -12, 18.0f, 0.9f } },
          Unison { 3, 12.0f, 0.3f }, Envelope { 0.005f, 0.2f }, -7.5f },
        { "Sub Bass", "A clean sine sub with a little edge",
          { Oscillator { sine, -12, 0.0f, 1.0f }, Oscillator { triangle, 0, 0.0f, 0.35f } },
          {}, Envelope { 0.002f, 0.12f }, -4.0f },
    }};

    inline constexpr int numPresets = (int) std::tuple_size_v<decltype (factory)>;

    /** The parameter values `preset` sets, by ID: whether each module is on
        the bench, and the settings of the ones that are. */
    inline std::vector<std::pair<const char*, float>> valuesOf (const Preset& preset)
    {
        std::vector<std::pair<const char*, float>> values;
        for (size_t k = 0; k < preset.oscillators.size(); ++k)
        {
            const auto& ids = ParamID::oscillators[k];
            const auto& osc = preset.oscillators[k];
            values.emplace_back (ids.on, osc.has_value() ? 1.0f : 0.0f);
            if (osc.has_value())
            {
                values.emplace_back (ids.wave,  (float) osc->wave);
                values.emplace_back (ids.pitch, (float) osc->semitones);
                values.emplace_back (ids.fine,  osc->fineCents);
                values.emplace_back (ids.level, osc->level);
            }
        }
        values.emplace_back (ParamID::unisonOn, preset.unison.has_value() ? 1.0f : 0.0f);
        if (preset.unison.has_value())
        {
            values.emplace_back (ParamID::voices, (float) preset.unison->voices);
            values.emplace_back (ParamID::detune, preset.unison->detuneCents);
            values.emplace_back (ParamID::width,  preset.unison->width);
        }
        values.emplace_back (ParamID::envelopeOn, preset.envelope.has_value() ? 1.0f : 0.0f);
        if (preset.envelope.has_value())
        {
            values.emplace_back (ParamID::attack,  preset.envelope->attackSeconds);
            values.emplace_back (ParamID::release, preset.envelope->releaseSeconds);
        }
        values.emplace_back (ParamID::output, preset.outputDb);
        return values;
    }

    /** Loads `preset` into `state`: every parameter back to its default, then
        the preset's own values, and the bench set out afresh. Message thread. */
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
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto given = std::find_if (values.begin(), values.end(),
                                                 [parameter] (const auto& v) { return parameter->getParameterID() == v.first; });
                set (*parameter, given != values.end() ? parameter->convertTo0to1 (given->second) : parameter->getDefaultValue());
            }

        Bench::Model (state).layOut();
    }
}

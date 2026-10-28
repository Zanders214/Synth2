#pragma once

#include <algorithm>

#include "Oscillator.h"
#include "Unison.h"

/**
    Synth2VisualModel

    Pure, framework-free numbers behind the editor's two displays: the WAVE
    well (the shape, and a faint copy per unison voice) and the UNISON well (a
    marker per voice at its detune and pan).

    It draws on the very same Oscillator::naive and Unison maths the engine
    plays, so what the displays show never drifts from what you hear. The
    voices' offsets in the WAVE well are only a picture of the detune: the
    real voices start at random points in their cycles and drift apart at the
    detune's beat rate.

    Kept header-only and dependency-free so it can be unit-tested on any
    platform without JUCE or a display.
*/
struct Synth2VisualModel
{
    static float clamp01 (float x) noexcept { return std::min (1.0f, std::max (0.0f, x)); }

    /** The WAVE well's shape at x (0..1 across the well), which shows two cycles. */
    static constexpr float cyclesShown = 2.0f;

    static float waveAt (Waveform wave, float x, float phaseOffset = 0.0f) noexcept
    {
        return Oscillator::naive (wave, (double) (x * cyclesShown + phaseOffset));
    }

    /** How far (in cycles) a unison voice's copy is drawn shifted: its detune
        as a share of the largest detune, up to an eighth of a cycle each way. */
    static float voicePhaseOffset (int index, int numVoices, float detuneCents) noexcept
    {
        return Unison::detuneCents (index, numVoices, detuneCents) / Unison::maxDetuneCents * 0.125f;
    }

    /** One voice's marker in the UNISON well. */
    struct Marker
    {
        float x     = 0.5f;   // 0..1: its detune on a fixed -50..+50 cent scale
        float pan   = 0.0f;   // -1..1: where it sits left to right
        float tint  = 0.5f;   // 0..1: where it sits on the spectrum (flat -> sharp)
    };

    static Marker marker (int index, int numVoices, float detuneCents, float width) noexcept
    {
        const float cents = Unison::detuneCents (index, numVoices, detuneCents);
        Marker m;
        m.x    = clamp01 (0.5f + 0.5f * cents / Unison::maxDetuneCents);
        m.pan  = Unison::pan (index, numVoices, width);
        m.tint = clamp01 (0.5f + 0.5f * Unison::position (index, numVoices));
        return m;
    }
};

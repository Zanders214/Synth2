#pragma once

#include <algorithm>

#include "Oscillator.h"
#include "Unison.h"

/**
    Synth2VisualModel

    Pure, framework-free numbers behind the editor's three displays: the WAVE
    well (the shape, and a faint copy per unison voice), the UNISON well (a
    marker per voice at its detune and pan) and the AMP well (the envelope's
    shape at OUTPUT's level, and a dot per sounding note riding it).

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

    // ---- AMP ---------------------------------------------------------------

    /** The AMP well's envelope, as fractions of the well (x 0..1 left to right,
        level 0..1 bottom to top): a rise over ATTACK, a stretch held at the
        top while the key is down, and a fall over RELEASE. Each ramp's width
        follows its dial's position (the dial's own, skewed, 0..1), so a turn
        of the dial visibly stretches it; the times themselves are in the
        readout. The top sits at OUTPUT's level on a -24..+6 dB scale. */
    struct Envelope
    {
        float attackEnd  = 0.0f;   // x where the rise reaches the top
        float holdEnd    = 0.0f;   // x where the release starts
        float releaseEnd = 0.0f;   // x where it reaches silence
        float peak       = 0.0f;   // the top's height
    };

    static constexpr float minDb = -24.0f;
    static constexpr float maxDb = 6.0f;

    /** How high a level in dB sits on the AMP well's scale (0 = -24 dB, 1 = +6 dB). */
    static float heightForDb (float db) noexcept { return clamp01 ((db - minDb) / (maxDb - minDb)); }

    static Envelope envelope (float attackPosition, float releasePosition, float outputDb) noexcept
    {
        Envelope e;
        e.attackEnd  = 0.06f + 0.30f * clamp01 (attackPosition);
        e.holdEnd    = e.attackEnd + 0.18f;
        e.releaseEnd = e.holdEnd + 0.06f + 0.40f * clamp01 (releasePosition);
        e.peak       = heightForDb (outputDb);
        return e;
    }

    /** Where a sounding note's dot rides the envelope: x across the well,
        y its height (both 0..1). Rising, it climbs the attack; held, it sits in
        the middle of the top; releasing, it walks down the release from
        wherever the key went up. */
    struct Dot
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    static Dot noteDot (const Envelope& e, int stage, float level, float releaseFrom) noexcept
    {
        // stage: 1 attack, 2 hold, 3 release (NoteActivity::Stage)
        const float l = clamp01 (level);
        Dot d;
        d.y = l * e.peak;
        if (stage == 1)
            d.x = l * e.attackEnd;
        else if (stage == 2)
            d.x = 0.5f * (e.attackEnd + e.holdEnd);
        else
            d.x = e.holdEnd + (1.0f - l / std::max (1.0e-4f, releaseFrom)) * (e.releaseEnd - e.holdEnd);
        d.x = clamp01 (d.x);
        return d;
    }
};

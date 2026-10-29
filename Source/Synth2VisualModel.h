#pragma once

#include <algorithm>
#include <cmath>

#include "Oscillator.h"
#include "Unison.h"

/**
    Synth2VisualModel

    Pure, framework-free numbers behind the editor's displays: the OSC 1 and
    OSC 2 wells (the shape at its pitch and level, and a faint copy per unison
    voice), the UNISON well (a
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

    /** An oscillator well's shape at x (0..1 across the well). At PITCH 0 it
        shows two cycles. */
    static constexpr float cyclesShown = 2.0f;

    static float waveAt (Waveform wave, float x, float phaseOffset = 0.0f, float cycles = cyclesShown) noexcept
    {
        return Oscillator::naive (wave, (double) (x * cycles + phaseOffset));
    }

    /** How many cycles an oscillator's well shows: two at PITCH 0, twice as
        many an octave up, half as many an octave down (so its PITCH and FINE
        show as a squeeze or a stretch), kept between half a cycle and eight. */
    static float cyclesFor (int semitones, float fineCents) noexcept
    {
        const float cycles = cyclesShown * std::exp2 (((float) semitones + fineCents / 100.0f) / 12.0f);
        return std::min (8.0f, std::max (0.5f, cycles));
    }

    /** The wave `delta` steps on from `index` (0..3), wrapping round: one
        past SQUARE is SINE again. */
    static int stepWave (int index, int delta) noexcept
    {
        constexpr int numWaves = 4;
        return ((index + delta) % numWaves + numWaves) % numWaves;
    }

    /** How long the WAVE well takes to morph from one shape to the next (the
        design system's --dur-base). */
    static constexpr float morphSeconds = 0.15f;

    /** The design system's ease, cubic-bezier(0.4, 0, 0.2, 1): quick out of
        the start, settling into the end. t and the result run 0..1. */
    static float ease (float t) noexcept
    {
        t = clamp01 (t);
        // The curve's x(u) and y(u) for its control points; find the u whose
        // x is t (x rises monotonically, so bisection is enough), then take y.
        const auto bezier = [] (float u, float p1, float p2)
        {
            const float v = 1.0f - u;
            return 3.0f * v * v * u * p1 + 3.0f * v * u * u * p2 + u * u * u;
        };
        float lo = 0.0f;
        float hi = 1.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (bezier (mid, 0.4f, 0.2f) < t ? lo : hi) = mid;
        }
        return bezier (0.5f * (lo + hi), 0.0f, 1.0f);
    }

    /** The WAVE well's shape `amount` (0..1) of the way from `from` to `to`. */
    static float morphAt (Waveform from, Waveform to, float amount, float x, float phaseOffset = 0.0f,
                          float cycles = cyclesShown) noexcept
    {
        const float a = waveAt (from, x, phaseOffset, cycles);
        const float b = waveAt (to, x, phaseOffset, cycles);
        return a + (b - a) * clamp01 (amount);
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

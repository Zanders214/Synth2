#pragma once

#include <cmath>

/**
    Unison

    How a note's stack of voices spreads out. Framework-free, so the engine and
    the editor's UNISON display place every voice with the same maths.

    VOICES copies of the oscillator play each note. They sit evenly across the
    DETUNE range, from -DETUNE to +DETUNE cents, so the outer two are DETUNE
    cents flat and sharp and the rest fall between (a single voice plays in
    tune). WIDTH spreads them across the stereo field the same way, but in an
    interleaved order: the flattest voice goes hard left, the next one hard
    right, then the next pair a step in, and so on. Each side then gets a mix
    of flat and sharp voices, so the stack is wide without the low side of the
    detune leaning one way.

    The stack is scaled by 1 / sqrt (VOICES): the voices start at random points
    in their cycles, so their levels add like noise, and this keeps a chord's
    loudness about the same whatever the count.
*/
namespace Unison
{
    inline constexpr int   maxVoices       = 16;
    inline constexpr float maxDetuneCents  = 50.0f;

    inline int clampVoices (int numVoices) noexcept
    {
        return numVoices < 1 ? 1 : (numVoices > maxVoices ? maxVoices : numVoices);
    }

    /** Where voice `index` of `numVoices` sits, from -1 (flattest) to +1 (sharpest). */
    inline float position (int index, int numVoices) noexcept
    {
        const int n = clampVoices (numVoices);
        if (n == 1)
            return 0.0f;
        return -1.0f + 2.0f * (float) index / (float) (n - 1);
    }

    /** Voice `index`'s offset from the note, in cents. */
    inline float detuneCents (int index, int numVoices, float detune) noexcept
    {
        return position (index, numVoices) * detune;
    }

    /** Voice `index`'s place in the stereo field, -1 (left) to +1 (right), at
        `width` 0..1. Interleaved: voice 0 hard left, voice 1 hard right, voice 2
        a step in from the left, and so on. */
    inline float pan (int index, int numVoices, float width) noexcept
    {
        const int n    = clampVoices (numVoices);
        const int slot = index % 2 == 0 ? index / 2 : n - 1 - index / 2;
        return position (slot, n) * width;
    }

    /** Left and right gains for a pan of -1..1: equal power, 0 dB in the centre. */
    struct PanGains
    {
        float left  = 1.0f;
        float right = 1.0f;
    };

    inline PanGains panGains (float panPosition) noexcept
    {
        constexpr float quarterPi = 0.78539816f;
        constexpr float sqrt2     = 1.41421356f;
        const float p = panPosition < -1.0f ? -1.0f : (panPosition > 1.0f ? 1.0f : panPosition);
        const float angle = (p + 1.0f) * quarterPi;   // 0 (left) .. pi/4 (centre) .. pi/2 (right)
        return { sqrt2 * std::cos (angle), sqrt2 * std::sin (angle) };
    }

    /** The stack's level scale for `numVoices` voices sounding together. */
    inline float stackGain (float numVoices) noexcept
    {
        return 1.0f / std::sqrt (numVoices < 1.0f ? 1.0f : numVoices);
    }

    /** A voice's frequency ratio for an offset in cents. */
    inline double ratio (float cents) noexcept { return std::exp2 ((double) cents / 1200.0); }
}

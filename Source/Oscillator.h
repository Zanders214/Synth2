#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Simd.h"

/** The basic shapes, and white noise, in the order the WAVE parameter lists them. */
enum class Waveform
{
    sine,
    triangle,
    saw,
    square,
    noise
};

/**
    Oscillator

    The basic wave shapes, framework-free so the engine, the editor's wave
    display and the tests all draw on the same maths.

    Every shape starts its cycle at (or, for the square, right after) a zero
    crossing and rises first, so a note never begins with a jump:

        sine      sin (2 pi t)
        triangle  0 -> 1 at t = 1/4 -> -1 at t = 3/4 -> 0
        saw       0 -> 1 just before t = 1/2, drops to -1, rises back to 0
        square    +1 for the first half cycle, -1 for the second
        noise     white noise, -1..1, whatever the pitch

    naive() is the ideal shape, used for drawing. BlepOscillator plays it with
    PolyBLEP corrections at the saw's and square's jumps, which removes most
    of the aliasing a naive saw / square folds back down at high notes. The
    triangle has no jumps and its harmonics fall away fast, so it plays as is.
    The noise is each sample's phase scrambled by an integer hash: no state
    beyond the phase, the same to the bit four at a time, and a different
    run for each start phase (a note's noise starts at a random one).

    BlepOscillator keeps its phase as a 32-bit fraction of a cycle, which
    wraps by itself as it overflows: no comparing and subtracting each
    sample, a pitch exact to about 0.00001 Hz that never drifts, and
    arithmetic simple enough to step four samples in one instruction. The
    shapes are worked out from it in float, within a float's precision of
    the maths above (the sine as a polynomial, within 2e-7 of std::sin).
*/
namespace Oscillator
{
    inline constexpr double twoPi = 6.283185307179586;

    /** Wraps a phase into 0..1. */
    inline double wrap (double phase) noexcept { return phase - std::floor (phase); }

    /** wrap() for a phase already in 0..2, without the floor: the same result
        to the bit, as taking away exactly 1 from 1..2 is exact. */
    inline double wrapOnce (double phase) noexcept { return phase >= 1.0 ? phase - 1.0 : phase; }

    /** Scrambles `x` (Chris Wellons' lowbias32 hash): neighbouring inputs
        come out unrelated, every bit as likely set as not. */
    inline std::uint32_t scramble (std::uint32_t x) noexcept
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        return x ^ (x >> 16);
    }

    /** The ideal shape at phase 0..1. Noise, which has no cycle, is drawn as
        a line through 64 random points a cycle, the same each time it is drawn. */
    inline float naive (Waveform wave, double phase) noexcept
    {
        const double t = wrap (phase);
        switch (wave)
        {
            case Waveform::sine:     return (float) std::sin (twoPi * t);
            case Waveform::triangle: return (float) (1.0 - 4.0 * std::abs (wrap (t + 0.25) - 0.5));
            case Waveform::saw:      return (float) (2.0 * wrap (t + 0.5) - 1.0);
            case Waveform::noise:
            {
                const double at    = phase * 64.0;
                const double point = std::floor (at);
                const auto   value = [] (double index) { return (double) (std::int32_t) scramble ((std::uint32_t) (std::int64_t) index) / 2147483648.0; };
                return (float) (value (point) + (value (point + 1.0) - value (point)) * (at - point));
            }
            case Waveform::square:   break;
        }
        return t < 0.5 ? 1.0f : -1.0f;
    }

    /** The PolyBLEP residual for a unit jump at phase 0, for a phase `t` that
        advances `dt` per sample: non-zero only within one sample of the jump. */
    inline double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }
}

/** One band-limited oscillator: a phase and how far it moves each sample. */
class BlepOscillator
{
public:
    /** Starts the cycle at `startPhase` (0..1). */
    void reset (double startPhase) noexcept
    {
        phase = (std::uint32_t) (std::uint64_t) (Oscillator::wrap (startPhase) * cycle);
    }

    /** Cycles per sample: the frequency over the sample rate (kept below 0.5). */
    void setIncrement (double cyclesPerSample) noexcept
    {
        dt    = (std::uint32_t) std::max (1.0, std::round (std::clamp (cyclesPerSample, 0.0, 0.49) * cycle));
        perDt = 1.0f / (float) dt;
    }

    /** Where in its cycle it is, and how far it moves each sample (0..1). */
    double getPhase() const noexcept     { return (double) phase / cycle; }
    double getIncrement() const noexcept { return (double) dt / cycle; }

    /** The next sample of `wave`, then one step on. */
    float next (Waveform wave) noexcept
    {
        float value = 0.0f;
        switch (wave)
        {
            case Waveform::sine:     value = shape<Waveform::sine>     (phase, perDt); break;
            case Waveform::triangle: value = shape<Waveform::triangle> (phase, perDt); break;
            case Waveform::saw:      value = shape<Waveform::saw>      (phase, perDt); break;
            case Waveform::square:   value = shape<Waveform::square>   (phase, perDt); break;
            case Waveform::noise:    value = shape<Waveform::noise>    (phase, perDt); break;
        }
        phase += dt;
        return value;
    }

    /** The next `numSamples` of `wave`, each times its `levels`, written to
        `out` (or, with `add`, added to it): what as many next() calls would
        give, to the bit, with the choice of shape made once for the run. */
    void render (Waveform wave, float* out, const float* levels, int numSamples, bool add) noexcept
    {
        BlepOscillator* self = this;
        renderAll (&self, &out, 1, wave, levels, numSamples, add);
    }

    /** render() for `count` oscillators all playing `wave`, each into its own
        `outs`. The samples are the same. */
    static void renderAll (BlepOscillator* const* oscillators, float* const* outs, int count,
                           Waveform wave, const float* levels, int numSamples, bool add) noexcept
    {
        if (add)
            run<true> (oscillators, outs, count, wave, levels, numSamples);
        else
            run<false> (oscillators, outs, count, wave, levels, numSamples);
    }

    /** renderAll() for `count` oscillators that each go straight into one
        stereo pair: each sample, times its `levels`, then times the
        oscillator's `fades`, is added into `left` and `right` times its
        `panLeft` and `panRight`. The same sums, in the same order, as
        rendering each into a row of its own and then adding the rows in one
        by one, so the result is the same to the bit; but each sample goes
        straight in. */
    static void renderPanned (BlepOscillator* const* oscillators, int count, Waveform wave, const float* levels,   // NOSONAR(cpp:S107): hot path, like SynthVoice::mix
                              const float* fades, const float* panLeft, const float* panRight,
                              float* left, float* right, int numSamples) noexcept
    {
        switch (wave)
        {
            case Waveform::sine:     runPanned<Waveform::sine>     (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::triangle: runPanned<Waveform::triangle> (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::saw:      runPanned<Waveform::saw>      (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::square:   runPanned<Waveform::square>   (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::noise:    runPanned<Waveform::noise>    (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
        }
    }

private:
    static constexpr double        cycle = 4294967296.0;   // 2^32: one cycle of the phase
    static constexpr std::uint32_t half  = 0x80000000u;    // half a cycle
    static constexpr std::uint32_t quarter = 0x40000000u;

    static float toFloat (std::uint32_t value) noexcept { return (float) (std::int32_t) value; }
    static float magnitude (float value) noexcept       { return std::abs (value); }
    static float signOf (std::uint32_t value) noexcept  { return (std::int32_t) value < 0 ? -1.0f : 1.0f; }

    static float clampUnit (float value) noexcept       { return std::min (std::max (value, -1.0f), 1.0f); }
    static std::uint32_t scramble (std::uint32_t value) noexcept { return Oscillator::scramble (value); }

    static std::uint32_t foldSine (std::uint32_t phase) noexcept
    {
        const auto signedPhase = (std::int32_t) phase;
        return signedPhase > (std::int32_t) quarter || signedPhase < -(std::int32_t) quarter ? half - phase : phase;
    }

#if SYNTH2_SSE2
    using Floats = Simd::Floats;
    using Ints   = Simd::Ints;

    static Floats toFloat (Ints value) noexcept    { return Floats { _mm_cvtepi32_ps (value.v) }; }
    static Floats magnitude (Floats value) noexcept { return Floats { _mm_and_ps (value.v, _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff))) }; }
    static Floats signOf (Ints value) noexcept      { return Floats { _mm_cvtepi32_ps (_mm_or_si128 (_mm_srai_epi32 (value.v, 31), _mm_set1_epi32 (1))) }; }
    static Floats clampUnit (Floats value) noexcept { return Floats { _mm_min_ps (_mm_max_ps (value.v, _mm_set1_ps (-1.0f)), _mm_set1_ps (1.0f)) }; }

    static Ints foldSine (Ints phase) noexcept
    {
        const __m128i outside = _mm_or_si128 (_mm_cmpgt_epi32 (phase.v, _mm_set1_epi32 ((int) quarter)),
                                              _mm_cmplt_epi32 (phase.v, _mm_set1_epi32 (-(int) quarter)));
        const __m128i mirrored = _mm_sub_epi32 (_mm_set1_epi32 ((int) half), phase.v);
        return Ints { _mm_or_si128 (_mm_and_si128 (outside, mirrored), _mm_andnot_si128 (outside, phase.v)) };
    }

    // Oscillator::scramble, four at a time (SSE2 multiplies 32 bits only in
    // pairs: the even lanes, then the odd ones shifted down, put back together).
    static Ints scramble (Ints value) noexcept
    {
        const auto times = [] (__m128i x, std::uint32_t by)
        {
            const __m128i factor = _mm_set1_epi32 ((int) by);
            const __m128i even = _mm_mul_epu32 (x, factor);
            const __m128i odd  = _mm_mul_epu32 (_mm_srli_epi64 (x, 32), factor);
            return _mm_unpacklo_epi32 (_mm_shuffle_epi32 (even, _MM_SHUFFLE (0, 0, 2, 0)), _mm_shuffle_epi32 (odd, _MM_SHUFFLE (0, 0, 2, 0)));
        };
        __m128i x = value.v;
        x = _mm_xor_si128 (x, _mm_srli_epi32 (x, 16));
        x = times (x, 0x7feb352du);
        x = _mm_xor_si128 (x, _mm_srli_epi32 (x, 15));
        x = times (x, 0x846ca68bu);
        return Ints { _mm_xor_si128 (x, _mm_srli_epi32 (x, 16)) };
    }

    Ints lanePhases() const noexcept
    {
        return Ints { _mm_setr_epi32 ((int) phase, (int) (phase + dt), (int) (phase + 2u * dt), (int) (phase + 3u * dt)) };
    }
#endif

    template <typename Float>
    static Float edge (Float stepsFromJump) noexcept
    {
        const Float x = clampUnit (stepsFromJump);
        return x * (magnitude (x) - Float (2.0f));
    }

    // One sample of `wave` at `phase`. Shifting by half or a quarter of a
    // cycle is an add that wraps by itself.
    template <Waveform wave, typename Float, typename Int>
    static Float shape (Int phase, Float perDt) noexcept
    {
        constexpr float perHalf    = 1.0f / 2147483648.0f;   // 2^-31
        constexpr float perQuarter = 1.0f / 1073741824.0f;   // 2^-30
        if constexpr (wave == Waveform::sine)
        {
            // The phase as signed, -1/2..1/2 of a cycle, folded into -1/4..1/4
            // (where the sine is the mirror of itself either side of a
            // quarter), exactly, then a short polynomial: within 2e-7 of
            // std::sin, and far cheaper.
            constexpr auto toRadians = (float) (Oscillator::twoPi / cycle);
            const Float x  = toFloat (foldSine (phase)) * Float (toRadians);
            const Float x2 = x * x;
            return x * (Float (1.0f) + x2 * (Float (-1.0f / 6.0f) + x2 * (Float (1.0f / 120.0f) + x2 * (Float (-1.0f / 5040.0f)
                         + x2 * (Float (1.0f / 362880.0f) + x2 * Float (-1.0f / 39916800.0f))))));
        }
        else if constexpr (wave == Waveform::triangle)
        {
            // 1 - 4 |t - 1/2| for t a quarter cycle on: the distance from the
            // middle of the cycle, as a signed fraction.
            return Float (1.0f) - magnitude (toFloat ((phase + Int (quarter)) ^ Int (half)) * Float (perQuarter));
        }
        else if constexpr (wave == Waveform::saw)
        {
            const Float fromJump = toFloat (phase + Int (half));
            return fromJump * Float (perHalf) + edge (fromJump * perDt);
        }
        else if constexpr (wave == Waveform::noise)
        {
            return toFloat (scramble (phase)) * Float (perHalf);
        }
        else
        {
            const Int fromDrop = phase + Int (half);
            return edge (toFloat (fromDrop) * perDt) - edge (toFloat (phase) * perDt) + signOf (fromDrop);
        }
    }

    template <bool add>
    static void run (BlepOscillator* const* oscillators, float* const* outs, int count,
                     Waveform wave, const float* levels, int numSamples) noexcept
    {
        switch (wave)
        {
            case Waveform::sine:     run<Waveform::sine, add>     (oscillators, outs, count, levels, numSamples); break;
            case Waveform::triangle: run<Waveform::triangle, add> (oscillators, outs, count, levels, numSamples); break;
            case Waveform::saw:      run<Waveform::saw, add>      (oscillators, outs, count, levels, numSamples); break;
            case Waveform::square:   run<Waveform::square, add>   (oscillators, outs, count, levels, numSamples); break;
            case Waveform::noise:    run<Waveform::noise, add>    (oscillators, outs, count, levels, numSamples); break;
        }
    }

    template <Waveform wave, bool add>
    static void run (BlepOscillator* const* oscillators, float* const* outs, int count,
                     const float* levels, int numSamples) noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            auto& oscillator = *oscillators[k];
            float* out = outs[k];
            const std::uint32_t dt = oscillator.dt;
            const float perDt = oscillator.perDt;
            std::uint32_t phase = oscillator.phase;
            int n = 0;
#if SYNTH2_SSE2
            {
                const Ints   stride { 4u * dt };
                const Floats perStep { perDt };
                Ints phases = oscillator.lanePhases();
                for (; n + 4 <= numSamples; n += 4)
                {
                    const Floats value = shape<wave> (phases, perStep) * Floats::load (levels + n);
                    if constexpr (add)
                        (Floats::load (out + n) + value).store (out + n);
                    else
                        value.store (out + n);
                    phases = phases + stride;
                }
                phase += (std::uint32_t) n * dt;
            }
#endif
            for (; n < numSamples; ++n)
            {
                const float value = shape<wave> (phase, perDt);
                if constexpr (add)
                    out[n] += value * levels[n];
                else
                    out[n] = value * levels[n];
                phase += dt;
            }
            oscillator.phase = phase;
        }
    }

    // Four at a time, then a pair, then the last one, each group added in
    // after the one before, as the rows would be.
    template <Waveform wave>
    static void runPanned (BlepOscillator* const* oscillators, int count, const float* levels, const float* fades,   // NOSONAR(cpp:S107): hot path
                           const float* panLeft, const float* panRight, float* left, float* right, int numSamples) noexcept
    {
        int k = 0;
#if SYNTH2_SSE2
        for (; k + 4 <= count; k += 4)
            runPannedGroup<wave, 4> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
        if (k + 2 <= count)
        {
            runPannedGroup<wave, 2> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
            k += 2;
        }
#endif
        for (; k < count; ++k)
            runPannedGroup<wave, 1> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
    }

    template <Waveform wave, size_t group>
    static void runPannedGroup (BlepOscillator* const* oscillators, const float* levels, const float* fades,   // NOSONAR(cpp:S107): hot path
                                const float* panLeft, const float* panRight, float* left, float* right, int numSamples) noexcept
    {
        int done = 0;
#if SYNTH2_SSE2
        struct Lane
        {
            Ints   phases;
            Ints   stride;
            Floats perDt;
            Floats fade;
            Floats toLeft;
            Floats toRight;
        };
        std::array<Lane, group> lanes;
        for (size_t j = 0; j < group; ++j)
            lanes[j] = { oscillators[j]->lanePhases(), Ints (4u * oscillators[j]->dt), Floats (oscillators[j]->perDt),
                         Floats (fades[j]), Floats (panLeft[j]), Floats (panRight[j]) };

        for (; done + 4 <= numSamples; done += 4)
        {
            const Floats level = Floats::load (levels + done);
            Floats toLeft  = Floats::load (left + done);
            Floats toRight = Floats::load (right + done);
            for (auto& lane : lanes)
            {
                const Floats value = shape<wave> (lane.phases, lane.perDt) * level * lane.fade;
                toLeft  = toLeft  + value * lane.toLeft;
                toRight = toRight + value * lane.toRight;
                lane.phases = lane.phases + lane.stride;
            }
            toLeft.store (left + done);
            toRight.store (right + done);
        }
#endif
        for (size_t j = 0; j < group; ++j)
        {
            auto& oscillator = *oscillators[j];
            const std::uint32_t dt = oscillator.dt;
            const float perDt = oscillator.perDt;
            std::uint32_t phase = oscillator.phase + (std::uint32_t) done * dt;
            for (int n = done; n < numSamples; ++n)
            {
                // As run() then a row's mix: the sample levelled, faded, panned.
                const float value = shape<wave> (phase, perDt) * levels[n] * fades[j];
                left[n]  += value * panLeft[j];
                right[n] += value * panRight[j];
                phase += dt;
            }
            oscillator.phase = phase;
        }
    }

    std::uint32_t phase = 0;   // fractions of a cycle (see cycle)
    std::uint32_t dt    = 1;
    float         perDt = 1.0f;  // 1 / dt
};

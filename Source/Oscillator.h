#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/** The four basic shapes, in the order the WAVE parameter lists them. */
enum class Waveform
{
    sine,
    triangle,
    saw,
    square
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

    naive() is the ideal shape, used for drawing. BlepOscillator plays it with
    PolyBLEP corrections at the saw's and square's jumps, which removes most
    of the aliasing a naive saw / square folds back down at high notes. The
    triangle has no jumps and its harmonics fall away fast, so it plays as is.

    BlepOscillator keeps its phase as a 32-bit fraction of a cycle, which
    wraps by itself as it overflows: no comparing and subtracting each
    sample, a pitch exact to about 0.00001 Hz that never drifts, and
    arithmetic simple enough for the compiler to step several oscillators in
    one instruction. The shapes are worked out from it in float, within a
    float's precision of the maths above (the sine as a polynomial, within
    2e-7 of std::sin).
*/
namespace Oscillator
{
    inline constexpr double twoPi = 6.283185307179586;

    /** Wraps a phase into 0..1. */
    inline double wrap (double phase) noexcept { return phase - std::floor (phase); }

    /** wrap() for a phase already in 0..2, without the floor: the same result
        to the bit, as taking away exactly 1 from 1..2 is exact. */
    inline double wrapOnce (double phase) noexcept { return phase >= 1.0 ? phase - 1.0 : phase; }

    /** The ideal shape at phase 0..1. */
    inline float naive (Waveform wave, double phase) noexcept
    {
        const double t = wrap (phase);
        switch (wave)
        {
            case Waveform::sine:     return (float) std::sin (twoPi * t);
            case Waveform::triangle: return (float) (1.0 - 4.0 * std::abs (wrap (t + 0.25) - 0.5));
            case Waveform::saw:      return (float) (2.0 * wrap (t + 0.5) - 1.0);
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
            case Waveform::sine:     value = sample<Waveform::sine>     (phase, dt, perDt); break;
            case Waveform::triangle: value = sample<Waveform::triangle> (phase, dt, perDt); break;
            case Waveform::saw:      value = sample<Waveform::saw>      (phase, dt, perDt); break;
            case Waveform::square:   value = sample<Waveform::square>   (phase, dt, perDt); break;
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
        `outs`. The samples are the same; stepping several oscillators side by
        side lets the CPU work on them at once instead of waiting on each
        one's phase in turn. */
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
        straight in, and the pair is read and written once for every few
        oscillators rather than once for each. */
    static void renderPanned (BlepOscillator* const* oscillators, int count, Waveform wave, const float* levels,
                              const float* fades, const float* panLeft, const float* panRight,
                              float* left, float* right, int numSamples) noexcept
    {
        switch (wave)
        {
            case Waveform::sine:     runPanned<Waveform::sine>     (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::triangle: runPanned<Waveform::triangle> (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::saw:      runPanned<Waveform::saw>      (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
            case Waveform::square:   runPanned<Waveform::square>   (oscillators, count, levels, fades, panLeft, panRight, left, right, numSamples); break;
        }
    }

private:
    static constexpr double        cycle = 4294967296.0;   // 2^32: one cycle of the phase
    static constexpr std::uint32_t half  = 0x80000000u;    // half a cycle
    static constexpr std::uint32_t quarter = 0x40000000u;

    // Oscillator::polyBlep() for a phase `t` stepping `dt`, both fractions of
    // 2^32, and perDt = 1 / dt. Both sides are worked out and the one that
    // applies is picked, without branching, so oscillators side by side are
    // worked out together.
    static float polyBlep (std::uint32_t t, std::uint32_t dt, float perDt) noexcept
    {
        const float x = (float) t * perDt;                       // after the jump, 0..1
        const float y = -(float) (std::uint32_t) (0u - t) * perDt;   // before it, -1..0
        const float rise = x + x - x * x - 1.0f;
        const float fall = y * y + y + y + 1.0f;
        return t < dt ? rise : (t > 0u - dt ? fall : 0.0f);
    }

    // One sample of `wave` at `phase`. Shifting by half or a quarter of a
    // cycle is an add that wraps by itself.
    template <Waveform wave>
    static float sample (std::uint32_t phase, std::uint32_t dt, float perDt) noexcept
    {
        constexpr float perHalf    = 1.0f / 2147483648.0f;   // 2^-31
        constexpr float perQuarter = 1.0f / 1073741824.0f;   // 2^-30
        if constexpr (wave == Waveform::sine)
        {
            // The phase as signed, -1/2..1/2 of a cycle, folded into -1/4..1/4
            // (where the sine is the mirror of itself either side of a
            // quarter), exactly, then a short polynomial: within 2e-7 of
            // std::sin, and far cheaper.
            const auto a = (std::int32_t) phase;
            const auto mirrored = (std::int32_t) (half - phase);
            const std::int32_t r = a > (std::int32_t) quarter || a < -(std::int32_t) quarter ? mirrored : a;
            constexpr float toRadians = (float) (Oscillator::twoPi / cycle);
            const float x  = (float) r * toRadians;
            const float x2 = x * x;
            return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f
                                                     + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
        }
        else if constexpr (wave == Waveform::triangle)
        {
            // 1 - 4 |t - 1/2| for t a quarter cycle on: the distance from the
            // middle of the cycle, as a signed fraction.
            const auto fromMiddle = (std::int32_t) ((phase + quarter) ^ half);
            return 1.0f - std::abs ((float) fromMiddle * perQuarter);
        }
        else if constexpr (wave == Waveform::saw)
        {
            // 2u - 1 for u half a cycle on is the phase read as signed; the
            // saw's drop sits there.
            return (float) (std::int32_t) phase * perHalf - polyBlep (phase + half, dt, perDt);
        }
        else
        {
            return ((std::int32_t) phase >= 0 ? 1.0f : -1.0f)
                   + polyBlep (phase, dt, perDt) - polyBlep (phase + half, dt, perDt);
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
        }
    }

    // Four at a time, then a pair, then the last one.
    template <Waveform wave, bool add>
    static void run (BlepOscillator* const* oscillators, float* const* outs, int count,
                     const float* levels, int numSamples) noexcept
    {
        int k = 0;
        for (; k + 4 <= count; k += 4)
            runSideBySide<wave, add, 4> (oscillators + k, outs + k, levels, numSamples);
        if (k + 2 <= count)
        {
            runSideBySide<wave, add, 2> (oscillators + k, outs + k, levels, numSamples);
            k += 2;
        }
        if (k < count)
            runSideBySide<wave, add, 1> (oscillators + k, outs + k, levels, numSamples);
    }

    template <Waveform wave, bool add, int lanes>
    static void runSideBySide (BlepOscillator* const* oscillators, float* const* outs,
                               const float* levels, int numSamples) noexcept
    {
        std::array<std::uint32_t, lanes> phases {};
        std::array<std::uint32_t, lanes> steps {};
        std::array<float, lanes> perSteps {};
        for (size_t j = 0; j < (size_t) lanes; ++j)
        {
            phases[j]   = oscillators[j]->phase;
            steps[j]    = oscillators[j]->dt;
            perSteps[j] = oscillators[j]->perDt;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            for (size_t j = 0; j < (size_t) lanes; ++j)
            {
                const float value = sample<wave> (phases[j], steps[j], perSteps[j]);
                if constexpr (add)
                    outs[j][n] += value * levels[n];
                else
                    outs[j][n] = value * levels[n];
                phases[j] += steps[j];
            }
        }

        for (size_t j = 0; j < (size_t) lanes; ++j)
            oscillators[j]->phase = phases[j];
    }

    // Four at a time, then a pair, then the last one, each group added in
    // after the one before, as the rows would be.
    template <Waveform wave>
    static void runPanned (BlepOscillator* const* oscillators, int count, const float* levels, const float* fades,
                           const float* panLeft, const float* panRight, float* left, float* right, int numSamples) noexcept
    {
        int k = 0;
        for (; k + 4 <= count; k += 4)
            runPannedSideBySide<wave, 4> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
        if (k + 2 <= count)
        {
            runPannedSideBySide<wave, 2> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
            k += 2;
        }
        if (k < count)
            runPannedSideBySide<wave, 1> (oscillators + k, levels, fades + k, panLeft + k, panRight + k, left, right, numSamples);
    }

    template <Waveform wave, int lanes>
    static void runPannedSideBySide (BlepOscillator* const* oscillators, const float* levels, const float* fades,
                                     const float* panLeft, const float* panRight, float* left, float* right, int numSamples) noexcept
    {
        std::array<std::uint32_t, lanes> phases {};
        std::array<std::uint32_t, lanes> steps {};
        std::array<float, lanes> perSteps {};
        for (size_t j = 0; j < (size_t) lanes; ++j)
        {
            phases[j]   = oscillators[j]->phase;
            steps[j]    = oscillators[j]->dt;
            perSteps[j] = oscillators[j]->perDt;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            float l = left[n];
            float r = right[n];
            for (size_t j = 0; j < (size_t) lanes; ++j)
            {
                // As run() then a row's mix: the sample levelled, faded, panned.
                const float row   = sample<wave> (phases[j], steps[j], perSteps[j]) * levels[n];
                const float value = row * fades[j];
                l += value * panLeft[j];
                r += value * panRight[j];
                phases[j] += steps[j];
            }
            left[n]  = l;
            right[n] = r;
        }

        for (size_t j = 0; j < (size_t) lanes; ++j)
            oscillators[j]->phase = phases[j];
    }

    std::uint32_t phase = 0;   // fractions of a cycle (see cycle)
    std::uint32_t dt    = 1;
    float         perDt = 1.0f;  // 1 / dt
};

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

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
    void reset (double startPhase) noexcept { phase = Oscillator::wrap (startPhase); }

    /** Cycles per sample: the frequency over the sample rate (kept below 0.5). */
    void setIncrement (double cyclesPerSample) noexcept
    {
        dt = std::clamp (cyclesPerSample, 0.0, 0.49);
    }

    double getPhase() const noexcept     { return phase; }
    double getIncrement() const noexcept { return dt; }

    /** The next sample of `wave`, then one step on. */
    float next (Waveform wave) noexcept
    {
        float value = 0.0f;
        switch (wave)
        {
            case Waveform::sine:     value = sample<Waveform::sine>     (phase, dt); break;
            case Waveform::triangle: value = sample<Waveform::triangle> (phase, dt); break;
            case Waveform::saw:      value = sample<Waveform::saw>      (phase, dt); break;
            case Waveform::square:   value = sample<Waveform::square>   (phase, dt); break;
        }
        phase = step (phase, dt);
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

private:
    // One sample of `wave` at `phase` (0..1). The phase is already wrapped, so
    // the half- and quarter-cycle shifts need wrapOnce(), not wrap()'s floor.
    template <Waveform wave>
    static float sample (double phase, double dt) noexcept
    {
        if constexpr (wave == Waveform::sine)
        {
            return (float) std::sin (Oscillator::twoPi * phase);
        }
        else if constexpr (wave == Waveform::triangle)
        {
            return (float) (1.0 - 4.0 * std::abs (Oscillator::wrapOnce (phase + 0.25) - 0.5));
        }
        else if constexpr (wave == Waveform::saw)
        {
            // The saw's drop sits half a cycle in; shift it to phase 0.
            const double u = Oscillator::wrapOnce (phase + 0.5);
            return (float) (2.0 * u - 1.0 - Oscillator::polyBlep (u, dt));
        }
        else
        {
            return (float) ((phase < 0.5 ? 1.0 : -1.0)
                            + Oscillator::polyBlep (phase, dt)
                            - Oscillator::polyBlep (Oscillator::wrapOnce (phase + 0.5), dt));
        }
    }

    static double step (double phase, double dt) noexcept
    {
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        return phase;
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
        std::array<double, lanes> phases {};
        std::array<double, lanes> steps {};
        for (size_t j = 0; j < (size_t) lanes; ++j)
        {
            phases[j] = oscillators[j]->phase;
            steps[j]  = oscillators[j]->dt;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            for (size_t j = 0; j < (size_t) lanes; ++j)
            {
                const float value = sample<wave> (phases[j], steps[j]);
                if constexpr (add)
                    outs[j][n] += value * levels[n];
                else
                    outs[j][n] = value * levels[n];
                phases[j] = step (phases[j], steps[j]);
            }
        }

        for (size_t j = 0; j < (size_t) lanes; ++j)
            oscillators[j]->phase = phases[j];
    }

    double phase = 0.0;
    double dt    = 0.0;
};

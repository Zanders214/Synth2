#pragma once

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
        dt = cyclesPerSample < 0.0 ? 0.0 : (cyclesPerSample > 0.49 ? 0.49 : cyclesPerSample);
    }

    double getPhase() const noexcept     { return phase; }
    double getIncrement() const noexcept { return dt; }

    /** The next sample of `wave`, then one step on. */
    float next (Waveform wave) noexcept
    {
        float value = 0.0f;
        switch (wave)
        {
            case Waveform::sine:
            case Waveform::triangle:
                value = Oscillator::naive (wave, phase);
                break;

            case Waveform::saw:
            {
                // The saw's drop sits half a cycle in; shift it to phase 0.
                const double u = Oscillator::wrap (phase + 0.5);
                value = (float) (2.0 * u - 1.0 - Oscillator::polyBlep (u, dt));
                break;
            }

            case Waveform::square:
                value = (float) ((phase < 0.5 ? 1.0 : -1.0)
                                 + Oscillator::polyBlep (phase, dt)
                                 - Oscillator::polyBlep (Oscillator::wrap (phase + 0.5), dt));
                break;
        }

        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        return value;
    }

private:
    double phase = 0.0;
    double dt    = 0.0;
};

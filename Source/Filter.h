#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

/**
    Filter

    A note's filter, framework-free so the engine, the editor's FILTER graph
    and the tests share it: a state-variable filter in its topology-preserving
    form (after Zavalishin and Simper), which stays stable and in tune however
    fast its cutoff moves, run in stereo over short chunks.

    Its types: LOW and HIGH at 12 or 24 dB an octave, BAND and NOTCH. The 24 dB
    types are two stages, the first a fixed Butterworth stage and the second
    carrying the resonance, so with no resonance they are Butterworth too.
    RESONANCE (0..1) raises the resonant stage's Q from its flat value up to
    40, evenly by ear: near the top it rings at the cutoff, but never runs
    away.

    The cutoff glides from where the last chunk ended to its new value across
    each chunk, so a sweep is smooth without working out a tangent per sample.
*/
class Filter
{
public:
    enum class Type { low24, low12, band, high12, high24, notch };
    static constexpr int numTypes = 6;

    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;
    static constexpr double maxQ = 40.0;

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    /** Silence: the filter forgets what it has heard, and its cutoff starts afresh. */
    void reset() noexcept
    {
        state  = {};
        primed = false;
    }

    /** Whether the filter is still ringing from what it heard: a resonant
        filter goes on sounding for a while after its input stops. */
    bool isRinging() const noexcept
    {
        for (const auto& stage : state)
            if (std::abs (stage.ic1) > ringFloor || std::abs (stage.ic2) > ringFloor)
                return true;
        return false;
    }

    /** About how long `type` at `cutoffHz` and `resonance` rings on after
        its input stops, until it is quiet (see isRinging). */
    static double ringSeconds (Type type, float cutoffHz, float resonance) noexcept
    {
        // A resonance's ring falls by e every 2Q / (2 pi f) seconds; 80 dB is
        // about 9.2 of those.
        const double q = std::max (resonantQ (type, resonance), firstStageQ);
        return 9.2 * 2.0 * q / (2.0 * pi * std::max ((double) cutoffHz, (double) minHz));
    }

    /** Filters `numSamples` of `left` and `right` in place as `type`, the
        cutoff gliding to `cutoffHz` across them. */
    void process (float* left, float* right, int numSamples, Type type, float cutoffHz, float resonance) noexcept
    {
        if (numSamples <= 0)
            return;

        const double to   = warp (cutoffHz);
        const double from = primed ? g : to;
        g      = to;
        primed = true;

        const bool   twoStages = type == Type::low24 || type == Type::high24;
        const double k1        = 1.0 / firstStageQ;
        const double k2        = 1.0 / resonantQ (type, resonance);
        const double step      = (to - from) / (double) numSamples;

        for (int n = 0; n < numSamples; ++n)
        {
            const double gn = from + step * (double) (n + 1);
            const auto   c2 = Coefficients::of (gn, k2);
            if (twoStages)
            {
                const auto c1 = Coefficients::of (gn, k1);
                left[n]  = (float) twoStage (type, c1, c2, state[0], state[1], (double) left[n]);
                right[n] = (float) twoStage (type, c1, c2, state[2], state[3], (double) right[n]);
            }
            else
            {
                left[n]  = (float) oneStage (type, c2, state[0], (double) left[n]);
                right[n] = (float) oneStage (type, c2, state[2], (double) right[n]);
            }
        }
    }

    /** How loud (in dB) `type` at `cutoffHz` and `resonance` leaves a tone at
        `frequencyHz`: the analog filter this one models, for drawing it. */
    static float magnitudeDb (Type type, float cutoffHz, float resonance, float frequencyHz) noexcept
    {
        const std::complex<double> s (0.0, (double) frequencyHz / std::max ((double) cutoffHz, 1.0));
        const auto denominator = [&s] (double k) { return s * s + k * s + 1.0; };
        const double k1 = 1.0 / firstStageQ;
        const double k2 = 1.0 / resonantQ (type, resonance);

        std::complex<double> h;
        switch (type)
        {
            case Type::low12:  h = 1.0 / denominator (k2); break;
            case Type::high12: h = s * s / denominator (k2); break;
            case Type::band:   h = k2 * s / denominator (k2); break;
            case Type::notch:  h = (s * s + 1.0) / denominator (k2); break;
            case Type::low24:  h = 1.0 / (denominator (k1) * denominator (k2)); break;
            case Type::high24: h = (s * s) * (s * s) / (denominator (k1) * denominator (k2)); break;
        }
        return (float) (20.0 * std::log10 (std::max (std::abs (h), 1.0e-9)));
    }

    /** The resonant stage's Q for `resonance` (0..1): flat (Butterworth) at
        0, rising evenly in octaves to maxQ at 1. */
    static double resonantQ (Type type, float resonance) noexcept
    {
        const double flat = type == Type::low24 || type == Type::high24 ? 1.3066 : 0.7071;
        return flat * std::pow (maxQ / flat, (double) std::clamp (resonance, 0.0f, 1.0f));
    }

private:
    static constexpr double firstStageQ = 0.5412;   // with 1.3066, a 4-pole Butterworth
    static constexpr double ringFloor   = 1.0e-4;   // -80 dB: quiet enough to stop
    static constexpr double pi = 3.14159265358979323846;

    // One stage's memory: its two integrators.
    struct Stage
    {
        double ic1 = 0.0;
        double ic2 = 0.0;
    };

    struct Coefficients
    {
        double a1, a2, a3, k;

        static Coefficients of (double g, double k) noexcept
        {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            return { a1, g * a1, g * g * a1, k };
        }
    };

    // The three outputs a stage gives for input `x`.
    struct Outputs
    {
        double low, band, high;
    };

    static Outputs tick (const Coefficients& c, Stage& s, double x) noexcept
    {
        const double v3 = x - s.ic2;
        const double v1 = c.a1 * s.ic1 + c.a2 * v3;
        const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;
        return { v2, v1, x - c.k * v1 - v2 };
    }

    static double oneStage (Type type, const Coefficients& c, Stage& s, double x) noexcept
    {
        const auto out = tick (c, s, x);
        switch (type)
        {
            case Type::low12:  return out.low;
            case Type::high12: return out.high;
            case Type::band:   return c.k * out.band;
            case Type::notch:  return out.low + out.high;
            case Type::low24:
            case Type::high24: break;
        }
        return out.low;
    }

    static double twoStage (Type type, const Coefficients& first, const Coefficients& second, Stage& a, Stage& b, double x) noexcept
    {
        const bool low = type == Type::low24;
        const auto one = tick (first, a, x);
        const auto two = tick (second, b, low ? one.low : one.high);
        return low ? two.low : two.high;
    }

    double warp (float cutoffHz) const noexcept
    {
        const double hz = std::clamp ((double) cutoffHz, (double) minHz, 0.45 * sampleRate);
        return std::tan (pi * hz / sampleRate);
    }

    double sampleRate = 44100.0;
    double g          = 0.0;    // where the last chunk's cutoff ended (prewarped)
    bool   primed     = false;  // false until a chunk has set g
    std::array<Stage, 4> state {};   // left stages 1 and 2, right stages 1 and 2
};

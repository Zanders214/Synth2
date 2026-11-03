#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#include "Simd.h"

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
        sampleRate  = newSampleRate;
        warpedValid = false;
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
        const auto ringing = [] (double value) { return std::abs (value) > ringFloor; };
        return std::any_of (state.begin(), state.end(), [&ringing] (const auto& stage)
                            { return std::any_of (stage.ic1.begin(), stage.ic1.end(), ringing)
                                  || std::any_of (stage.ic2.begin(), stage.ic2.end(), ringing); });
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

        const double to   = warped (cutoffHz);
        const double from = primed ? g : to;
        g      = to;
        primed = true;

        const double k2 = damping (type, resonance);
        switch (type)
        {
            case Type::low24:  run<Type::low24>  (left, right, numSamples, from, to, k2); break;
            case Type::low12:  run<Type::low12>  (left, right, numSamples, from, to, k2); break;
            case Type::band:   run<Type::band>   (left, right, numSamples, from, to, k2); break;
            case Type::high12: run<Type::high12> (left, right, numSamples, from, to, k2); break;
            case Type::high24: run<Type::high24> (left, right, numSamples, from, to, k2); break;
            case Type::notch:  run<Type::notch>  (left, right, numSamples, from, to, k2); break;
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

    // One stage's memory: its two integrators, left and right.
    struct Stage
    {
        std::array<double, 2> ic1 {};
        std::array<double, 2> ic2 {};
    };

    template <typename Value>
    struct Memory
    {
        Value ic1;
        Value ic2;
    };

    struct Coefficients
    {
        double a1;
        double a2;
        double a3;
        double k;

        static Coefficients of (double g, double k) noexcept
        {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            return { a1, g * a1, g * g * a1, k };
        }
    };

    // The three outputs a stage gives for input `x`.
    template <typename Value>
    struct Outputs
    {
        Value low;
        Value band;
        Value high;
    };

    template <typename Value>
    static Outputs<Value> tick (const Coefficients& c, Memory<Value>& s, Value x) noexcept
    {
        const Value v3 = x - s.ic2;
        const Value v1 = Value (c.a1) * s.ic1 + Value (c.a2) * v3;
        const Value v2 = s.ic2 + Value (c.a2) * s.ic1 + Value (c.a3) * v3;
        s.ic1 = Value (2.0) * v1 - s.ic1;
        s.ic2 = Value (2.0) * v2 - s.ic2;
        return { v2, v1, x - Value (c.k) * v1 - v2 };
    }

    template <Type type>
    static constexpr bool hasTwoStages = type == Type::low24 || type == Type::high24;

    template <Type type, typename Value>
    static Value filterOne (Value x, const Coefficients& first, const Coefficients& second, Memory<Value>& a, Memory<Value>& b) noexcept
    {
        if constexpr (hasTwoStages<type>)
        {
            constexpr bool low = type == Type::low24;
            const auto one = tick (first, a, x);
            const auto two = tick (second, b, low ? one.low : one.high);
            return low ? two.low : two.high;
        }
        else
        {
            const auto out = tick (second, a, x);
            if constexpr (type == Type::high12)
                return out.high;
            else if constexpr (type == Type::band)
                return Value (second.k) * out.band;
            else if constexpr (type == Type::notch)
                return out.low + out.high;
            else
                return out.low;
        }
    }

    template <Type type, bool glide, typename Value, typename Frame>
    static void filterFrames (int numSamples, double from, double to, double k2, Memory<Value>& a, Memory<Value>& b, Frame&& frame) noexcept
    {
        const double k1   = 1.0 / firstStageQ;
        const double step = (to - from) / (double) numSamples;
        auto second = Coefficients::of (to, k2);
        auto first  = hasTwoStages<type> ? Coefficients::of (to, k1) : second;
        for (int n = 0; n < numSamples; ++n)
        {
            if constexpr (glide)
            {
                const double gn = from + step * (double) (n + 1);
                second = Coefficients::of (gn, k2);
                first  = hasTwoStages<type> ? Coefficients::of (gn, k1) : second;
            }
            frame (n, [&] (Value x) { return filterOne<type> (x, first, second, a, b); });
        }
    }

    template <Type type, typename Value, typename Frame>
    static void runFrames (int numSamples, double from, double to, double k2, Memory<Value>& a, Memory<Value>& b, Frame&& frame) noexcept
    {
        if (from < to || from > to)
            filterFrames<type, true> (numSamples, from, to, k2, a, b, frame);
        else
            filterFrames<type, false> (numSamples, from, to, k2, a, b, frame);
    }

    template <Type type>
    void run (float* left, float* right, int numSamples, double from, double to, double k2) noexcept
    {
#if SYNTH2_SSE2
        using Pair = Simd::Doubles;
        Memory<Pair> a { _mm_loadu_pd (state[0].ic1.data()), _mm_loadu_pd (state[0].ic2.data()) };
        Memory<Pair> b { _mm_loadu_pd (state[1].ic1.data()), _mm_loadu_pd (state[1].ic2.data()) };
        runFrames<type> (numSamples, from, to, k2, a, b, [left, right] (int n, auto&& filter)
        {
            const Pair y = filter (Pair (_mm_setr_pd ((double) left[n], (double) right[n])));
            left[n]  = (float) y.first();
            right[n] = (float) y.second();
        });
        _mm_storeu_pd (state[0].ic1.data(), a.ic1.v);
        _mm_storeu_pd (state[0].ic2.data(), a.ic2.v);
        _mm_storeu_pd (state[1].ic1.data(), b.ic1.v);
        _mm_storeu_pd (state[1].ic2.data(), b.ic2.v);
#else
        for (size_t side = 0; side < 2; ++side)
        {
            float* channel = side == 0 ? left : right;
            Memory<double> a { state[0].ic1[side], state[0].ic2[side] };
            Memory<double> b { state[1].ic1[side], state[1].ic2[side] };
            runFrames<type> (numSamples, from, to, k2, a, b, [channel] (int n, auto&& filter)
            {
                channel[n] = (float) filter ((double) channel[n]);
            });
            state[0].ic1[side] = a.ic1;
            state[0].ic2[side] = a.ic2;
            state[1].ic1[side] = b.ic1;
            state[1].ic2[side] = b.ic2;
        }
#endif
    }

    double warp (float cutoffHz) const noexcept
    {
        const double hz = std::clamp ((double) cutoffHz, (double) minHz, 0.45 * sampleRate);
        return std::tan (pi * hz / sampleRate);
    }

    double warped (float cutoffHz) noexcept
    {
        if (! warpedValid || cutoffHz < warpedHz || cutoffHz > warpedHz)
        {
            warpedValid = true;
            warpedHz    = cutoffHz;
            warpedG     = warp (cutoffHz);
        }
        return warpedG;
    }

    double damping (Type type, float resonance) noexcept
    {
        if (! dampingValid || type != dampedType || resonance < dampedResonance || resonance > dampedResonance)
        {
            dampingValid    = true;
            dampedType      = type;
            dampedResonance = resonance;
            dampingK        = 1.0 / resonantQ (type, resonance);
        }
        return dampingK;
    }

    double sampleRate = 44100.0;
    double g          = 0.0;    // where the last chunk's cutoff ended (prewarped)
    bool   primed     = false;  // false until a chunk has set g
    std::array<Stage, 2> state {};   // stages 1 and 2, each left and right

    bool   warpedValid     = false;
    float  warpedHz        = 0.0f;
    double warpedG         = 0.0;
    bool   dampingValid    = false;
    Type   dampedType      = Type::low24;
    float  dampedResonance = 0.0f;
    double dampingK        = 0.0;
};

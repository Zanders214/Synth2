#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
    Room

    The reverb's room: a stereo pair in, the room's sound out (no dry). Built
    the way modern algorithmic reverbs are, rather than Freeverb's handful of
    combs, whose few strong echo paths ring metallic on held synth chords:

        in -> low cut -> PRE-DELAY -> diffuser (4 steps) -> feedback network (8 lines) -> out
                                           \______________ early reflections ______________/

    - The diffuser smears each sound into a dense cloud within a few tens of
      milliseconds: each step delays eight channels by different amounts,
      shuffles them, flips some and mixes them all into each other (a
      Hadamard matrix), so echoes multiply eightfold per step.
    - The feedback network keeps that cloud ringing: eight delay lines of
      55 to 110 ms, mixed back into each other (a Householder matrix) and
      each turned down just enough to fall 60 dB over the ring time SIZE
      asks for. Each line's length drifts very slightly and slowly, which
      keeps any one pitch from ringing out (the metallic sound), and each
      is darkened by DAMPING.
    - The tail is levelled so a long room isn't much louder than a short
      one, only longer.

    Framework-free, so it is tested on its own. Changing SIZE, DAMPING or
    PRE-DELAY glides over about 50 ms.
*/
namespace Effects
{
    class Room
    {
    public:
        static constexpr int    channels      = 8;
        static constexpr double diffusionMs   = 70.0;   // the four diffusion steps together
        static constexpr double shortestMs    = 55.0;   // the feedback lines run from here to twice it
        static constexpr double driftMs       = 0.35;   // how far each line's length drifts
        static constexpr double lowCutHz      = 140.0;

        /** The room to aim for: how long it rings (to -60 dB), how dull it
            gets, how wide it spreads (0..1) and how long it waits. */
        struct Shape
        {
            double seconds    = 2.0;
            float  damping    = 0.4f;
            float  width      = 1.0f;
            double preDelayMs = 20.0;
        };

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;

            // The same room every time: a fixed seed for its lengths and flips.
            std::uint32_t seed = 0x2545f491u;
            const auto random = [&seed]
            {
                seed = seed * 1664525u + 1013904223u;
                return (double) (seed >> 8) / 16777216.0;
            };

            double range = diffusionMs;
            for (size_t s = 0; s < diffusion.size(); ++s)
            {
                auto& step = diffusion[s];
                range *= 0.5;   // 35, 17.5, 8.75 and 4.4 ms
                for (size_t c = 0; c < (size_t) channels; ++c)
                {
                    const double ms = range * ((double) c + random()) / (double) channels;
                    step.delay[c] = std::max<size_t> (1, (size_t) (ms * 0.001 * sampleRate));
                    step.flip[c]  = random() < 0.5 ? -1.0f : 1.0f;
                    step.lines[c].resize (step.delay[c] + 1);
                }
                step.shift = 1 + s * 3;
            }

            for (size_t c = 0; c < (size_t) channels; ++c)
            {
                auto& line = feedback[c];
                const double ms = shortestMs * std::pow (2.0, ((double) c + 0.5 * random()) / (double) channels);
                line.length = ms * 0.001 * sampleRate;
                line.line.resize ((size_t) (line.length + 2.0 * driftMs * 0.001 * sampleRate) + 4);

                // Each line drifts at its own slow rate, from its own phase.
                const double rate  = 0.3 + 0.6 * ((double) c + random()) / (double) channels;
                const double phase = random() * 6.283185307179586;
                line.turn = { (float) std::cos (6.283185307179586 * rate / sampleRate),
                              (float) std::sin (6.283185307179586 * rate / sampleRate) };
                line.wobble = { (float) std::cos (phase), (float) std::sin (phase) };
            }
            meanLength = 0.0;
            for (const auto& line : feedback)
                meanLength += line.length / (double) channels;

            for (auto& line : pre)
                line.resize ((size_t) ((double) Effects::maxPreDelayMs * 0.001 * sampleRate) + 4);

            drift    = driftMs * 0.001 * sampleRate;
            lowCut   = (float) (1.0 - std::exp (-6.283185307179586 * lowCutHz / sampleRate));
            current  = target;
            preDelay = targetPreDelay();
            clear();
        }

        void set (const Shape& shape) noexcept
        {
            target.seconds    = std::clamp (shape.seconds, 0.1, 30.0);
            target.damping    = std::clamp (shape.damping, 0.0f, 1.0f);
            target.width      = std::clamp (shape.width, 0.0f, 1.0f);
            target.preDelayMs = std::clamp (shape.preDelayMs, 0.0, (double) Effects::maxPreDelayMs);
        }

        /** Replaces the pair with the room's sound, in place. */
        void process (float* left, float* right, int numSamples) noexcept
        {
            if (feedback[0].line.buffer.empty())
                return;
            glide (numSamples);

            for (int n = 0; n < numSamples; ++n)
            {
                // Low cut, then the pre-delay.
                inputLow[0] += lowCut * (left[n] - inputLow[0]);
                inputLow[1] += lowCut * (right[n] - inputLow[1]);
                const float inL = left[n] - inputLow[0];
                const float inR = right[n] - inputLow[1];
                preDelay += preGlide * (targetPreDelay() - preDelay);
                const float waitedL = pre[0].tap (preDelay);
                const float waitedR = pre[1].tap (preDelay);
                pre[0].push (inL);
                pre[1].push (inR);

                Frame x {};
                for (size_t c = 0; c < (size_t) channels; ++c)
                    x[c] = c % 2 == 0 ? waitedL : waitedR;
                diffuse (x);

                // The feedback network: read each line, darken it, turn it
                // down, mix them into each other and add the new sound.
                Frame out {};
                float sum = 0.0f;
                for (size_t c = 0; c < (size_t) channels; ++c)
                {
                    auto& line = feedback[c];
                    line.wobble = { line.wobble[0] * line.turn[0] - line.wobble[1] * line.turn[1],
                                    line.wobble[0] * line.turn[1] + line.wobble[1] * line.turn[0] };
                    const float read = line.line.tap (line.length + drift * (double) line.wobble[1]);
                    line.dull += dullness * (read - line.dull);
                    out[c] = line.dull;
                    sum += gains[c] * out[c];
                }
                const float mixBack = sum * (2.0f / (float) channels);
                for (size_t c = 0; c < (size_t) channels; ++c)
                    feedback[c].line.push (x[c] + gains[c] * out[c] - mixBack);

                // Out: the tail, levelled, over a touch of the early cloud;
                // the even channels to the left, the odd to the right.
                float wetL = 0.0f;
                float wetR = 0.0f;
                for (size_t c = 0; c < (size_t) channels; c += 2)
                {
                    wetL += level * out[c] + earlyLevel * x[c];
                    wetR += level * out[c + 1] + earlyLevel * x[c + 1];
                }
                const float mid  = 0.5f * (wetL + wetR);
                const float side = 0.5f * (wetL - wetR) * current.width;
                left[n]  = outputLevel * (mid + side);
                right[n] = outputLevel * (mid - side);
            }
            for (auto& line : feedback)
                line.steady();
        }

        /** Lets the room fall silent at once. */
        void clear() noexcept
        {
            for (auto& step : diffusion)
                for (auto& line : step.lines)
                    line.clear();
            for (auto& line : feedback)
            {
                line.line.clear();
                line.dull = 0.0f;
            }
            for (auto& line : pre)
                line.clear();
            inputLow = {};
        }

    private:
        using Frame = std::array<float, channels>;

        // Overall level, set so noise through the default room comes out
        // near the level it went in.
        static constexpr float outputLevel = 1.3f;
        static constexpr float earlyLevel  = 0.18f;

        /** A delay line, read before it is written. */
        struct Line
        {
            std::vector<float> buffer;
            size_t mask = 0;
            size_t at   = 0;

            void resize (size_t length)
            {
                size_t size = 1;
                while (size < length + 2)
                    size <<= 1;
                buffer.assign (size, 0.0f);
                mask = size - 1;
                at   = 0;
            }

            void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }
            void push (float x) noexcept { buffer[at] = x; at = (at + 1) & mask; }

            // `delay` samples back (at least one), whole.
            float tap (size_t delay) const noexcept { return buffer[(at - delay) & mask]; }

            // `delay` samples back (at least one), between samples.
            float tap (double delay) const noexcept
            {
                const double position = (double) at - delay;
                const double whole    = std::floor (position);
                const auto   index    = (size_t) (long long) whole;
                const auto   frac     = (float) (position - whole);
                const float  a        = buffer[index & mask];
                const float  b        = buffer[(index + 1) & mask];
                return a + frac * (b - a);
            }
        };

        struct Step
        {
            std::array<Line, channels>   lines;
            std::array<size_t, channels> delay {};
            std::array<float, channels>  flip {};
            size_t shift = 1;
        };

        struct FeedbackLine
        {
            Line   line;
            double length = 1.0;   // in samples
            float  dull   = 0.0f;  // DAMPING's lowpass
            std::array<float, 2> wobble { 1.0f, 0.0f };   // the drift's phase, as a point on a circle
            std::array<float, 2> turn   { 1.0f, 0.0f };   // how far it turns each sample

            // Keeps the drift's circle from creeping out of round.
            void steady() noexcept
            {
                const float radius = std::sqrt (wobble[0] * wobble[0] + wobble[1] * wobble[1]);
                if (radius > 0.0f)
                    wobble = { wobble[0] / radius, wobble[1] / radius };
            }
        };

        // Delays each channel, shuffles and flips them and mixes them all
        // into each other, four times over.
        void diffuse (Frame& x) noexcept
        {
            for (auto& step : diffusion)
            {
                Frame delayed {};
                for (size_t c = 0; c < (size_t) channels; ++c)
                {
                    delayed[c] = step.lines[c].tap (step.delay[c]);
                    step.lines[c].push (x[c]);
                }
                for (size_t c = 0; c < (size_t) channels; ++c)
                    x[c] = delayed[(c + step.shift) % (size_t) channels] * step.flip[c];
                hadamard (x);
            }
        }

        // Every channel into every other, keeping the energy: the
        // Walsh-Hadamard butterflies.
        static void hadamard (Frame& x) noexcept
        {
            for (size_t half = 1; half < (size_t) channels; half *= 2)
                for (size_t start = 0; start < (size_t) channels; start += 2 * half)
                    for (size_t i = start; i < start + half; ++i)
                    {
                        const float a = x[i];
                        const float b = x[i + half];
                        x[i]        = a + b;
                        x[i + half] = a - b;
                    }
            const float scale = 1.0f / std::sqrt ((float) channels);
            for (auto& v : x)
                v *= scale;
        }

        double targetPreDelay() const noexcept
        {
            return std::max (1.0, target.preDelayMs * 0.001 * sampleRate);
        }

        // Moves the room toward its target (once a call: the engine's chunks
        // are at most 32 samples), and works out what follows from it.
        void glide (int numSamples) noexcept
        {
            const double toward = 1.0 - std::exp (-(double) numSamples / (0.05 * sampleRate));
            current.seconds += toward * (target.seconds - current.seconds);
            current.damping += (float) toward * (target.damping - current.damping);
            current.width   += (float) toward * (target.width - current.width);
            preGlide = (float) (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));

            for (size_t c = 0; c < (size_t) channels; ++c)
                gains[c] = (float) std::pow (10.0, -3.0 * feedback[c].length / (current.seconds * sampleRate));
            const double meanGain = std::pow (10.0, -3.0 * meanLength / (current.seconds * sampleRate));
            level = 0.5f * (float) std::sqrt (1.0 - meanGain * meanGain);

            const double cutoff = std::min (0.45 * sampleRate, 18000.0 * std::pow (0.06, (double) current.damping));
            dullness = (float) (1.0 - std::exp (-6.283185307179586 * cutoff / sampleRate));
        }

        double sampleRate = 44100.0;
        std::array<Step, 4> diffusion;
        std::array<FeedbackLine, channels> feedback;
        std::array<Line, 2> pre;
        std::array<float, channels> gains {};
        std::array<float, 2> inputLow {};
        Shape  target;
        Shape  current;
        double meanLength = 1.0;
        double drift      = 0.0;
        double preDelay   = 1.0;
        float  preGlide   = 1.0f;
        float  lowCut     = 0.0f;
        float  dullness   = 1.0f;
        float  level      = 0.5f;
    };
}

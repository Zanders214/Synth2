#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "EffectSettings.h"
#include "Room.h"

/**
    Effects

    The effects every note goes through together, after the mix and before
    OUTPUT: a stereo DELAY and a REVERB, one after the other in either order.

        notes  ->  +  ->  DELAY  ->  REVERB  ->  OUTPUT     (or REVERB first)

    Each has its own switch and MIX. MIX keeps the dry sound at full level up
    to the middle, adding the effect underneath it, and from there fades the
    dry sound out until only the effect is left. Switching an effect on or
    off fades it in or out over 20 ms, so it never clicks; once it is off and
    faded out it costs nothing, leaves the sound untouched, and forgets what
    it was holding, so switching it back on starts clean.

    DELAY repeats the sound after TIME, either in milliseconds or (with SYNC)
    as a note length at the host's tempo. Each repeat comes back FEEDBACK as
    loud as the one before and a little darker, as TONE says: at the top the
    repeats stay bright, at the bottom each one loses its highs. Like tape,
    each pass also loses its deepest lows (so the repeats don't pile up into
    mud), rounds off gently when driven hard, and wavers very slightly in
    pitch, so the repeats sit behind the sound rather than copying it. With
    PING-PONG the repeats bounce from left to right. Turning TIME while
    repeats ring glides there, bending their pitch like tape.

    REVERB is a feedback-delay-network room (see Room.h): SIZE sets how long
    it rings, PRE-DELAY how long it waits before it starts, DAMPING how
    quickly its highs die away, and WIDTH how wide it spreads.

    Kept free of any host plumbing, like the engine that runs it.
*/
namespace Effects
{
    /** An effect's switch and MIX: how much of the dry sound and of the
        effect go on, gliding to new settings sample by sample. */
    class Blend
    {
    public:
        struct Gains
        {
            float dry = 1.0f;
            float wet = 0.0f;
        };

        void prepare (double sampleRate) noexcept
        {
            enable.reset (sampleRate, fadeSeconds);
            mix.reset (sampleRate, fadeSeconds);
            enable.setCurrentAndTargetValue (enable.getTargetValue());
            mix.setCurrentAndTargetValue (mix.getTargetValue());
        }

        void set (bool on, float newMix) noexcept
        {
            enable.setTargetValue (on ? 1.0f : 0.0f);
            mix.setTargetValue (std::clamp (newMix, 0.0f, 1.0f));
        }

        /** Off and faded out: the effect has nothing to do. */
        bool isIdle() const noexcept { return ! enable.isSmoothing() && enable.getTargetValue() < 0.5f; }

        Gains next() noexcept
        {
            const float on     = enable.getNextValue();
            const float amount = mix.getNextValue();
            return { 1.0f - on + on * dryGain (amount), on * wetGain (amount) };
        }

    private:
        juce::SmoothedValue<float> enable { 0.0f };
        juce::SmoothedValue<float> mix    { 0.0f };
    };

    /** The stereo delay: a line per side, read between samples so TIME can glide. */
    class Delay
    {
    public:
        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;
            const auto size = (size_t) juce::nextPowerOfTwo ((int) std::ceil (maxSeconds * sampleRate) + 4);
            for (auto& line : lines)
                line.assign (size, 0.0f);
            mask = size - 1;

            blend.prepare (sampleRate);
            for (auto* value : { &feedback, &tone })
            {
                value->reset (sampleRate, fadeSeconds);
                value->setCurrentAndTargetValue (value->getTargetValue());
            }
            // TIME glides with a 50 ms time constant.
            wow.prepare (sampleRate);
            time.glide   = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
            time.current = time.target = targetSamples();
            clear();
        }

        void set (const DelaySettings& newSettings, double newBpm) noexcept
        {
            settings = newSettings;
            bpm      = newBpm;
            blend.set (settings.on, settings.mix);
            feedback.setTargetValue (std::clamp (settings.feedback, 0.0f, maxFeedback));
            tone.setTargetValue (std::clamp (settings.tone, 0.0f, 1.0f));
            time.target = targetSamples();
        }

        void process (float* left, float* right, int numSamples) noexcept
        {
            if (blend.isIdle() || lines[0].empty())   // off, or not prepared yet
            {
                if (holding)
                    clear();
                return;
            }
            if (! holding)
                time.current = time.target;   // starting afresh: no glide from a stale time
            holding = true;

            // TONE moves once per call (the engine's chunks are at most 32
            // samples): a one-pole lowpass from 1 kHz up to 18 kHz.
            tone.skip (numSamples);
            const double cutoff = 1000.0 * std::pow (18.0, (double) tone.getCurrentValue());
            const auto   darken = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / sampleRate));

            for (int n = 0; n < numSamples; ++n)
            {
                time.current += time.glide * (time.target - time.current);
                wow.turn();
                std::array<float, 2> wet {};
                for (size_t side = 0; side < 2; ++side)
                {
                    // Each side wavers a quarter turn apart from the other.
                    const double wavered = time.current + wow.depth * (double) (side == 0 ? wow.at[1] : wow.at[0]);
                    lowpass[side] += darken * (read (side, wavered) - lowpass[side]);
                    lowCut[side]  += wow.lowCut * (lowpass[side] - lowCut[side]);
                    wet[side] = lowpass[side] - lowCut[side];
                }
                const float wetL = wet[0];
                const float wetR = wet[1];
                const float back = feedback.getNextValue();
                const float inL  = left[n];
                const float inR  = right[n];

                // Ping-pong feeds both sides into the left line, then crosses
                // each line's repeats into the other.
                if (settings.pingPong)
                {
                    lines[0][writeAt] = saturate (0.5f * (inL + inR) + back * wetR);
                    lines[1][writeAt] = saturate (back * wetL);
                }
                else
                {
                    lines[0][writeAt] = saturate (inL + back * wetL);
                    lines[1][writeAt] = saturate (inR + back * wetR);
                }
                writeAt = (writeAt + 1) & mask;

                const auto gains = blend.next();
                left[n]  = gains.dry * inL + gains.wet * wetL;
                right[n] = gains.dry * inR + gains.wet * wetR;
            }
        }

        /** Forgets every repeat. */
        void clear() noexcept
        {
            for (auto& line : lines)
                std::fill (line.begin(), line.end(), 0.0f);
            lowpass = {};
            lowCut  = {};
            writeAt = 0;
            holding = false;
        }

        /** How far the repeats waver either way, in milliseconds. */
        static constexpr double wowMs = 0.25;

    private:
        double targetSamples() const noexcept
        {
            const double longest = lines[0].empty() ? 1.0 : (double) lines[0].size() - 3.0 - wow.depth;
            return std::clamp (delaySeconds (settings, bpm) * sampleRate, 1.0 + wow.depth, longest);
        }

        // Tape's gentle rounding: all but untouched at ordinary levels, and
        // loud repeats are squeezed rather than clipped.
        static float saturate (float x) noexcept { return 2.0f * std::tanh (0.5f * x); }

        // Line `side`, `delay` samples back, between samples.
        float read (size_t side, double delay) const noexcept
        {
            const double at    = (double) writeAt - delay + (double) lines[side].size();
            const auto   whole = (size_t) at;
            const auto   frac  = (float) (at - (double) whole);
            const float  a     = lines[side][whole & mask];
            const float  b     = lines[side][(whole + 1) & mask];
            return a + frac * (b - a);
        }

        struct Time
        {
            double current = 1.0;   // in samples
            double target  = 1.0;
            double glide   = 1.0;
        };

        // The tape's slow waver (a point turning round a circle, 0.45 times
        // a second), and the low cut on each pass.
        struct Wow
        {
            std::array<float, 2> at   { 1.0f, 0.0f };
            std::array<float, 2> step { 1.0f, 0.0f };
            double depth  = 0.0;    // in samples
            float  lowCut = 0.0f;

            void prepare (double rate)
            {
                const double angle = juce::MathConstants<double>::twoPi * 0.45 / rate;
                step   = { (float) std::cos (angle), (float) std::sin (angle) };
                at     = { 1.0f, 0.0f };
                depth  = wowMs * 0.001 * rate;
                lowCut = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 120.0 / rate));
            }

            void turn() noexcept
            {
                at = { at[0] * step[0] - at[1] * step[1], at[0] * step[1] + at[1] * step[0] };
                const float radius = std::sqrt (at[0] * at[0] + at[1] * at[1]);
                at = { at[0] / radius, at[1] / radius };
            }
        };

        DelaySettings settings;
        double sampleRate = 44100.0;
        double bpm        = 120.0;
        std::array<std::vector<float>, 2> lines;
        size_t mask    = 0;
        size_t writeAt = 0;
        Time   time;
        Wow    wow;
        std::array<float, 2> lowpass {};
        std::array<float, 2> lowCut {};
        juce::SmoothedValue<float> feedback { 0.35f };
        juce::SmoothedValue<float> tone     { 0.6f };
        Blend  blend;
        bool   holding = false;   // something may be in the lines
    };

    /** The reverb: the Room (see Room.h), mixed in by its Blend. */
    class Reverb
    {
    public:
        void prepare (double sampleRate)
        {
            room.prepare (sampleRate);
            blend.prepare (sampleRate);
            clear();
        }

        void set (const ReverbSettings& settings) noexcept
        {
            blend.set (settings.on, settings.mix);
            room.set ({ reverbSeconds (settings.size), settings.damping, settings.width, (double) settings.preDelayMs });
        }

        void process (float* left, float* right, int numSamples) noexcept
        {
            if (blend.isIdle())
            {
                if (holding)
                    clear();
                return;
            }
            holding = true;

            for (int done = 0; done < numSamples;)
            {
                const int length = std::min (chunk, numSamples - done);
                std::array<float, chunk> wetL {};
                std::array<float, chunk> wetR {};
                std::copy (left + done, left + done + length, wetL.begin());
                std::copy (right + done, right + done + length, wetR.begin());
                room.process (wetL.data(), wetR.data(), length);

                for (int n = 0; n < length; ++n)
                {
                    const auto gains = blend.next();
                    const auto i     = (size_t) n;
                    left [done + n] = gains.dry * left [done + n] + gains.wet * wetL[i];
                    right[done + n] = gains.dry * right[done + n] + gains.wet * wetR[i];
                }
                done += length;
            }
        }

        /** Lets the room fall silent at once. */
        void clear() noexcept
        {
            room.clear();
            holding = false;
        }

    private:
        static constexpr int chunk = 32;

        Room  room;
        Blend blend;
        bool  holding = false;
    };

    /** Both effects, in the order the settings ask for. */
    class Chain
    {
    public:
        void prepare (double sampleRate)
        {
            delay.prepare (sampleRate);
            reverb.prepare (sampleRate);
        }

        void set (const Settings& settings) noexcept
        {
            delay.set (settings.delay, settings.bpm);
            reverb.set (settings.reverb);
            reverbFirst = settings.reverbFirst;
        }

        /** Runs the stereo pair through both effects, in place. */
        void process (float* left, float* right, int numSamples) noexcept
        {
            if (reverbFirst)
            {
                reverb.process (left, right, numSamples);
                delay.process (left, right, numSamples);
            }
            else
            {
                delay.process (left, right, numSamples);
                reverb.process (left, right, numSamples);
            }
        }

        void clear() noexcept
        {
            delay.clear();
            reverb.clear();
        }

    private:
        Delay  delay;
        Reverb reverb;
        bool   reverbFirst = false;
    };
}

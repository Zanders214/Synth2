#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

#include "Oscillator.h"
#include "Unison.h"

/** How every note sounds right now: the engine hands these to each voice once
    per sub-block, already smoothed. */
struct VoiceSettings
{
    Waveform wave        = Waveform::saw;
    int      voices      = 1;      // unison copies per note, 1..Unison::maxVoices
    float    detuneCents = 0.0f;   // the outer copies' offset, 0..Unison::maxDetuneCents
    float    width       = 0.0f;   // stereo spread, 0..1
};

/**
    SynthVoice

    One note: a stack of up to 16 unison oscillators (see Unison.h) under an
    attack / release envelope.

    Changing VOICES while a note sounds fades copies in and out over 5 ms
    rather than switching them, and the stack's level and the copies' pans
    glide to their new values, so turning the knob mid-chord doesn't click.
    A copy that is leaving keeps its last detune and pan until it has gone.
*/
class SynthVoice
{
public:
    static constexpr int    maxChunk    = 32;      // the most samples render() takes at once
    static constexpr float  noteLevel   = 0.25f;   // one note at full velocity, before the stack scaling
    static constexpr double fadeSeconds = 0.005;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envelope.setSampleRate (newSampleRate);
        fadeStep = (float) (1.0 / (fadeSeconds * newSampleRate));
        kill();
    }

    void setEnvelope (const juce::ADSR::Parameters& parameters) { envelope.setParameters (parameters); }

    /** Plays `midiNote`. A voice taken over while still sounding glides its
        envelope on from where it was instead of jumping back to silence. */
    void start (int midiNote, float velocity, std::uint64_t order, const VoiceSettings& settings, juce::Random& random)
    {
        if (! isActive())
        {
            // Unison copies start at random points in their cycles, so they
            // don't all line up and thump on the attack. A lone voice starts on
            // the zero crossing at the top of the cycle.
            const int count = Unison::clampVoices (settings.voices);
            for (size_t i = 0; i < oscillators.size(); ++i)
            {
                oscillators[i].reset (i == 0 && count == 1 ? 0.0 : random.nextDouble());
                fades[i] = (int) i < count ? 1.0f : 0.0f;
                pans[i]  = Unison::panGains (Unison::pan ((int) i, count, settings.width));
            }
            stackLevel = Unison::stackGain ((float) count);
        }

        note       = midiNote;
        level      = noteLevel * juce::jlimit (0.0f, 1.0f, velocity);
        age        = order;
        held       = true;
        sustained  = false;
        noteHz     = 440.0 * std::exp2 ((midiNote - 69) / 12.0);
        envelope.noteOn();
    }

    /** The key went up (and the sustain pedal, if any, is up too). */
    void release()
    {
        held      = false;
        sustained = false;
        envelope.noteOff();
    }

    /** The key went up while the sustain pedal is down: keep sounding until it lifts. */
    void sustain()
    {
        held      = false;
        sustained = true;
    }

    /** Stops dead, with no release. */
    void kill()
    {
        envelope.reset();
        note      = -1;
        held      = false;
        sustained = false;
    }

    bool isActive() const noexcept      { return note >= 0; }
    bool isHeld() const noexcept        { return held; }
    bool isSustained() const noexcept   { return sustained; }
    bool isReleasing() const noexcept   { return isActive() && ! held && ! sustained; }
    int  getNote() const noexcept       { return note; }
    std::uint64_t getAge() const noexcept { return age; }

    /** Adds `numSamples` (at most maxChunk) of this note into left / right. */
    void render (float* left, float* right, int numSamples, const VoiceSettings& settings) noexcept
    {
        if (! isActive())
            return;

        const int count = Unison::clampVoices (settings.voices);
        std::array<float, maxChunk> stackL {};
        std::array<float, maxChunk> stackR {};
        const float ramp = 1.0f / (float) numSamples;

        for (size_t i = 0; i < oscillators.size(); ++i)
        {
            const float target = (int) i < count ? 1.0f : 0.0f;
            if (fades[i] <= 0.0f && target <= 0.0f)
                continue;

            // A copy that is leaving keeps its last place. The others move to
            // theirs across this chunk, so a new VOICES count re-pans them
            // without a jump.
            const auto from = pans[i];
            if (target > 0.0f)
            {
                cents[i] = Unison::detuneCents ((int) i, count, settings.detuneCents);
                pans[i]  = Unison::panGains (Unison::pan ((int) i, count, settings.width));
            }

            auto& osc = oscillators[i];
            osc.setIncrement (noteHz * Unison::ratio (cents[i]) / sampleRate);

            float fade = fades[i];
            const auto to = pans[i];
            for (int n = 0; n < numSamples; ++n)
            {
                const float t     = (float) (n + 1) * ramp;
                const float value = osc.next (settings.wave) * fade;
                stackL[(size_t) n] += value * (from.left  + (to.left  - from.left)  * t);
                stackR[(size_t) n] += value * (from.right + (to.right - from.right) * t);
                fade = target > fade ? juce::jmin (target, fade + fadeStep)
                                     : juce::jmax (target, fade - fadeStep);
            }
            fades[i] = fade;
        }

        // The stack's level follows the copies actually sounding (fading ones
        // count in part), gliding across the chunk, so it never jumps.
        float sounding = 0.0f;
        for (const float fade : fades)
            sounding += fade;
        const float stackFrom = stackLevel;
        stackLevel = Unison::stackGain (sounding);

        for (int n = 0; n < numSamples; ++n)
        {
            const float stack = stackFrom + (stackLevel - stackFrom) * (float) (n + 1) * ramp;
            const float gain  = envelope.getNextSample() * level * stack;
            left[n]  += stackL[(size_t) n] * gain;
            right[n] += stackR[(size_t) n] * gain;
        }

        if (! envelope.isActive())
            kill();
    }

private:
    std::array<BlepOscillator, Unison::maxVoices>    oscillators {};
    std::array<float, Unison::maxVoices>             fades {};
    std::array<float, Unison::maxVoices>             cents {};
    std::array<Unison::PanGains, Unison::maxVoices>  pans {};
    juce::ADSR envelope;

    double sampleRate = 44100.0;
    double noteHz     = 440.0;
    float  fadeStep   = 0.0f;
    float  level      = 0.0f;
    float  stackLevel = 1.0f;
    int    note       = -1;
    bool   held       = false;
    bool   sustained  = false;
    std::uint64_t age = 0;
};

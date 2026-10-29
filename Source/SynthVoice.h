#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cstdint>

#include "Oscillator.h"
#include "Unison.h"

/** Where a note is in its envelope, for the editor's AMP display. */
struct NoteActivity
{
    enum class Stage { idle, attack, hold, release };

    Stage stage       = Stage::idle;
    float level       = 0.0f;   // the envelope, 0..1
    float releaseFrom = 1.0f;   // the envelope when the release began
};

/** How every note sounds right now: the engine hands these to each voice once
    per sub-block, already smoothed. */
struct VoiceSettings
{
    static constexpr int numOscillators = 4;

    int      voices      = 1;      // unison copies per note, 1..Unison::maxVoices
    float    detuneCents = 0.0f;   // the outer copies' offset, 0..Unison::maxDetuneCents
    float    width       = 0.0f;   // stereo spread, 0..1

    // Each oscillator's shape, pitch against the note (from its PITCH and
    // FINE) and level. An oscillator is silent at level 0, and isn't run at
    // all once it has faded there.
    struct Oscillator
    {
        Waveform wave  = Waveform::saw;
        float    ratio = 1.0f;
        float    level = 0.0f;
    };
    std::array<Oscillator, numOscillators> oscillators {};
};

/**
    SynthVoice

    One note: a stack of up to 16 unison copies (see Unison.h) under an
    attack / release envelope. Each copy plays every oscillator that is on (up
    to four), each at its own pitch and level, sharing the copy's detune and
    pan.

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
    static constexpr int    numOscillators = VoiceSettings::numOscillators;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envelope.setSampleRate (newSampleRate);
        fadeStep = (float) (1.0 / (fadeSeconds * newSampleRate));
        kill();
    }

    void setEnvelope (const juce::ADSR::Parameters& parameters) { envelope.setParameters (parameters); }

    /** Plays `midiNote` on MIDI `midiChannel` (1..16). A voice taken over while
        still sounding glides its envelope on from where it was instead of
        jumping back to silence. */
    void start (int midiChannel, int midiNote, float velocity, std::uint64_t order, const VoiceSettings& settings, juce::Random& random)
    {
        if (! isActive())
        {
            // Unison copies start at random points in their cycles, so they
            // don't all line up and thump on the attack. A lone voice starts on
            // the zero crossing at the top of the cycle.
            const int count = Unison::clampVoices (settings.voices);
            for (size_t i = 0; i < copies.size(); ++i)
            {
                auto& copy = copies[i];
                const bool lone = i == 0 && count == 1;
                for (auto& osc : copy.oscillators)
                    osc.reset (lone ? 0.0 : random.nextDouble());
                copy.fade = (int) i < count ? 1.0f : 0.0f;
                copy.pan  = Unison::panGains (Unison::pan ((int) i, count, settings.width));
            }
            stackLevel = Unison::stackGain ((float) count);
            for (size_t k = 0; k < oscLevels.size(); ++k)
                oscLevels[k] = settings.oscillators[k].level;
            layout     = {};
        }

        channel    = midiChannel;
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
        held        = false;
        sustained   = false;
        releaseFrom = envelopeLevel;
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
        envelopeLevel = 0.0f;
        note      = -1;
        held      = false;
        sustained = false;
    }

    bool isActive() const noexcept      { return note >= 0; }
    bool isHeld() const noexcept        { return held; }
    bool isSustained() const noexcept   { return sustained; }
    bool isReleasing() const noexcept   { return isActive() && ! held && ! sustained; }
    int  getNote() const noexcept       { return note; }
    int  getChannel() const noexcept    { return channel; }

    /** Whether this voice is sounding `midiNote` from MIDI `midiChannel`. */
    bool plays (int midiChannel, int midiNote) const noexcept
    {
        return isActive() && channel == midiChannel && note == midiNote;
    }
    std::uint64_t getAge() const noexcept { return age; }

    /** Where this note is in its envelope: rising, holding (key or pedal
        down) or releasing, and how loud the envelope is. */
    NoteActivity getActivity() const noexcept
    {
        NoteActivity activity;
        if (! isActive())
            return activity;

        activity.level = envelopeLevel;
        if (isReleasing())
        {
            activity.stage       = NoteActivity::Stage::release;
            activity.releaseFrom = juce::jmax (envelopeLevel, releaseFrom);
        }
        else
        {
            activity.stage = envelopeLevel < 0.999f ? NoteActivity::Stage::attack : NoteActivity::Stage::hold;
        }
        return activity;
    }

    /** Adds `numSamples` (at most maxChunk) of this note into left / right. */
    void render (float* left, float* right, int numSamples, const VoiceSettings& settings) noexcept
    {
        if (! isActive())
            return;

        const int count = Unison::clampVoices (settings.voices);
        const float ramp = 1.0f / (float) numSamples;

        // The oscillators' levels glide across the chunk. An oscillator runs
        // only while it is (or was, a moment ago) above silence.
        std::array<std::array<float, maxChunk>, numOscillators> levels;
        std::array<bool, numOscillators> running {};
        for (size_t k = 0; k < (size_t) numOscillators; ++k)
        {
            const float target = settings.oscillators[k].level;
            glide (levels[k], oscLevels[k], target, numSamples, ramp);
            running[k]   = target > 0.0f || oscLevels[k] > 0.0f;
            oscLevels[k] = target;
        }

        // The copies' detune and pans only need working out again when VOICES,
        // DETUNE or WIDTH has moved since the last chunk.
        const bool relayout = count != layout.count
                           || ! juce::exactlyEqual (settings.detuneCents, layout.detuneCents)
                           || ! juce::exactlyEqual (settings.width, layout.width);
        layout = { count, settings.detuneCents, settings.width };

        // First the sounding copies' oscillators, one oscillator at a time
        // across all the copies, each copy into its own row...
        std::array<size_t, Unison::maxVoices> playing {};
        const int numPlaying = tune (settings, count, relayout, running, playing);

        std::array<std::array<float, maxChunk>, Unison::maxVoices> rows;
        std::array<float*, Unison::maxVoices> outs {};
        for (size_t k = 0; k < (size_t) numPlaying; ++k)
            outs[k] = rows[k].data();

        bool written = false;
        for (size_t osc = 0; osc < (size_t) numOscillators; ++osc)
        {
            if (! running[osc])
                continue;

            std::array<BlepOscillator*, Unison::maxVoices> bank {};
            for (size_t k = 0; k < (size_t) numPlaying; ++k)
                bank[k] = &copies[playing[k]].oscillators[osc];
            BlepOscillator::renderAll (bank.data(), outs.data(), numPlaying, settings.oscillators[osc].wave,
                                       levels[osc].data(), numSamples, written);
            written = true;
        }
        if (! written)
            for (size_t k = 0; k < (size_t) numPlaying; ++k)
                std::fill_n (rows[k].begin(), numSamples, 0.0f);

        // ...then each copy into the stack, in order, faded and panned.
        std::array<float, maxChunk> stackL {};
        std::array<float, maxChunk> stackR {};
        for (size_t k = 0; k < (size_t) numPlaying; ++k)
            mix (playing[k], count, settings.width, relayout, rows[k].data(), stackL.data(), stackR.data(), numSamples, ramp);

        applyEnvelope (left, right, stackL.data(), stackR.data(), numSamples, ramp);
        if (! envelope.isActive())
            kill();
    }

private:
    // One unison copy: its two oscillators, how far it has faded in, and its
    // place in the detune spread and the stereo field.
    struct Copy
    {
        std::array<BlepOscillator, numOscillators> oscillators {};
        float            fade  = 0.0f;
        float            cents = 0.0f;
        double           ratio = 1.0;   // Unison::ratio (cents)
        Unison::PanGains pan;
    };

    // What the copies' detune and pans were last worked out for.
    struct Layout
    {
        int   count       = 0;   // none yet
        float detuneCents = 0.0f;
        float width       = 0.0f;
    };

    // `from` gliding to `to` across the chunk, ending on `to`.
    static void glide (std::array<float, maxChunk>& values, float from, float to, int numSamples, float ramp) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) * ramp;
            values[(size_t) n] = from + (to - from) * t;
        }
    }

    // Tunes the copies that are sounding (or fading out) for this chunk and
    // lists them in `playing`, returning how many there are.
    int tune (const VoiceSettings& settings, int count, bool relayout,
              const std::array<bool, numOscillators>& running,
              std::array<size_t, Unison::maxVoices>& playing) noexcept
    {
        int numPlaying = 0;
        for (size_t i = 0; i < copies.size(); ++i)
        {
            auto& copy = copies[i];
            const bool wanted = (int) i < count;
            if (copy.fade <= 0.0f && ! wanted)
                continue;

            if (wanted && relayout)
            {
                copy.cents = Unison::detuneCents ((int) i, count, settings.detuneCents);
                copy.ratio = Unison::ratio (copy.cents);
            }

            const double copyHz = noteHz * copy.ratio;
            for (size_t osc = 0; osc < (size_t) numOscillators; ++osc)
                if (running[osc])
                    copy.oscillators[osc].setIncrement (copyHz * settings.oscillators[osc].ratio / sampleRate);
            playing[(size_t) numPlaying++] = i;
        }
        return numPlaying;
    }

    // Adds copy `i`'s chunk (its oscillators, already levelled) into the
    // stack, faded and panned.
    void mix (size_t i, int count, float width, bool relayout, const float* samples,
              float* stackL, float* stackR, int numSamples, float ramp) noexcept
    {
        auto& copy = copies[i];
        const float target = (int) i < count ? 1.0f : 0.0f;

        // A copy that is leaving keeps its last place. The others move to
        // theirs across this chunk, so a new VOICES count re-pans them
        // without a jump.
        const auto from = copy.pan;
        if (target > 0.0f && relayout)
            copy.pan = Unison::panGains (Unison::pan ((int) i, count, width));

        float fade = copy.fade;
        const auto to = copy.pan;
        if (juce::exactlyEqual (fade, target) && juce::exactlyEqual (from.left, to.left) && juce::exactlyEqual (from.right, to.right))
        {
            // Settled: nothing to glide, so the copy goes straight in.
            for (int n = 0; n < numSamples; ++n)
            {
                const float value = samples[n] * fade;
                stackL[n] += value * to.left;
                stackR[n] += value * to.right;
            }
            return;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) * ramp;
            const float value = samples[n] * fade;
            stackL[n] += value * (from.left  + (to.left  - from.left)  * t);
            stackR[n] += value * (from.right + (to.right - from.right) * t);
            fade = target > fade ? juce::jmin (target, fade + fadeStep)
                                 : juce::jmax (target, fade - fadeStep);
        }
        copy.fade = fade;
    }

    // Puts the stack under the envelope and adds it into left / right. The
    // stack's level follows the copies actually sounding (fading ones count
    // in part), gliding across the chunk, so it never jumps.
    void applyEnvelope (float* left, float* right, const float* stackL, const float* stackR, int numSamples, float ramp) noexcept
    {
        float sounding = 0.0f;
        for (const auto& copy : copies)
            sounding += copy.fade;
        const float stackFrom = stackLevel;
        stackLevel = Unison::stackGain (sounding);

        for (int n = 0; n < numSamples; ++n)
        {
            const float stack = stackFrom + (stackLevel - stackFrom) * (float) (n + 1) * ramp;
            envelopeLevel = envelope.getNextSample();
            const float gain = envelopeLevel * level * stack;
            left[n]  += stackL[n] * gain;
            right[n] += stackR[n] * gain;
        }
    }

    std::array<Copy, Unison::maxVoices> copies {};
    juce::ADSR envelope;
    Layout     layout;

    double sampleRate = 44100.0;
    double noteHz     = 440.0;
    float  fadeStep   = 0.0f;
    float  level      = 0.0f;
    float  stackLevel = 1.0f;
    std::array<float, numOscillators> oscLevels {};   // the levels the last chunk ended on
    float  envelopeLevel = 0.0f;
    float  releaseFrom   = 1.0f;
    int    note       = -1;
    int    channel    = 1;
    bool   held       = false;
    bool   sustained  = false;
    std::uint64_t age = 0;
};

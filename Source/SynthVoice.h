#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
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
    Waveform wave        = Waveform::saw;   // OSC 1's shape
    int      voices      = 1;      // unison copies per note, 1..Unison::maxVoices
    float    detuneCents = 0.0f;   // the outer copies' offset, 0..Unison::maxDetuneCents
    float    width       = 0.0f;   // stereo spread, 0..1

    // Each oscillator's pitch against the note (from its PITCH and FINE) and
    // its level. OSC 2 is silent at level 0, and isn't run at all once it has
    // faded there.
    float    ratio1      = 1.0f;
    float    level1      = 1.0f;
    Waveform wave2       = Waveform::saw;
    float    ratio2      = 1.0f;
    float    level2      = 0.0f;
};

/**
    SynthVoice

    One note: a stack of up to 16 unison copies (see Unison.h) under an
    attack / release envelope. Each copy plays OSC 1 and, when it is on, OSC 2:
    two oscillators at their own pitch and level, sharing the copy's detune
    and pan.

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
            for (size_t i = 0; i < oscillators.size(); ++i)
            {
                const bool lone = i == 0 && count == 1;
                oscillators[i] .reset (lone ? 0.0 : random.nextDouble());
                oscillators2[i].reset (lone ? 0.0 : random.nextDouble());
                fades[i] = (int) i < count ? 1.0f : 0.0f;
                pans[i]  = Unison::panGains (Unison::pan ((int) i, count, settings.width));
            }
            stackLevel = Unison::stackGain ((float) count);
            oscLevel1  = settings.level1;
            oscLevel2  = settings.level2;
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
        std::array<float, maxChunk> stackL {};
        std::array<float, maxChunk> stackR {};
        const float ramp = 1.0f / (float) numSamples;

        // The oscillators' levels glide across the chunk. OSC 2 runs only
        // while it is (or was, a moment ago) above silence.
        std::array<float, maxChunk> levels1 {};
        std::array<float, maxChunk> levels2 {};
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) * ramp;
            levels1[(size_t) n] = oscLevel1 + (settings.level1 - oscLevel1) * t;
            levels2[(size_t) n] = oscLevel2 + (settings.level2 - oscLevel2) * t;
        }
        const bool second = settings.level2 > 0.0f || oscLevel2 > 0.0f;
        oscLevel1 = settings.level1;
        oscLevel2 = settings.level2;

        // The copies' detune and pans only need working out again when VOICES,
        // DETUNE or WIDTH has moved since the last chunk.
        const bool relayout = count != layout.count
                           || ! juce::exactlyEqual (settings.detuneCents, layout.detuneCents)
                           || ! juce::exactlyEqual (settings.width, layout.width);
        layout = { count, settings.detuneCents, settings.width };

        // First the copies' oscillators, all of them together, each copy into
        // its own row...
        std::array<std::array<float, maxChunk>, Unison::maxVoices> copies;
        std::array<size_t, Unison::maxVoices>          playing {};
        std::array<BlepOscillator*, Unison::maxVoices> playing1 {};
        std::array<BlepOscillator*, Unison::maxVoices> playing2 {};
        std::array<float*, Unison::maxVoices>          rows {};
        int numPlaying = 0;

        for (size_t i = 0; i < oscillators.size(); ++i)
        {
            const float target = (int) i < count ? 1.0f : 0.0f;
            if (fades[i] <= 0.0f && target <= 0.0f)
                continue;

            if (target > 0.0f && relayout)
            {
                cents[i]  = Unison::detuneCents ((int) i, count, settings.detuneCents);
                ratios[i] = Unison::ratio (cents[i]);
            }

            const double copyHz = noteHz * ratios[i];
            oscillators[i].setIncrement (copyHz * settings.ratio1 / sampleRate);
            if (second)
                oscillators2[i].setIncrement (copyHz * settings.ratio2 / sampleRate);

            const auto k = (size_t) numPlaying++;
            playing[k]  = i;
            playing1[k] = &oscillators[i];
            playing2[k] = &oscillators2[i];
            rows[k]     = copies[i].data();
        }

        BlepOscillator::renderAll (playing1.data(), rows.data(), numPlaying, settings.wave, levels1.data(), numSamples, false);
        if (second)
            BlepOscillator::renderAll (playing2.data(), rows.data(), numPlaying, settings.wave2, levels2.data(), numSamples, true);

        // ...then each copy into the stack, in order, faded and panned.
        for (size_t k = 0; k < (size_t) numPlaying; ++k)
        {
            const size_t i = playing[k];
            const float target = (int) i < count ? 1.0f : 0.0f;
            const auto& copy = copies[i];

            // A copy that is leaving keeps its last place. The others move to
            // theirs across this chunk, so a new VOICES count re-pans them
            // without a jump.
            const auto from = pans[i];
            if (target > 0.0f && relayout)
                pans[i] = Unison::panGains (Unison::pan ((int) i, count, settings.width));

            float fade = fades[i];
            const auto to = pans[i];
            if (juce::exactlyEqual (fade, target) && juce::exactlyEqual (from.left, to.left) && juce::exactlyEqual (from.right, to.right))
            {
                // Settled: nothing to glide, so the copy goes straight in.
                for (int n = 0; n < numSamples; ++n)
                {
                    const float value = copy[(size_t) n] * fade;
                    stackL[(size_t) n] += value * to.left;
                    stackR[(size_t) n] += value * to.right;
                }
                continue;
            }

            for (int n = 0; n < numSamples; ++n)
            {
                const float t = (float) (n + 1) * ramp;
                const float value = copy[(size_t) n] * fade;
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
            envelopeLevel = envelope.getNextSample();
            const float gain = envelopeLevel * level * stack;
            left[n]  += stackL[(size_t) n] * gain;
            right[n] += stackR[(size_t) n] * gain;
        }

        if (! envelope.isActive())
            kill();
    }

private:
    std::array<BlepOscillator, Unison::maxVoices>    oscillators {};    // OSC 1, one per copy
    std::array<BlepOscillator, Unison::maxVoices>    oscillators2 {};   // OSC 2
    std::array<float, Unison::maxVoices>             fades {};
    std::array<float, Unison::maxVoices>             cents {};
    std::array<double, Unison::maxVoices>            ratios = filled (1.0);   // Unison::ratio (cents)
    std::array<Unison::PanGains, Unison::maxVoices>  pans {};
    juce::ADSR envelope;

    // What the copies' detune and pans were last worked out for.
    struct Layout
    {
        int   count       = 0;   // none yet
        float detuneCents = 0.0f;
        float width       = 0.0f;
    };
    Layout layout;

    static std::array<double, Unison::maxVoices> filled (double value) noexcept
    {
        std::array<double, Unison::maxVoices> values {};
        values.fill (value);
        return values;
    }

    double sampleRate = 44100.0;
    double noteHz     = 440.0;
    float  fadeStep   = 0.0f;
    float  level      = 0.0f;
    float  stackLevel = 1.0f;
    float  oscLevel1  = 1.0f;   // the levels the last chunk ended on
    float  oscLevel2  = 0.0f;
    float  envelopeLevel = 0.0f;
    float  releaseFrom   = 1.0f;
    int    note       = -1;
    int    channel    = 1;
    bool   held       = false;
    bool   sustained  = false;
    std::uint64_t age = 0;
};

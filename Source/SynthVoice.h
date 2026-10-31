#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cstdint>

#include "Envelope.h"
#include "Filter.h"
#include "Oscillator.h"
#include "Unison.h"

/** Where a note is in its envelope, for the editor's AMP display. */
struct NoteActivity
{
    using Stage = Envelope::Stage;

    Stage stage    = Stage::idle;
    float level    = 0.0f;   // the envelope, 0..1
    float progress = 0.0f;   // how far through its stage, 0..1
};

/** How every note sounds right now: the engine hands these to each voice once
    per sub-block, already smoothed. */
struct VoiceSettings
{
    static constexpr int numOscillators = 4;

    // Each oscillator's shape, pitch against the note (from its PITCH and
    // FINE), level and unison stack. An oscillator is silent at level 0, and
    // isn't run at all once it has faded there.
    struct Oscillator
    {
        Waveform wave        = Waveform::saw;
        float    ratio       = 1.0f;
        float    level       = 0.0f;
        int      voices      = 1;      // unison copies, 1..Unison::maxVoices
        float    detuneCents = 0.0f;   // the outer copies' offset, 0..Unison::maxDetuneCents
        float    width       = 0.0f;   // stereo spread, 0..1

        // Its filter, if one is on it: the type, the cutoff before the
        // sweep, RESONANCE, and how far the sweep moves the cutoff (in
        // semitones, at the top of the sweep envelope).
        bool         filtered  = false;
        Filter::Type filter    = Filter::Type::low24;
        float        cutoffHz  = 1000.0f;
        float        resonance = 0.0f;
        float        envAmount = 0.0f;
    };
    std::array<Oscillator, numOscillators> oscillators {};
};

/**
    SynthVoice

    One note, played by up to four oscillators. Each oscillator is a part of
    its own: a stack of up to 16 unison copies (see Unison.h) with its own
    detune and spread, under its own envelope (see Envelope.h), so each
    oscillator can be shaped differently, and, if a filter is on it, through
    its own filter (see Filter.h), whose cutoff its own sweep envelope moves.
    A filter coming or going fades in or out over 5 ms, so it never clicks.

    Changing VOICES while a note sounds fades copies in and out over 5 ms
    rather than switching them, and the stack's level and the copies' pans
    glide to their new values, so turning the knob mid-chord doesn't click.
    A copy that is leaving keeps its last detune and pan until it has gone.

    Every part's envelope follows the key, sounding or not, so an oscillator
    that comes in mid-note joins in step. Once the key is up, the note ends
    when every oscillator playing it has released.
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
        for (auto& part : parts)
        {
            part.envelope.setSampleRate (newSampleRate);
            part.sweep.setSampleRate (newSampleRate);
            part.filter.setSampleRate (newSampleRate);
        }
        fadeStep = (float) (1.0 / (fadeSeconds * newSampleRate));
        kill();
    }

    /** Sets oscillator `osc`'s envelope. */
    void setEnvelope (int osc, const Envelope::Parameters& parameters) { parts[(size_t) osc].envelope.setParameters (parameters); }

    /** Sets the envelope that sweeps oscillator `osc`'s filter. */
    void setSweep (int osc, const Envelope::Parameters& parameters) { parts[(size_t) osc].sweep.setParameters (parameters); }

    /** Plays `midiNote` on MIDI `midiChannel` (1..16). A voice taken over while
        still sounding glides its envelopes on from where they were instead of
        jumping back to silence. */
    void start (int midiChannel, int midiNote, float velocity, std::uint64_t order, const VoiceSettings& settings, juce::Random& random)
    {
        if (! isActive())
        {
            // Unison copies start at random points in their cycles, so they
            // don't all line up and thump on the attack. A lone voice starts on
            // the zero crossing at the top of the cycle.
            for (size_t k = 0; k < parts.size(); ++k)
            {
                auto& part = parts[k];
                const auto& osc = settings.oscillators[k];
                const int count = Unison::clampVoices (osc.voices);
                for (size_t i = 0; i < part.copies.size(); ++i)
                {
                    auto& copy = part.copies[i];
                    const bool lone = i == 0 && count == 1;
                    copy.oscillator.reset (lone ? 0.0 : random.nextDouble());
                    copy.fade = (int) i < count ? 1.0f : 0.0f;
                    copy.pan  = Unison::panGains (Unison::pan ((int) i, count, osc.width));
                }
                part.stackLevel = Unison::stackGain ((float) count);
                part.level      = osc.level;
                part.layout     = {};
                part.filter.reset();
                part.filterMix  = osc.filtered ? 1.0f : 0.0f;
                part.lastFilter = osc;
            }
        }

        channel    = midiChannel;
        note       = midiNote;
        level      = noteLevel * juce::jlimit (0.0f, 1.0f, velocity);
        age        = order;
        held       = true;
        sustained  = false;
        noteHz     = 440.0 * std::exp2 ((midiNote - 69) / 12.0);
        for (auto& part : parts)
        {
            part.envelope.noteOn();
            part.sweep.noteOn();
        }
    }

    /** The key went up (and the sustain pedal, if any, is up too). */
    void release()
    {
        held      = false;
        sustained = false;
        for (auto& part : parts)
        {
            part.envelope.noteOff();
            part.sweep.noteOff();
        }
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
        for (auto& part : parts)
        {
            part.envelope.reset();
            part.sweep.reset();
            part.envelopeLevel = 0.0f;
        }
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

    /** Where this note is in oscillator `osc`'s envelope: which stage, how far
        through it, and how loud the envelope is. */
    NoteActivity getActivity (int osc) const noexcept
    {
        NoteActivity activity;
        if (! isActive())
            return activity;

        return activityOf (parts[(size_t) osc].envelope);
    }

    /** Where this note is in the envelope sweeping oscillator `osc`'s filter. */
    NoteActivity getSweepActivity (int osc) const noexcept
    {
        if (! isActive())
            return {};
        return activityOf (parts[(size_t) osc].sweep);
    }

    /** Adds `numSamples` (at most maxChunk) of this note into left / right. */
    void render (float* left, float* right, int numSamples, const VoiceSettings& settings) noexcept
    {
        if (! isActive())
            return;

        const float ramp = 1.0f / (float) numSamples;
        bool sounding = false;
        for (size_t k = 0; k < parts.size(); ++k)
        {
            auto& part = parts[k];
            const auto& osc = settings.oscillators[k];

            // The level glides across the chunk. An oscillator runs only while
            // it is (or was, a moment ago) above silence; one that isn't still
            // keeps its envelope moving.
            const bool running = osc.level > 0.0f || part.level > 0.0f;
            if (running)
                renderPart (part, osc, left, right, numSamples, ramp);
            else
            {
                part.envelope.advance (numSamples);
                part.sweep.advance (numSamples);
                part.envelopeLevel = part.envelope.getLevel();
            }
            part.level = osc.level;
            sounding = sounding || (running && part.envelope.isActive());
        }

        // Once the key is up, the note lasts as long as the envelopes of the
        // oscillators actually playing it.
        if (! sounding && ! held && ! sustained)
            kill();
    }

private:
    // One unison copy of an oscillator: its oscillator, how far it has faded
    // in, and its place in the detune spread and the stereo field.
    struct Copy
    {
        BlepOscillator   oscillator;
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

    // One oscillator's share of the note: its unison stack, its envelope,
    // and its filter with the envelope that sweeps it.
    struct Part
    {
        std::array<Copy, Unison::maxVoices> copies {};
        Envelope   envelope;
        Envelope   sweep;
        Filter     filter;
        Layout     layout;
        float      stackLevel    = 1.0f;
        float      level         = 0.0f;   // the level the last chunk ended on
        float      envelopeLevel = 0.0f;
        float      filterMix     = 0.0f;   // how far the filter is in, 0..1
        VoiceSettings::Oscillator lastFilter;   // the filter's settings, kept while it fades out
    };

    static NoteActivity activityOf (const Envelope& envelope) noexcept
    {
        NoteActivity activity;
        activity.stage    = envelope.getStage();
        activity.level    = envelope.getLevel();
        activity.progress = envelope.getProgress();
        return activity;
    }

    // Adds one oscillator's chunk into left / right: its sounding copies, one
    // row each, already under the part's envelope, then each faded and panned.
    void renderPart (Part& part, const VoiceSettings::Oscillator& osc, float* left, float* right, int numSamples, float ramp) const noexcept
    {
        const int count = Unison::clampVoices (osc.voices);

        // The copies' detune and pans only need working out again when VOICES,
        // DETUNE or WIDTH has moved since the last chunk.
        const bool relayout = count != part.layout.count
                           || ! juce::exactlyEqual (osc.detuneCents, part.layout.detuneCents)
                           || ! juce::exactlyEqual (osc.width, part.layout.width);
        part.layout = { count, osc.detuneCents, osc.width };

        std::array<size_t, Unison::maxVoices> playing {};
        const int numPlaying = tune (part, osc, count, relayout, playing);

        std::array<float, maxChunk> levels;
        shape (part, osc.level, count, levels, numSamples, ramp);

        std::array<std::array<float, maxChunk>, Unison::maxVoices> rows;
        std::array<float*, Unison::maxVoices> outs {};
        std::array<BlepOscillator*, Unison::maxVoices> bank {};
        for (size_t k = 0; k < (size_t) numPlaying; ++k)
        {
            outs[k] = rows[k].data();
            bank[k] = &part.copies[playing[k]].oscillator;
        }
        BlepOscillator::renderAll (bank.data(), outs.data(), numPlaying, osc.wave, levels.data(), numSamples, false);

        // With no filter on it (or fading), the copies go straight into the
        // note; otherwise into a chunk of their own, to be filtered first.
        if (! osc.filtered && juce::exactlyEqual (part.filterMix, 0.0f))
        {
            part.sweep.advance (numSamples);
            for (size_t k = 0; k < (size_t) numPlaying; ++k)
                mix (part.copies[playing[k]], (int) playing[k] < count, count, (int) playing[k], osc.width, relayout,
                     rows[k].data(), left, right, numSamples, ramp);
            return;
        }

        std::array<float, maxChunk> dryL {};
        std::array<float, maxChunk> dryR {};
        for (size_t k = 0; k < (size_t) numPlaying; ++k)
            mix (part.copies[playing[k]], (int) playing[k] < count, count, (int) playing[k], osc.width, relayout,
                 rows[k].data(), dryL.data(), dryR.data(), numSamples, ramp);
        filterInto (part, osc, dryL, dryR, left, right, numSamples);
    }

    // Filters a part's chunk and adds it into left / right. The cutoff
    // follows the sweep envelope (in semitones, ENV AMOUNT at its top) to
    // where it is at the chunk's end, and the filter fades in or out as it
    // comes or goes, the unfiltered sound filling in for it.
    void filterInto (Part& part, const VoiceSettings::Oscillator& osc, const std::array<float, maxChunk>& dryL,
                     const std::array<float, maxChunk>& dryR, float* left, float* right, int numSamples) const noexcept
    {
        if (osc.filtered)
            part.lastFilter = osc;
        const auto& shape = part.lastFilter;
        part.sweep.advance (numSamples);
        const float sweepLevel = part.sweep.getLevel();
        const float cutoff     = shape.cutoffHz * std::exp2 (shape.envAmount * sweepLevel / 12.0f);

        std::array<float, maxChunk> wetL = dryL;
        std::array<float, maxChunk> wetR = dryR;
        part.filter.process (wetL.data(), wetR.data(), numSamples, shape.filter, cutoff, shape.resonance);

        const float target = osc.filtered ? 1.0f : 0.0f;
        float mixAmount = part.filterMix;
        for (int n = 0; n < numSamples; ++n)
        {
            const auto i = (size_t) n;
            left[n]  += dryL[i] + (wetL[i] - dryL[i]) * mixAmount;
            right[n] += dryR[i] + (wetR[i] - dryR[i]) * mixAmount;
            mixAmount = target > mixAmount ? juce::jmin (target, mixAmount + fadeStep) : juce::jmax (target, mixAmount - fadeStep);
        }
        part.filterMix = mixAmount;
        if (juce::exactlyEqual (mixAmount, 0.0f))
            part.filter.reset();
    }

    // Each sample's level for a part's copies: the oscillator's LEVEL (gliding
    // from where the last chunk ended), under the part's envelope, the note's
    // velocity and the stack's level. The stack's level follows the copies
    // that will be sounding by the end of the chunk (fading ones count in
    // part), gliding there, so it never jumps.
    void shape (Part& part, float oscLevel, int count, std::array<float, maxChunk>& levels, int numSamples, float ramp) const noexcept
    {
        const float travel = fadeStep * (float) numSamples;
        float sounding = 0.0f;
        for (size_t i = 0; i < part.copies.size(); ++i)
        {
            const float fade = part.copies[i].fade;
            sounding += (int) i < count ? juce::jmin (1.0f, fade + travel) : juce::jmax (0.0f, fade - travel);
        }
        const float stackFrom = part.stackLevel;
        part.stackLevel = Unison::stackGain (sounding);

        // A held note resting on its SUSTAIN level needs no stepping.
        const bool settled = part.envelope.isSettled();
        if (settled)
            part.envelopeLevel = part.envelope.getLevel();
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) * ramp;
            if (! settled)
                part.envelopeLevel = part.envelope.getNextSample();
            const float stack  = stackFrom + (part.stackLevel - stackFrom) * t;
            const float glided = part.level + (oscLevel - part.level) * t;
            levels[(size_t) n] = glided * part.envelopeLevel * level * stack;
        }
    }

    // Tunes the part's copies that are sounding (or fading out) for this chunk
    // and lists them in `playing`, returning how many there are.
    int tune (Part& part, const VoiceSettings::Oscillator& osc, int count, bool relayout,
              std::array<size_t, Unison::maxVoices>& playing) const noexcept
    {
        int numPlaying = 0;
        for (size_t i = 0; i < part.copies.size(); ++i)
        {
            auto& copy = part.copies[i];
            const bool wanted = (int) i < count;
            if (copy.fade <= 0.0f && ! wanted)
                continue;

            if (wanted && relayout)
            {
                copy.cents = Unison::detuneCents ((int) i, count, osc.detuneCents);
                copy.ratio = Unison::ratio (copy.cents);
            }

            copy.oscillator.setIncrement (noteHz * copy.ratio * osc.ratio / sampleRate);
            playing[(size_t) numPlaying++] = i;
        }
        return numPlaying;
    }

    // Adds copy `index`'s chunk (already levelled) into left / right, faded and panned.
    // (Its arguments stay apart: bundling the output into a struct measured
    // about 1% slower, and this runs for every copy of every note.)
    void mix (Copy& copy, bool wanted, int count, int index, float width, bool relayout, const float* samples,   // NOSONAR(cpp:S107): hot path, see above
              float* left, float* right, int numSamples, float ramp) const noexcept
    {
        const float target = wanted ? 1.0f : 0.0f;

        // A copy that is leaving keeps its last place. The others move to
        // theirs across this chunk, so a new VOICES count re-pans them
        // without a jump.
        const auto from = copy.pan;
        if (wanted && relayout)
            copy.pan = Unison::panGains (Unison::pan (index, count, width));

        float fade = copy.fade;
        const auto to = copy.pan;
        if (juce::exactlyEqual (fade, target) && juce::exactlyEqual (from.left, to.left) && juce::exactlyEqual (from.right, to.right))
        {
            // Settled: nothing to glide, so the copy goes straight in.
            for (int n = 0; n < numSamples; ++n)
            {
                const float value = samples[n] * fade;
                left[n]  += value * to.left;
                right[n] += value * to.right;
            }
            return;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) * ramp;
            const float value = samples[n] * fade;
            left[n]  += value * (from.left  + (to.left  - from.left)  * t);
            right[n] += value * (from.right + (to.right - from.right) * t);
            fade = target > fade ? juce::jmin (target, fade + fadeStep)
                                 : juce::jmax (target, fade - fadeStep);
        }
        copy.fade = fade;
    }

    std::array<Part, numOscillators> parts {};

    double sampleRate = 44100.0;
    double noteHz     = 440.0;
    float  fadeStep   = 0.0f;
    float  level      = 0.0f;
    int    note       = -1;
    int    channel    = 1;
    bool   held       = false;
    bool   sustained  = false;
    std::uint64_t age = 0;
};

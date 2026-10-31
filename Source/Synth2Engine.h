#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Effects.h"
#include "Oscillator.h"
#include "RtSafety.h"
#include "SynthVoice.h"
#include "Unison.h"

/**
    Synth2Engine

    The DSP heart of the synth, deliberately kept free of any host / plugin
    plumbing so it can be unit-tested or reused on its own.

    Sixteen notes of polyphony, each played by the oscillators that are
    switched on (up to four). Each oscillator plays one of the four basic
    shapes at its own PITCH (semitones), FINE tune (cents) and LEVEL, as a
    stack of VOICES unison copies, DETUNEd and spread across the stereo field
    by WIDTH (see Unison.h), under the ATTACK / RELEASE envelope that shapes
    it (one of four):

        note  ->  OSC n: VOICES x shape  ->  detune + pan  ->  ENV  ->  +  ->  EFFECTS  ->  OUTPUT

    An oscillator with no envelope simply opens and closes over a few
    milliseconds, so it never clicks. One that is on but not routed to the
    output (see OscillatorSettings::routed), or switched off with its power
    switch (OscillatorSettings::active), is silent, as if it were off.

    Every note goes through the effects together, the drive, the delay and
    the reverb in whichever order the settings say (see Effects.h), before
    OUTPUT.

    MIDI is read sample-accurately: the block is rendered up to each event,
    then the event applied. A seventeenth note takes over the voice that has
    been releasing longest (or, failing that, the oldest held one). Each MIDI
    channel keeps its own notes: the same key on two channels plays two
    voices, and each channel has its own sustain pedal (CC 64), All Sound Off
    (CC 120, silences at once) and All Notes Off (CC 123, releases).

    OUTPUT and each oscillator's DETUNE, WIDTH, FINE and LEVEL glide to new
    values over 20 ms, so turning them while a chord plays doesn't zipper;
    switching an oscillator on or off, or routing it to the output or away,
    fades it in or out the same way.
*/
class Synth2Engine
{
public:
    static constexpr int   maxNotes     = 16;
    static constexpr int   maxSemitones = 24;      // each oscillator's PITCH, either way
    static constexpr float maxFineCents = 50.0f;   // and its FINE tune
    static constexpr int   numOscillators = SynthVoice::numOscillators;
    static constexpr int   numEnvelopes   = 4;

    // An oscillator no envelope shapes still opens and closes this fast, so
    // it never clicks.
    static constexpr float gateAttackSeconds  = 0.005f;
    static constexpr float gateReleaseSeconds = 0.03f;

    /** One oscillator: whether it is on the bench, and routed to the output,
        its shape, its tuning against the note, its level, its unison stack,
        which envelope shapes it (-1 for none) and whether its power switch
        is on. */
    struct OscillatorSettings
    {
        bool     on          = false;
        bool     routed      = true;
        Waveform wave        = Waveform::saw;
        int      semitones   = 0;
        float    fineCents   = 0.0f;
        float    level       = 1.0f;
        int      voices      = 1;
        float    detuneCents = 15.0f;
        float    width       = 0.5f;
        int      envelope    = -1;
        bool     active      = true;
    };

    /** One envelope (see Envelope.h): how long a note waits, takes to fade
        in, stays at the top and takes to fall to its sustain level, that
        level, how long it rings on after its key goes up, and how its
        attack, decay and release ramps bend (-1..1, 0 straight). With the
        defaults it fades in and stays up while the key is held. */
    struct EnvelopeSettings
    {
        float delaySeconds   = 0.0f;
        float attackSeconds  = 0.005f;
        float holdSeconds    = 0.0f;
        float decaySeconds   = 0.3f;
        float sustainLevel   = 1.0f;
        float releaseSeconds = 0.3f;
        float attackCurve    = 0.0f;
        float decayCurve     = 0.0f;
        float releaseCurve   = 0.0f;
    };

    struct Settings
    {
        // OSC 1 plays from the start, under ENV 1; the others each have their
        // own sound ready for when they are switched on, under their own
        // envelope.
        std::array<OscillatorSettings, numOscillators> oscillators {
            OscillatorSettings { true,  true, Waveform::saw,      0,  0.0f, 1.0f, 1, 15.0f, 0.5f, 0 },
            OscillatorSettings { false, true, Waveform::square, -12,  0.0f, 0.7f, 1, 15.0f, 0.5f, 1 },
            OscillatorSettings { false, true, Waveform::saw,      7,  0.0f, 0.5f, 1, 15.0f, 0.5f, 2 },
            OscillatorSettings { false, true, Waveform::triangle, 12, 0.0f, 0.4f, 1, 15.0f, 0.5f, 3 } };

        std::array<EnvelopeSettings, numEnvelopes> envelopes {};
        Effects::Settings effects {};
        float outputDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        for (auto& voice : voices)
            voice.prepare (sampleRate);

        constexpr double glide = 0.02;
        smoothed.gain.reset (sampleRate, glide);
        smoothed.gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.outputDb));
        for (size_t k = 0; k < (size_t) numOscillators; ++k)
        {
            const auto& oscillator = settings.oscillators[k];
            for (auto* value : { &smoothed.fine[k], &smoothed.level[k], &smoothed.detune[k], &smoothed.width[k] })
                value->reset (sampleRate, glide);
            smoothed.fine[k]  .setCurrentAndTargetValue (oscillator.fineCents);
            smoothed.level[k] .setCurrentAndTargetValue (levelTarget (oscillator));
            smoothed.detune[k].setCurrentAndTargetValue (oscillator.detuneCents);
            smoothed.width[k] .setCurrentAndTargetValue (oscillator.width);
            appliedEnvelopes[k].attack = -1.0f;   // hand every voice the envelope afresh
        }
        applyEnvelopes();
        effects.set (settings.effects);
        effects.prepare (sampleRate);
        sustainDown = {};
    }

    void setSettings (const Settings& newSettings) noexcept
    {
        settings = newSettings;
        for (size_t k = 0; k < (size_t) numOscillators; ++k)
        {
            auto& oscillator = settings.oscillators[k];
            oscillator.semitones   = juce::jlimit (-maxSemitones, maxSemitones, oscillator.semitones);
            oscillator.fineCents   = juce::jlimit (-maxFineCents, maxFineCents, oscillator.fineCents);
            oscillator.level       = juce::jlimit (0.0f, 1.0f, oscillator.level);
            oscillator.voices      = Unison::clampVoices (oscillator.voices);
            oscillator.detuneCents = juce::jlimit (0.0f, Unison::maxDetuneCents, oscillator.detuneCents);
            oscillator.width       = juce::jlimit (0.0f, 1.0f, oscillator.width);
            oscillator.envelope    = juce::jlimit (-1, numEnvelopes - 1, oscillator.envelope);
            smoothed.fine[k]  .setTargetValue (oscillator.fineCents);
            smoothed.level[k] .setTargetValue (levelTarget (oscillator));
            smoothed.detune[k].setTargetValue (oscillator.detuneCents);
            smoothed.width[k] .setTargetValue (oscillator.width);
        }
        smoothed.gain.setTargetValue (juce::Decibels::decibelsToGain (settings.outputDb, -100.0f));
        applyEnvelopes();
        effects.set (settings.effects);
    }

    const Settings& getSettings() const noexcept { return settings; }

    /** Renders the block, playing the MIDI in it. The buffer's contents are
        replaced; a mono buffer gets both sides of the stereo spread. */
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept SYNTH2_RT_NONBLOCKING
    {
        buffer.clear();
        const int numSamples = buffer.getNumSamples();
        if (buffer.getNumChannels() == 0 || numSamples == 0)
            return;

        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        int done = 0;
        for (const auto event : midi)
        {
            const int when = juce::jlimit (done, numSamples, event.samplePosition);
            render (left, right, done, when);
            handleMidi (event.data, event.numBytes);
            done = when;
        }
        render (left, right, done, numSamples);
    }

    // ---- Notes (also driven by process()'s MIDI) --------------------------------

    void noteOn (int midiChannel, int midiNote, float velocity) noexcept
    {
        if (velocity <= 0.0f)
        {
            noteOff (midiChannel, midiNote);
            return;
        }

        SynthVoice* chosen = nullptr;

        // The same key again on the same channel plays on the voice it already had.
        for (auto& voice : voices)
            if (voice.plays (midiChannel, midiNote))
                chosen = &voice;

        if (chosen == nullptr)
            for (auto& voice : voices)
                if (! voice.isActive())
                {
                    chosen = &voice;
                    break;
                }

        if (chosen == nullptr)
            chosen = &voiceToSteal();

        chosen->start (midiChannel, midiNote, velocity, ++noteCounter, currentVoiceSettings(), random);
    }

    void noteOff (int midiChannel, int midiNote) noexcept
    {
        for (auto& voice : voices)
        {
            if (! voice.plays (midiChannel, midiNote) || ! voice.isHeld())
                continue;

            if (isSustainPedalDown (midiChannel))
                voice.sustain();
            else
                voice.release();
        }
    }

    void setSustainPedal (int midiChannel, bool isDown) noexcept
    {
        sustainDown[channelIndex (midiChannel)] = isDown;
        if (isDown)
            return;

        for (auto& voice : voices)
            if (voice.isActive() && voice.getChannel() == midiChannel && voice.isSustained())
                voice.release();
    }

    /** Releases every note on `midiChannel`, as All Notes Off does. */
    void allNotesOff (int midiChannel) noexcept
    {
        sustainDown[channelIndex (midiChannel)] = false;
        for (auto& voice : voices)
            if (voice.isActive() && voice.getChannel() == midiChannel && ! voice.isReleasing())
                voice.release();
    }

    /** Silences every note on `midiChannel` at once, as All Sound Off does,
        and the delay's repeats and the reverb's ring with them. */
    void allSoundOff (int midiChannel) noexcept
    {
        sustainDown[channelIndex (midiChannel)] = false;
        for (auto& voice : voices)
            if (voice.getChannel() == midiChannel)
                voice.kill();
        effects.clear();
    }

    /** How many notes are sounding (held, sustained or releasing). */
    int getNumActiveNotes() const noexcept
    {
        int count = 0;
        for (const auto& voice : voices)
            count += voice.isActive() ? 1 : 0;
        return count;
    }

    /** Whether `midiNote` is sounding on any channel (held, sustained or releasing). */
    bool isNoteSounding (int midiNote) const noexcept
    {
        return std::any_of (voices.begin(), voices.end(),
                            [midiNote] (const SynthVoice& voice) { return voice.isActive() && voice.getNote() == midiNote; });
    }

    /** Where every voice is in oscillator `osc`'s envelope (idle ones included). */
    std::array<NoteActivity, maxNotes> getActivity (int osc) const noexcept
    {
        std::array<NoteActivity, maxNotes> activity {};
        for (size_t i = 0; i < voices.size(); ++i)
            activity[i] = voices[i].getActivity (osc);
        return activity;
    }

    /** The keys held down right now, one bit per MIDI note (notes 0..63 in the
        first word, 64..127 in the second). */
    std::array<std::uint64_t, 2> getHeldNotes() const noexcept
    {
        std::array<std::uint64_t, 2> held {};
        for (const auto& voice : voices)
            if (voice.isActive() && voice.isHeld())
            {
                const int note = voice.getNote();
                held[(size_t) note / 64] |= std::uint64_t { 1 } << (note % 64);
            }
        return held;
    }

    /** Whether `midiNote` is sounding from MIDI `midiChannel`. */
    bool isNoteSounding (int midiChannel, int midiNote) const noexcept
    {
        return std::any_of (voices.begin(), voices.end(),
                            [midiChannel, midiNote] (const SynthVoice& voice) { return voice.plays (midiChannel, midiNote); });
    }

    bool isSustainPedalDown (int midiChannel) const noexcept { return sustainDown[channelIndex (midiChannel)]; }

private:
    void handleMidi (const juce::uint8* data, int numBytes) noexcept
    {
        if (numBytes < 1)
            return;

        // Byte `index` of the message under `mask`, or 0 past its end.
        const auto bits = [data, numBytes] (int index, std::byte mask)
        {
            return index < numBytes ? std::to_integer<int> (std::byte { data[index] } & mask) : 0;
        };

        const int status  = bits (0, std::byte { 0xf0 });
        const int channel = bits (0, std::byte { 0x0f }) + 1;
        const int first   = bits (1, std::byte { 0x7f });
        const int second  = bits (2, std::byte { 0x7f });

        switch (status)
        {
            case 0x90: noteOn (channel, first, (float) second / 127.0f); break;
            case 0x80: noteOff (channel, first); break;
            case 0xb0:
                if (first == 64)        setSustainPedal (channel, second >= 64);
                else if (first == 120)  allSoundOff (channel);
                else if (first == 123)  allNotesOff (channel);
                break;
            default: break;
        }
    }

    void render (float* left, float* right, int start, int end) noexcept
    {
        while (start < end)
        {
            const int length = juce::jmin (SynthVoice::maxChunk, end - start);

            // The oscillators' detune, width, tuning and levels move once per
            // chunk (at most 32 samples; the voices glide the levels across
            // it), which is fine-grained enough for pitch and pan; the output
            // gain moves every sample.
            for (size_t k = 0; k < (size_t) numOscillators; ++k)
                for (auto* value : { &smoothed.fine[k], &smoothed.level[k], &smoothed.detune[k], &smoothed.width[k] })
                    value->skip (length);
            const auto chunkSettings = currentVoiceSettings();

            std::array<float, SynthVoice::maxChunk> mixL {};
            std::array<float, SynthVoice::maxChunk> mixR {};
            for (auto& voice : voices)
                voice.render (mixL.data(), mixR.data(), length, chunkSettings);
            effects.process (mixL.data(), mixR.data(), length);

            for (int n = 0; n < length; ++n)
            {
                const float gain = smoothed.gain.getNextValue();
                const auto  i    = (size_t) n;
                if (right != nullptr)
                {
                    left [start + n] = mixL[i] * gain;
                    right[start + n] = mixR[i] * gain;
                }
                else
                {
                    left[start + n] = 0.5f * (mixL[i] + mixR[i]) * gain;
                }
            }

            start += length;
        }
    }

    static size_t channelIndex (int midiChannel) noexcept { return (size_t) juce::jlimit (1, 16, midiChannel) - 1; }

    VoiceSettings currentVoiceSettings() const noexcept
    {
        VoiceSettings voice;
        for (size_t k = 0; k < (size_t) numOscillators; ++k)
        {
            auto& oscillator = voice.oscillators[k];
            oscillator.wave        = settings.oscillators[k].wave;
            oscillator.ratio       = tuning (settings.oscillators[k].semitones, smoothed.fine[k].getCurrentValue());
            oscillator.level       = smoothed.level[k].getCurrentValue();
            oscillator.voices      = settings.oscillators[k].voices;
            oscillator.detuneCents = smoothed.detune[k].getCurrentValue();
            oscillator.width       = smoothed.width[k].getCurrentValue();
        }
        return voice;
    }

    /** An oscillator's pitch against the note, from its PITCH and FINE. */
    static float tuning (int semitones, float fineCents) noexcept
    {
        return std::exp2 (((float) semitones + fineCents / 100.0f) / 12.0f);
    }

    /** Where an oscillator's level heads: its LEVEL while it is on, routed
        to the output and switched on, silence otherwise. */
    static float levelTarget (const OscillatorSettings& oscillator) noexcept
    {
        return oscillator.on && oscillator.routed && oscillator.active ? oscillator.level : 0.0f;
    }

    // The voice that has been releasing longest, or failing that the oldest.
    SynthVoice& voiceToSteal() noexcept
    {
        SynthVoice* oldestReleasing = nullptr;
        SynthVoice* oldest          = &voices.front();
        for (auto& voice : voices)
        {
            if (voice.getAge() < oldest->getAge())
                oldest = &voice;
            if (voice.isReleasing() && (oldestReleasing == nullptr || voice.getAge() < oldestReleasing->getAge()))
                oldestReleasing = &voice;
        }
        return oldestReleasing != nullptr ? *oldestReleasing : *oldest;
    }

    // Hands each oscillator's envelope to the voices: the one that shapes
    // it, or the short gate when none does. Only what changed is handed on.
    void applyEnvelopes() noexcept
    {
        for (size_t k = 0; k < (size_t) numOscillators; ++k)
        {
            const int shapedBy = settings.oscillators[k].envelope;
            Envelope::Parameters envelope;
            envelope.attack  = gateAttackSeconds;
            envelope.decay   = 0.0f;
            envelope.release = gateReleaseSeconds;
            if (shapedBy >= 0)
            {
                const auto& shape = settings.envelopes[(size_t) shapedBy];
                envelope.delay   = juce::jmax (0.0f, shape.delaySeconds);
                envelope.attack  = juce::jmax (0.001f, shape.attackSeconds);
                envelope.hold    = juce::jmax (0.0f, shape.holdSeconds);
                envelope.decay   = juce::jmax (0.001f, shape.decaySeconds);
                envelope.sustain = juce::jlimit (0.0f, 1.0f, shape.sustainLevel);
                envelope.release = juce::jmax (0.005f, shape.releaseSeconds);
                envelope.attackCurve  = shape.attackCurve;
                envelope.decayCurve   = shape.decayCurve;
                envelope.releaseCurve = shape.releaseCurve;
            }

            auto& applied = appliedEnvelopes[k];
            if (sameStages (envelope, applied))
                continue;

            applied = envelope;
            for (auto& voice : voices)
                voice.setEnvelope ((int) k, envelope);
        }
    }

    std::array<SynthVoice, maxNotes> voices;
    Settings settings;
    Effects::Chain effects;

    struct Smoothed
    {
        juce::SmoothedValue<float> gain;
        std::array<juce::SmoothedValue<float>, numOscillators> fine;
        std::array<juce::SmoothedValue<float>, numOscillators> level;
        std::array<juce::SmoothedValue<float>, numOscillators> detune;
        std::array<juce::SmoothedValue<float>, numOscillators> width;
    };
    Smoothed smoothed;

    std::array<Envelope::Parameters, numOscillators> appliedEnvelopes {};

    static bool sameStages (const Envelope::Parameters& a, const Envelope::Parameters& b) noexcept
    {
        const auto same = [] (float x, float y) { return juce::exactlyEqual (x, y); };
        return same (a.delay, b.delay) && same (a.attack, b.attack) && same (a.hold, b.hold)
            && same (a.decay, b.decay) && same (a.sustain, b.sustain) && same (a.release, b.release)
            && same (a.attackCurve, b.attackCurve) && same (a.decayCurve, b.decayCurve) && same (a.releaseCurve, b.releaseCurve);
    }
    juce::Random  random { 0x5eed };
    std::uint64_t noteCounter = 0;
    std::array<bool, 16> sustainDown {};   // per MIDI channel
};

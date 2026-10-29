#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Oscillator.h"
#include "RtSafety.h"
#include "SynthVoice.h"
#include "Unison.h"

/**
    Synth2Engine

    The DSP heart of the synth, deliberately kept free of any host / plugin
    plumbing so it can be unit-tested or reused on its own.

    Sixteen notes of polyphony, each a stack of VOICES unison copies, DETUNEd
    and spread across the stereo field by WIDTH (see Unison.h), under an
    ATTACK / RELEASE envelope. Every copy plays OSC 1 and, when it is switched
    on, OSC 2: each oscillator in one of the four basic shapes, at its own
    PITCH (semitones) and FINE tune (cents), at its own LEVEL:

        note  ->  VOICES x (OSC 1 + OSC 2)  ->  detune + pan  ->  envelope  ->  OUTPUT

    MIDI is read sample-accurately: the block is rendered up to each event,
    then the event applied. A seventeenth note takes over the voice that has
    been releasing longest (or, failing that, the oldest held one). Each MIDI
    channel keeps its own notes: the same key on two channels plays two
    voices, and each channel has its own sustain pedal (CC 64), All Sound Off
    (CC 120, silences at once) and All Notes Off (CC 123, releases).

    DETUNE, WIDTH, OUTPUT and each oscillator's FINE and LEVEL glide to new
    values over 20 ms, so turning them while a chord plays doesn't zipper;
    switching OSC 2 on or off fades it in or out the same way.
*/
class Synth2Engine
{
public:
    static constexpr int   maxNotes     = 16;
    static constexpr int   maxSemitones = 24;      // each oscillator's PITCH, either way
    static constexpr float maxFineCents = 50.0f;   // and its FINE tune

    struct Settings
    {
        Waveform wave           = Waveform::saw;   // OSC 1's shape
        int      voices         = 1;
        float    detuneCents    = 15.0f;
        float    width          = 0.5f;
        float    attackSeconds  = 0.005f;
        float    releaseSeconds = 0.3f;
        float    outputDb       = 0.0f;

        int      osc1Semitones  = 0;
        float    osc1FineCents  = 0.0f;
        float    osc1Level      = 1.0f;
        bool     osc2On         = false;
        Waveform osc2Wave       = Waveform::square;
        int      osc2Semitones  = -12;
        float    osc2FineCents  = 0.0f;
        float    osc2Level      = 0.7f;
    };

    void prepare (double sampleRate)
    {
        for (auto& voice : voices)
            voice.prepare (sampleRate);

        constexpr double glide = 0.02;
        smoothed.detune.reset (sampleRate, glide);
        smoothed.width .reset (sampleRate, glide);
        smoothed.gain  .reset (sampleRate, glide);
        smoothed.detune.setCurrentAndTargetValue (settings.detuneCents);
        smoothed.width .setCurrentAndTargetValue (settings.width);
        smoothed.gain  .setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.outputDb));
        for (auto* value : { &smoothed.fine1, &smoothed.level1, &smoothed.fine2, &smoothed.level2 })
            value->reset (sampleRate, glide);
        smoothed.fine1 .setCurrentAndTargetValue (settings.osc1FineCents);
        smoothed.level1.setCurrentAndTargetValue (settings.osc1Level);
        smoothed.fine2 .setCurrentAndTargetValue (settings.osc2FineCents);
        smoothed.level2.setCurrentAndTargetValue (osc2Target());

        appliedEnvelope.attack = -1.0f;   // hand every voice the envelope afresh
        applyEnvelope();
        sustainDown = {};
    }

    void setSettings (const Settings& newSettings) noexcept
    {
        settings = newSettings;
        settings.voices      = Unison::clampVoices (settings.voices);
        settings.detuneCents = juce::jlimit (0.0f, Unison::maxDetuneCents, settings.detuneCents);
        settings.width       = juce::jlimit (0.0f, 1.0f, settings.width);
        for (auto* semitones : { &settings.osc1Semitones, &settings.osc2Semitones })
            *semitones = juce::jlimit (-maxSemitones, maxSemitones, *semitones);
        for (auto* cents : { &settings.osc1FineCents, &settings.osc2FineCents })
            *cents = juce::jlimit (-maxFineCents, maxFineCents, *cents);
        for (auto* level : { &settings.osc1Level, &settings.osc2Level })
            *level = juce::jlimit (0.0f, 1.0f, *level);

        smoothed.fine1 .setTargetValue (settings.osc1FineCents);
        smoothed.level1.setTargetValue (settings.osc1Level);
        smoothed.fine2 .setTargetValue (settings.osc2FineCents);
        smoothed.level2.setTargetValue (osc2Target());
        smoothed.detune.setTargetValue (settings.detuneCents);
        smoothed.width .setTargetValue (settings.width);
        smoothed.gain  .setTargetValue (juce::Decibels::decibelsToGain (settings.outputDb, -100.0f));
        applyEnvelope();
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

    /** Silences every note on `midiChannel` at once, as All Sound Off does. */
    void allSoundOff (int midiChannel) noexcept
    {
        sustainDown[channelIndex (midiChannel)] = false;
        for (auto& voice : voices)
            if (voice.getChannel() == midiChannel)
                voice.kill();
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

    /** Where every voice is in its envelope (idle ones included). */
    std::array<NoteActivity, maxNotes> getActivity() const noexcept
    {
        std::array<NoteActivity, maxNotes> activity {};
        for (size_t i = 0; i < voices.size(); ++i)
            activity[i] = voices[i].getActivity();
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

            // Detune, width and the oscillators' tuning and levels move once
            // per chunk (at most 32 samples; the voices glide the levels across
            // it), which is fine-grained enough for pitch and pan; the output
            // gain moves every sample.
            for (auto* value : { &smoothed.detune, &smoothed.width, &smoothed.fine1, &smoothed.level1, &smoothed.fine2, &smoothed.level2 })
                value->skip (length);
            const auto chunkSettings = currentVoiceSettings();

            std::array<float, SynthVoice::maxChunk> mixL {};
            std::array<float, SynthVoice::maxChunk> mixR {};
            for (auto& voice : voices)
                voice.render (mixL.data(), mixR.data(), length, chunkSettings);

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
        voice.wave        = settings.wave;
        voice.voices      = settings.voices;
        voice.detuneCents = smoothed.detune.getCurrentValue();
        voice.width       = smoothed.width.getCurrentValue();
        voice.ratio1      = tuning (settings.osc1Semitones, smoothed.fine1.getCurrentValue());
        voice.level1      = smoothed.level1.getCurrentValue();
        voice.wave2       = settings.osc2Wave;
        voice.ratio2      = tuning (settings.osc2Semitones, smoothed.fine2.getCurrentValue());
        voice.level2      = smoothed.level2.getCurrentValue();
        return voice;
    }

    /** An oscillator's pitch against the note, from its PITCH and FINE. */
    static float tuning (int semitones, float fineCents) noexcept
    {
        return std::exp2 (((float) semitones + fineCents / 100.0f) / 12.0f);
    }

    /** Where OSC 2's level heads: its LEVEL while it is on, silence while off. */
    float osc2Target() const noexcept { return settings.osc2On ? settings.osc2Level : 0.0f; }

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

    void applyEnvelope() noexcept
    {
        juce::ADSR::Parameters envelope;
        envelope.attack  = juce::jmax (0.001f, settings.attackSeconds);
        envelope.decay   = 0.0f;
        envelope.sustain = 1.0f;
        envelope.release = juce::jmax (0.005f, settings.releaseSeconds);

        if (juce::exactlyEqual (envelope.attack, appliedEnvelope.attack)
            && juce::exactlyEqual (envelope.release, appliedEnvelope.release))
            return;

        appliedEnvelope = envelope;
        for (auto& voice : voices)
            voice.setEnvelope (envelope);
    }

    std::array<SynthVoice, maxNotes> voices;
    Settings settings;

    struct Smoothed
    {
        juce::SmoothedValue<float> detune;
        juce::SmoothedValue<float> width;
        juce::SmoothedValue<float> gain;
        juce::SmoothedValue<float> fine1;
        juce::SmoothedValue<float> level1;
        juce::SmoothedValue<float> fine2;
        juce::SmoothedValue<float> level2;
    };
    Smoothed smoothed;

    juce::ADSR::Parameters appliedEnvelope {};
    juce::Random  random { 0x5eed };
    std::uint64_t noteCounter = 0;
    std::array<bool, 16> sustainDown {};   // per MIDI channel
};

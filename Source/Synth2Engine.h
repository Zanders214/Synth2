#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

#include "Oscillator.h"
#include "RtSafety.h"
#include "SynthVoice.h"
#include "Unison.h"

/**
    Synth2Engine

    The DSP heart of the synth, deliberately kept free of any host / plugin
    plumbing so it can be unit-tested or reused on its own.

    Sixteen notes of polyphony, each a stack of VOICES unison oscillators in
    one of the four basic shapes, DETUNEd and spread across the stereo field by
    WIDTH (see Unison.h), under an ATTACK / RELEASE envelope:

        note  ->  VOICES x oscillator  ->  detune + pan  ->  envelope  ->  OUTPUT

    MIDI is read sample-accurately: the block is rendered up to each event,
    then the event applied. A seventeenth note takes over the voice that has
    been releasing longest (or, failing that, the oldest held one). Each MIDI
    channel keeps its own notes: the same key on two channels plays two
    voices, and each channel has its own sustain pedal (CC 64), All Sound Off
    (CC 120, silences at once) and All Notes Off (CC 123, releases).

    DETUNE, WIDTH and OUTPUT glide to new values over 20 ms, so turning them
    while a chord plays doesn't zipper.
*/
class Synth2Engine
{
public:
    static constexpr int maxNotes = 16;

    struct Settings
    {
        Waveform wave           = Waveform::saw;
        int      voices         = 1;
        float    detuneCents    = 15.0f;
        float    width          = 0.5f;
        float    attackSeconds  = 0.005f;
        float    releaseSeconds = 0.3f;
        float    outputDb       = 0.0f;
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
        for (const auto& voice : voices)
            if (voice.isActive() && voice.getNote() == midiNote)
                return true;
        return false;
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
        for (const auto& voice : voices)
            if (voice.plays (midiChannel, midiNote))
                return true;
        return false;
    }

    bool isSustainPedalDown (int midiChannel) const noexcept { return sustainDown[channelIndex (midiChannel)]; }

private:
    void handleMidi (const juce::uint8* data, int numBytes) noexcept
    {
        if (numBytes < 1)
            return;

        const int status  = data[0] & 0xf0;
        const int channel = (data[0] & 0x0f) + 1;
        const int first   = numBytes > 1 ? data[1] & 0x7f : 0;
        const int second  = numBytes > 2 ? data[2] & 0x7f : 0;

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

            // Detune and width move once per chunk (at most 32 samples), which
            // is fine-grained enough for pitch and pan; the output gain moves
            // every sample.
            smoothed.detune.skip (length);
            smoothed.width .skip (length);
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
        return { settings.wave, settings.voices,
                 smoothed.detune.getCurrentValue(), smoothed.width.getCurrentValue() };
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
    };
    Smoothed smoothed;

    juce::ADSR::Parameters appliedEnvelope {};
    juce::Random  random { 0x5eed };
    std::uint64_t noteCounter = 0;
    std::array<bool, 16> sustainDown {};   // per MIDI channel
};

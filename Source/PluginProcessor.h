#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

#include "Synth2Engine.h"

/**
    Synth2AudioProcessor

    Thin host wrapper around Synth2Engine. It owns the parameter state and feeds
    the engine's settings each block; all DSP lives in Synth2Engine.

    The editor's on-screen keyboard plays through keyboardState. Its notes reach
    the audio thread through a lock-free FIFO (JUCE's own hand-over locks), and
    the keys the host is playing are lit on it from the editor's timer.
*/
class Synth2AudioProcessor : public juce::AudioProcessor,
                             private juce::MidiKeyboardState::Listener
{
public:
    Synth2AudioProcessor();
    ~Synth2AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;   // keep the double-precision overload visible

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState&       getState() noexcept       { return apvts; }
    const juce::AudioProcessorValueTreeState& getState() const noexcept { return apvts; }

    /** The on-screen keyboard's notes, played alongside the host's MIDI. Message
        thread only. */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    /** Lights the on-screen keys the host is holding down, and unlights the
        ones it let go (keys held with the mouse are left alone). Message thread. */
    void showHostNotesOnKeyboard();

    /** How many notes are sounding, for the tests and the editor. */
    int getNumActiveNotes() const noexcept { return activeNotes.load(); }

    /** Where each voice was in its envelope at the end of the last block, for
        the AMP display. Any thread. */
    std::array<NoteActivity, Synth2Engine::maxNotes> getNoteActivity() const noexcept;

    /** The engine's settings, from the parameters. */
    Synth2Engine::Settings currentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // MidiKeyboardState::Listener: a key on the on-screen keyboard went down / up.
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void playOnScreenKeys() noexcept;

    juce::AudioProcessorValueTreeState apvts;
    Synth2Engine engine;
    std::atomic<int> activeNotes { 0 };

    // Each voice's envelope, published by the audio thread every block.
    struct PublishedActivity
    {
        std::atomic<int>   stage       { 0 };
        std::atomic<float> level       { 0.0f };
        std::atomic<float> releaseFrom { 1.0f };
    };
    std::array<PublishedActivity, Synth2Engine::maxNotes> noteActivity;

    // The on-screen keyboard. Key presses go to the audio thread through
    // keyFifo (velocity 0 = key up); the keys the host holds come back as
    // hostHeldNotes, one bit per note.
    struct KeyEvent
    {
        int   note     = 0;
        float velocity = 0.0f;
    };
    static constexpr int keyFifoSize = 256;
    juce::MidiKeyboardState keyboardState;
    juce::AbstractFifo keyFifo { keyFifoSize };
    std::array<KeyEvent, keyFifoSize> keyEvents {};
    std::array<std::atomic<std::uint64_t>, 2> hostHeldNotes {};
    std::bitset<128> keysHeldByMouse;
    bool showingHostNotes = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2AudioProcessor)
};

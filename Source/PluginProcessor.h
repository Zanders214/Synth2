#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

#include "Bench.h"
#include "Synth2Engine.h"

/**
    Synth2AudioProcessor

    Thin host wrapper around Synth2Engine. It owns the parameter state (and in
    it the bench's layout, see Bench.h) and feeds the engine's settings each
    block; all DSP lives in Synth2Engine.

    The editor's on-screen keyboard plays through keyboardState. Its notes reach
    the audio thread through a lock-free FIFO (JUCE's own hand-over locks), and
    the keys the host is playing are lit on it from the editor's timer.
*/
class Synth2AudioProcessor : public juce::AudioProcessor,
                             private juce::MidiKeyboardState::Listener,
                             private juce::ValueTree::Listener,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    /** How sound flows between the modules on the bench: by number, in the
        order they are laid out, or along the cables patched between them. */
    using SignalFlow = Bench::Flow;

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

    /** Where each voice was in oscillator `osc`'s envelope at the end of the
        last block, for the envelopes' AMP displays. Any thread. */
    std::array<NoteActivity, Synth2Engine::maxNotes> getNoteActivity (int osc) const noexcept;

    /** The engine's settings, from the parameters. */
    Synth2Engine::Settings currentSettings() const noexcept;

    /** Whether oscillator `oscillator` (0..3) is patched through to the
        output, for when the signal flow follows the cables. Any thread. */
    void setRoutedToOutput (int oscillator, bool routed) noexcept;

    /** Loads factory preset `index` (see Presets.h), even if it is the one
        already loaded, so picking it again undoes any edits. Message thread. */
    void loadPreset (int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // MidiKeyboardState::Listener: a key on the on-screen keyboard went down / up.
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void playOnScreenKeys() noexcept;

    // What the bench decides for the sound (which envelope shapes each
    // oscillator, and which reach the output along the cables), worked out
    // again whenever the bench changes: a module added, removed, moved or
    // patched, or a whole new state. Message thread.
    void followBench();
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // The bench followed, or a host's program change loaded, on the message
    // thread when either came from off it.
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    Synth2Engine engine;
    std::atomic<int> activeNotes { 0 };
    std::array<std::atomic<bool>, Synth2Engine::numOscillators> routedToOutput { true, true, true, true };

    // Which envelope shapes each oscillator (-1 for none), for each way sound
    // can flow, so the audio thread just picks the row for the flow in use.
    using Assignment = std::array<std::atomic<int>, Synth2Engine::numOscillators>;
    std::array<Assignment, 3> envelopeFor {};

    // The factory preset last loaded (kept in the state too, so a host that
    // sets the saved program again after restoring a session changes nothing),
    // and one a host asked for off the message thread.
    std::atomic<int> currentPreset { 0 };
    std::atomic<int> pendingPreset { -1 };

    // Each voice's envelope for each oscillator, published by the audio thread every block.
    struct PublishedActivity
    {
        std::atomic<int>   stage       { 0 };
        std::atomic<float> level       { 0.0f };
        std::atomic<float> releaseFrom { 1.0f };
    };
    std::array<std::array<PublishedActivity, Synth2Engine::maxNotes>, Synth2Engine::numOscillators> noteActivity;

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

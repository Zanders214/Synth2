#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

#include "Arp.h"
#include "Bench.h"
#include "Synth2Engine.h"
#include "UserPresets.h"

/**
    Synth2AudioProcessor

    Thin host wrapper around Synth2Engine. It owns the parameter state (and in
    it the bench's layout, see Bench.h) and feeds the engine's settings each
    block; all DSP lives in Synth2Engine.

    The editor's on-screen keyboard plays through keyboardState. Its notes reach
    the audio thread through a lock-free FIFO (JUCE's own hand-over locks), and
    the keys the host is playing are lit on it from the editor's timer.

    Both go through the arp (see Arp.h) on their way to the engine: switched
    off, it lets them through as they are; on, it plays its pattern from the
    keys held instead, in time with the host.
*/
class Synth2AudioProcessor : public juce::AudioProcessor,
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
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keys.getState(); }

    /** Lights the on-screen keys the host is holding down, and unlights the
        ones it let go (keys held with the mouse are left alone). Message thread. */
    void showHostNotesOnKeyboard();

    /** What the voices were doing at the end of the last block, published
        by the audio thread for the editor's displays and the tests. */
    class Activity
    {
    public:
        /** How many notes are sounding. Any thread. */
        int getNumActiveNotes() const noexcept { return activeNotes.load(); }

        /** Where each voice was in oscillator `osc`'s envelope, for the
            envelopes' AMP displays. Any thread. */
        std::array<NoteActivity, Synth2Engine::maxNotes> getNoteActivity (int osc) const noexcept;

        /** Where each voice was in the sweep envelope of the filter on
            oscillator `osc`, for the filters' SWEEP displays. Any thread. */
        std::array<NoteActivity, Synth2Engine::maxNotes> getSweepActivity (int osc) const noexcept;

        /** Takes it all from `voices`, at the end of a block. Audio thread. */
        void publish (const Synth2Engine& voices) noexcept;

    private:
        struct PublishedActivity
        {
            std::atomic<int>   stage       { 0 };
            std::atomic<float> level       { 0.0f };
            std::atomic<float> progress    { 0.0f };

            void publish (const NoteActivity& note) noexcept
            {
                stage   .store ((int) note.stage);
                level   .store (note.level);
                progress.store (note.progress);
            }
        };
        using PublishedNotes = std::array<PublishedActivity, Synth2Engine::maxNotes>;

        static std::array<NoteActivity, Synth2Engine::maxNotes> read (const PublishedNotes&) noexcept;

        std::atomic<int> activeNotes { 0 };
        std::array<PublishedNotes, Synth2Engine::numOscillators> noteActivity;
        std::array<PublishedNotes, Synth2Engine::numOscillators> sweepActivity;
    };

    const Activity& getActivity() const noexcept { return activity; }

    /** The engine's settings, from the parameters. */
    Synth2Engine::Settings currentSettings() const noexcept;

    /** The arp's, from its parameters and the pattern in the state. Any thread. */
    Arp::Settings arpSettings() const noexcept;

    /** The arp, for the ARP tab to show the step it plays and the notes
        its rows are playing. */
    const Arp::Player& getArp() const noexcept { return arp.player; }

    /** Whether oscillator `oscillator` (0..3) is patched through to the
        output, for when the signal flow follows the cables. Any thread. */
    void setRoutedToOutput (int oscillator, bool routed) noexcept;

    /** Loads factory preset `index` (see Presets.h), even if it is the one
        already loaded, so picking it again undoes any edits. Message thread. */
    void loadPreset (int index);

    /** The presets the user saves, from the synth's side: where they are
        kept (see UserPresets.h), saving the synth as one, loading one,
        deleting one, and which preset is loaded. Message thread. */
    class PresetLoader
    {
    public:
        explicit PresetLoader (Synth2AudioProcessor& processorToLoadInto) : owner (processorToLoadInto) {}

        UserPresets& getUserPresets() noexcept { return store; }

        /** Loads the user preset `entry`: all of the synth as it was saved,
            the bench and the effects with it; false if its file can't be
            read. The host's program stays as it was. */
        bool loadUserPreset (const UserPresets::Entry& entry);

        /** Saves the synth as it is now as user preset `name`, replacing one
            already saved under that name, and has it loaded; what was saved,
            or nothing if it couldn't be. */
        std::optional<UserPresets::Entry> saveUserPreset (const juce::String& name, const juce::String& detail, Presets::Category category);

        /** Deletes the user preset `entry` (to the Trash). */
        bool deleteUserPreset (const UserPresets::Entry& entry);

        /** The preset loaded: the user preset's name if one was loaded or
            saved since the last factory preset, otherwise the factory preset's. */
        juce::String getPresetName() const;

        /** The user preset loaded, or empty if the last was a factory one. */
        juce::String getUserPresetName() const;

    private:
        Synth2AudioProcessor& owner;
        UserPresets store;

        JUCE_DECLARE_NON_COPYABLE (PresetLoader)
    };

    PresetLoader& getPresetLoader() noexcept { return presetLoader; }

private:
    // Sets the synth to `restored` (a saved state), bringing an older one up
    // to date; with `missingToDefaults`, the parameters it doesn't mention go
    // back to their defaults, otherwise they mostly stay as they are.
    void restoreState (const juce::ValueTree& restored, bool missingToDefaults);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // The on-screen keyboard. Key presses go to the audio thread through a
    // FIFO (velocity 0 = key up); the keys the host holds come back one bit
    // per note, to be lit on it.
    class OnScreenKeys final : private juce::MidiKeyboardState::Listener
    {
    public:
        OnScreenKeys() { keyboardState.addListener (this); }
        ~OnScreenKeys() override { keyboardState.removeListener (this); }

        juce::MidiKeyboardState& getState() noexcept { return keyboardState; }

        // Audio thread: adds the keys pressed since the last block to
        // `midi`, at its start; and takes note of the keys the host holds.
        void addTo (juce::MidiBuffer& midi) noexcept;
        void setHostHeld (const std::array<std::uint64_t, 2>& held) noexcept;

        // Message thread: see showHostNotesOnKeyboard.
        void showHostNotes();

    private:
        // MidiKeyboardState::Listener: a key on the on-screen keyboard went down / up.
        void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
        void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

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

        JUCE_DECLARE_NON_COPYABLE (OnScreenKeys)
    };

    // Works out again what the bench decides for the sound (which envelope
    // shapes each oscillator, and which reach the output along the cables)
    // whenever the bench changes: a module added, removed, moved or patched,
    // or a whole new state; and the order the effects play in (see Rack.h)
    // whenever one goes into the chain or out, or a card moves. Message
    // thread; a module or an effect coming or going off it is followed there
    // shortly after.
    class Follower final : private juce::ValueTree::Listener,
                           private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        explicit Follower (Synth2AudioProcessor& processorToFollowFor);
        ~Follower() override;

        void followBench();
        void followRack();
        void followArp();

    private:
        void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
        void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
        void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
        void valueTreeRedirected (juce::ValueTree& tree) override;
        void parameterChanged (const juce::String& parameterID, float newValue) override;

        Synth2AudioProcessor& owner;

        JUCE_DECLARE_NON_COPYABLE (Follower)
    };

    // The bench and the effects followed, or a host's program change
    // loaded, on the message thread when either came from off it.
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;

    // The filters' parameters, found once: currentSettings reads them every
    // block, and looking sixty parameters up by name each time costs as much
    // as a small chord's voices.
    struct FilterValues
    {
        const std::atomic<float>* active    = nullptr;
        const std::atomic<float>* type      = nullptr;
        const std::atomic<float>* cutoff    = nullptr;
        const std::atomic<float>* resonance = nullptr;
        const std::atomic<float>* envAmount = nullptr;
        std::array<const std::atomic<float>*, 9> sweep {};   // DELAY..RELEASE, then the three bends
    };
    std::array<FilterValues, Synth2Engine::numFilters> filterValues;
    Synth2Engine engine;
    std::array<std::atomic<bool>, Synth2Engine::numOscillators> routedToOutput { true, true, true, true };
    std::atomic<double> hostBpm { 120.0 };   // the host's tempo, as of the last block
    std::atomic<int> effectsChain { Effects::encode (Effects::defaultOrder) };   // the effects' order, encoded

    // The arp, and its pattern from the state, a step to an atomic. The
    // keys played (the on-screen keyboard's, then the host's) are gathered
    // in `played`, and the arp plays them into `out` for the engine; both
    // have their room (Arp::midiRoom) set aside up front.
    struct ArpParts
    {
        Arp::Player player;
        std::array<std::atomic<std::uint32_t>, Arp::maxSteps> steps {};
        std::atomic<int> length { 8 };
        juce::MidiBuffer played;
        juce::MidiBuffer out;
    };
    ArpParts arp;

    // Which envelope shapes and which filter filters each oscillator (-1 for
    // none), for each way sound can flow, so the audio thread just picks the
    // row for the flow in use.
    using Assignment = std::array<std::atomic<int>, Synth2Engine::numOscillators>;
    std::array<Assignment, 3> envelopeFor {};
    std::array<Assignment, 3> filterFor {};

    // The factory preset last loaded (kept in the state too, so a host that
    // sets the saved program again after restoring a session changes nothing),
    // and one a host asked for off the message thread.
    std::atomic<int> currentPreset { 0 };
    std::atomic<int> pendingPreset { -1 };

    Activity activity;

    PresetLoader presetLoader { *this };
    OnScreenKeys keys;
    Follower     follower { *this };   // last: it reads and fills in the state above

    JUCE_LEAK_DETECTOR (Synth2AudioProcessor)   // AudioProcessor already can't be copied
};

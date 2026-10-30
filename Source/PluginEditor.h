#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <optional>

#include "PluginProcessor.h"
#include "Bench.h"
#include "BenchModules.h"
#include "BenchView.h"
#include "EffectsView.h"
#include "Synth2LookAndFeel.h"
#include "NeonKeyboard.h"
#include "WavePicker.h"

/**
    Synth2AudioProcessorEditor — the Neon Plugins face for Synth2: a workbench.

    A dark panel, top to bottom: the wordmark, the BENCH / EFFECTS tabs, the
    preset picker, the POLY badge and the gear (the signal-flow settings);
    the bench, which starts empty and holds whatever modules are put on it,
    wherever they are put (see BenchView), or on the EFFECTS tab the effects
    every note goes through, put there and arranged the same way (see
    EffectsView); and along the bottom, on both tabs, the keyboard that plays
    it and the OUTPUT dial.

    In the standalone app the computer's keys play that keyboard too (see
    NeonKeyboard): the editor keeps the keyboard focus whenever nothing else
    in the window has it, and passes on the keys nothing else used.

    Click an empty spot on the bench to add a module there: an oscillator,
    with its own unison stack, or an envelope (up to four of each). Drag a
    module by its header to move it; its cross takes it off the bench. The
    EFFECTS tab works the same way, with a drive, a delay and a reverb (one
    of each), played in the order their cards read in.

    The preset picker shows the factory preset last loaded: its arrows step
    to the previous and next one, and a click on its name opens a tray of
    them all.

    The gear picks how sound flows between the modules, and so which
    oscillator each envelope shapes: by number (ENV 1 shapes OSC 1),
    following the layout (each module shows its place in the order, and each
    envelope takes the first free oscillator before it), or along patch
    cables, OSC -> ENV -> OUT. With patch cables the modules get jacks: drag
    from an OUT to an envelope's IN or to the output's IN to patch a cable
    (an oscillator that doesn't reach the output is greyed out and silent),
    and drag a cable off either end to unplug it. An oscillator no envelope
    shapes simply starts and stops with its key.

    Everything is drawn natively (no WebView) and scales proportionally from
    the 1040 px design.
*/
class Synth2AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit Synth2AudioProcessorEditor (Synth2AudioProcessor&);
    ~Synth2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    /** Whether the computer's keys play the keyboard: in the standalone app. */
    bool playsTypedKeys() const noexcept { return processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }

    static constexpr float designWidth  = 1040.0f;
    static constexpr float designHeight = 916.0f;

    /** What fills the space over the keyboard. */
    enum class Tab { bench, effects };

    void showTab (Tab tab);
    Tab  getTab() const noexcept { return tab; }
    juce::Rectangle<float> tabBounds (Tab which) const noexcept
    {
        return (which == Tab::bench ? layout.benchTab : layout.effectsTab).toFloat();
    }

    /** The patch cables, drawn over the bench and the bottom strip without
        taking any clicks from them, and the patching: a cable picked up from
        a jack (or the output's IN), dragged, and dropped on another or let
        go. Everything here is in the editor's coordinates. */
    class CableLayer final : public juce::Component
    {
    public:
        explicit CableLayer (Synth2AudioProcessorEditor& ownerToUse) : owner (ownerToUse) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;

        /** Where the output's IN jack sits. */
        juce::Rectangle<float> outputJackBounds() const noexcept;
        /** Where one of module `id`'s jacks sits. */
        juce::Point<float> jackCentre (const Bench::ModuleId& id, BenchModule::Jack which = BenchModule::Jack::out) const;
        bool isPatching() const noexcept { return patching.from.has_value(); }

        /** One of module `id`'s jacks was pressed, dragged or let go. */
        void patch (const Bench::ModuleId& id, BenchModule::Jack which, const juce::MouseEvent& e, BenchModule::JackPhase phase);
        /** The editor was pressed at `at`: on the output's IN jack (with the
            cables showing), that takes the last cable patched into it. */
        void pickUpFromOutput (juce::Point<float> at);
        /** The mouse moved to `at`, or let go there, in the editor. */
        void dragTo (juce::Point<float> at);
        void letGo (juce::Point<float> at);

        /** With the cables: every module's jacks, lit where a cable is
            plugged in, and greyed out what isn't heard. */
        void showOnModules();
        /** Every cable and where its ends are, to repaint only when that changes. */
        juce::String signature() const;

    private:
        void dropCable (juce::Point<float> at);
        void paintCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to, juce::Colour colour) const;

        // A cable being dragged: from which module, to where the mouse is.
        struct Patching
        {
            std::optional<Bench::ModuleId> from;
            juce::Point<float> to;
        };

        Synth2AudioProcessorEditor& owner;
        Patching patching;
    };

    /** The trays opened over everything: add a module (on the bench), add an
        effect (on the EFFECTS tab), the gear's settings and the presets; and
        the preset picker's arrows, which step through the presets. */
    class Trays
    {
    public:
        explicit Trays (Synth2AudioProcessorEditor& ownerToUse);

        ChoiceTray& getAdd() noexcept      { return addTray; }
        ChoiceTray& getEffect() noexcept   { return effectTray; }
        ChoiceTray& getSettings() noexcept { return settingsTray; }
        ChoiceTray& getPreset() noexcept   { return presetTray; }
        std::array<ChoiceTray*, 4> all() noexcept { return { &addTray, &effectTray, &settingsTray, &presetTray }; }
        bool isSettingsOpen() const noexcept { return settingsTray.isOpen(); }
        bool isPresetsOpen() const noexcept  { return presetTray.isOpen(); }

        void openAdd (juce::Point<float> benchAt, juce::Point<int> editorAt);
        void openEffect (juce::Point<float> rackAt, juce::Point<int> editorAt);
        void openSettings();
        void openPresets();
        void stepPreset (int delta);

    private:
        Synth2AudioProcessorEditor& owner;
        ChoiceTray addTray;
        ChoiceTray effectTray;
        ChoiceTray settingsTray;
        ChoiceTray presetTray;
        juce::Point<float> addAt;      // on the bench
        juce::Point<float> effectAt;   // on the EFFECTS tab
    };

    // For the tests: the bench, the cables, the trays, the OUTPUT dial and
    // keyboard, and where the gear and the preset picker sit (in the editor).
    BenchView&    getBench() noexcept                  { return benchView; }
    EffectsView&  getEffects() noexcept                { return effectsView; }
    CableLayer&   getCables() noexcept                 { return cables; }
    Trays&        getTrays() noexcept                  { return trays; }
    WaveMenu&     getWaveMenu (int slot) noexcept      { return waveMenus[(size_t) slot]; }
    juce::Slider& getOutputDial() noexcept             { return outputDial; }
    NeonKeyboard& getKeyboard() noexcept               { return keyboard; }
    juce::Rectangle<float> gearBounds() const noexcept { return layout.gear.toFloat(); }
    juce::Rectangle<float> presetBounds() const noexcept { return layout.preset.toFloat(); }

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&, float scale) const;
    void paintOutput (juce::Graphics&, float scale) const;

    Synth2AudioProcessor::SignalFlow signalFlow() const;
    void showView();   // shows the cards as the bench's view says (gear menu, or a restored set)
    void showFlow();
    AmpDisplayState ampState (const Synth2Engine::Settings& settings, int envelope, const std::array<int, Bench::numOscillators>& shapedBy) const;

    Synth2AudioProcessor& processorRef;
    Synth2LookAndFeel looks;
    Bench::Model model;

    BenchView    benchView;
    EffectsView  effectsView;
    NeonKeyboard keyboard;
    juce::Slider outputDial;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    CableLayer   cables { *this };
    std::array<WaveMenu, Bench::numOscillators> waveMenus;
    Trays        trays { *this };

    // Painted regions computed by resized() and read back by paint().
    struct Layout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> benchTab;
        juce::Rectangle<int> effectsTab;
        juce::Rectangle<int> preset;
        juce::Rectangle<int> badge;
        juce::Rectangle<int> gear;
        juce::Rectangle<int> outputColumn;
        int stripSeparatorY = 0;
    };
    Layout layout;

    // What was last shown, to repaint only what changed.
    struct Shown
    {
        juce::String cables;
        float output = 0.0f;
        int   flow   = -1;
        int   preset = -1;
        Bench::View view;
    };
    Shown shown;
    Tab   tab     = Tab::bench;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2AudioProcessorEditor)
};

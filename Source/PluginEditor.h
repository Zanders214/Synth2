#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <optional>

#include "PluginProcessor.h"
#include "Bench.h"
#include "BenchModules.h"
#include "BenchView.h"
#include "Synth2LookAndFeel.h"
#include "NeonKeyboard.h"
#include "WavePicker.h"

/**
    Synth2AudioProcessorEditor — the Neon Plugins face for Synth2: a workbench.

    A dark panel, top to bottom: the wordmark, the POLY badge and the gear
    (the signal-flow settings); the bench, which starts empty and holds
    whatever modules are put on it, wherever they are put (see BenchView);
    and along the bottom the keyboard that plays it and the OUTPUT dial.

    Click an empty spot on the bench to add a module there: an oscillator,
    with its own unison stack, or an envelope (up to four of each). Drag a
    module by its header to move it; its cross takes it off the bench.

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

    static constexpr float designWidth  = 1040.0f;
    static constexpr float designHeight = 916.0f;

    // For the tests: the bench, the trays, the OUTPUT dial and keyboard, and
    // where the gear and the output's IN jack sit (in the editor).
    BenchView&    getBench() noexcept                  { return bench; }
    ChoiceTray&   getAddTray() noexcept                { return addTray; }
    ChoiceTray&   getSettingsTray() noexcept           { return settingsTray; }
    WaveMenu&     getWaveMenu (int slot) noexcept      { return waveMenus[(size_t) slot]; }
    juce::Slider& getOutputDial() noexcept             { return outputDial; }
    NeonKeyboard& getKeyboard() noexcept               { return keyboard; }
    juce::Rectangle<float> gearBounds() const noexcept { return layout.gear.toFloat(); }
    juce::Rectangle<float> outputJackBounds() const noexcept;
    bool isPatching() const noexcept                   { return patchingFrom.has_value(); }

    /** Where one of module `id`'s jacks sits, in the editor. */
    juce::Point<float> jackCentre (const Bench::ModuleId& id, BenchModule::Jack which = BenchModule::Jack::out);

private:
    // Draws the cables over the bench and the bottom strip, without taking
    // any clicks from them.
    class CableLayer final : public juce::Component
    {
    public:
        explicit CableLayer (Synth2AudioProcessorEditor& ownerToUse) : owner (ownerToUse) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { owner.paintCables (g); }

    private:
        Synth2AudioProcessorEditor& owner;
    };

    void timerCallback() override;
    void paintHeader (juce::Graphics&, float scale) const;
    void paintOutput (juce::Graphics&, float scale) const;
    void paintCables (juce::Graphics&);
    void paintCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to, juce::Colour colour) const;

    Synth2AudioProcessor::SignalFlow signalFlow() const;
    void showFlow();
    void openAddTray (juce::Point<float> benchAt, juce::Point<int> editorAt);
    void openSettings();
    void patch (const Bench::ModuleId& module, BenchModule::Jack which, const juce::MouseEvent& e, int phase);
    void dropCable (juce::Point<float> at);
    void showCables();
    juce::String cableSignature();
    AmpDisplayState ampState (const Synth2Engine::Settings& settings, int envelope, const std::array<int, Bench::numOscillators>& shapedBy) const;

    Synth2AudioProcessor& processorRef;
    Synth2LookAndFeel looks;
    Bench::Model model;

    BenchView    bench;
    NeonKeyboard keyboard;
    juce::Slider outputDial;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    CableLayer   cables { *this };
    std::array<WaveMenu, Bench::numOscillators> waveMenus;
    ChoiceTray   addTray;
    ChoiceTray   settingsTray;
    juce::Point<float> addAt;   // where on the bench the add tray was opened

    // Painted regions computed by resized() and read back by paint().
    struct Layout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> badge;
        juce::Rectangle<int> gear;
        juce::Rectangle<int> outputColumn;
        int stripSeparatorY = 0;
    };
    Layout layout;

    // A cable being dragged: from which oscillator, to where the mouse is.
    std::optional<Bench::ModuleId> patchingFrom;
    juce::Point<float> patchingTo;

    juce::String lastCables;
    float lastOutput = 0.0f;
    int   lastFlow   = -1;
    float uiScale    = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2AudioProcessorEditor)
};

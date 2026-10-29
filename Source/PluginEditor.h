#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "PluginProcessor.h"
#include "Synth2LookAndFeel.h"
#include "NeonKeyboard.h"
#include "VoiceDisplays.h"
#include "WavePicker.h"

/**
    Synth2AudioProcessorEditor — the Neon Plugins face for Synth2.

    A dark panel, top to bottom: the wordmark and POLY badge; then four groups
    in two rows, each a display over the three dials it shows. On top, the
    oscillators: the OSC 1 well (the shape at its pitch and level, and a faint
    copy per unison voice) over its PITCH / FINE / LEVEL, across the whole row
    until OSC 2 is added (the "+ OSC 2" chip in its corner); then OSC 1 takes
    the left half and OSC 2 fades in on the right, its power chip taking it
    away again. Each well is also where its shape is picked (a pill that steps
    and scrolls, and a click that drops open a tray of the four). Below, the UNISON well (a dot per voice, by detune and pan) over
    VOICES / DETUNE / WIDTH, and the AMP well (the envelope, with a dot riding
    it for each note that sounds) over ATTACK / RELEASE / OUTPUT. Each row of
    dials runs the spectrum, and each display draws in its dials' colours. A
    three-octave keyboard plays it. Everything is drawn natively (no WebView)
    and scales proportionally from the 480 px design.
*/
class Synth2AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit Synth2AudioProcessorEditor (Synth2AudioProcessor&);
    ~Synth2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int numDials       = 12;
    static constexpr int numOscillators = 2;

    // For the tests: the controls, in display order (the dials run OSC 1,
    // OSC 2, UNISON, AMP, three each), and each oscillator's well, picker and tray.
    juce::Slider&  getDial (int index) noexcept                { return dials[(size_t) index]; }
    WavePicker&    getWavePicker (int oscillator = 0) noexcept { return oscillator == 0 ? osc1Picker : osc2Picker; }
    WaveMenu&      getWaveMenu (int oscillator = 0) noexcept   { return waveMenus[(size_t) oscillator]; }
    WaveDisplay&   getWaveDisplay (int oscillator = 0) noexcept { return waveDisplays[(size_t) oscillator]; }
    NeonKeyboard&  getKeyboard() noexcept                      { return keyboard; }
    AddChip&       getAddOsc2Chip() noexcept                   { return addOsc2; }
    bool           isOsc2Shown() const noexcept                { return osc2Shown; }
    UnisonDisplay& getUnisonDisplay() noexcept  { return unisonDisplay; }
    AmpDisplay&    getAmpDisplay() noexcept     { return ampDisplay; }

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&, float scale) const;
    void paintDialLabels (juce::Graphics&, float scale) const;
    VoiceDisplayState displayState (Waveform wave) const;
    WaveDisplay::Osc  oscView (int oscillator) const;
    void showOsc2 (bool shown, bool fadeIn);
    AmpDisplayState   ampState() const;

    Synth2AudioProcessor& processorRef;
    Synth2LookAndFeel looks;

    std::array<WaveDisplay, numOscillators> waveDisplays;
    UnisonDisplay unisonDisplay;
    AmpDisplay    ampDisplay;
    WavePicker    osc1Picker;
    WavePicker    osc2Picker;
    std::array<WaveMenu, numOscillators> waveMenus;
    AddChip       addOsc2 { "OSC 2" };
    NeonKeyboard  keyboard;

    std::array<juce::Slider, numDials> dials;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<SliderAttachment>, numDials> attachments;

    // Painted regions computed by resized() and read back by paint().
    struct Layout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> badge;
        std::array<juce::Rectangle<int>, numDials> dialColumns;
        int groupsSeparatorY   = 0;
        int keyboardSeparatorY = 0;
    };
    Layout layout;

    float uiScale = 1.0f;
    bool  osc2Shown = false;   // OSC 2 is on, and its well and dials are in view

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2AudioProcessorEditor)
};

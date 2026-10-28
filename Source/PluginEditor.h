#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "PluginProcessor.h"
#include "Synth2LookAndFeel.h"
#include "NeonKeyboard.h"
#include "VoiceDisplays.h"
#include "WaveSelector.h"

/**
    Synth2AudioProcessorEditor — the Neon Plugins face for Synth2.

    A dark panel, top to bottom: the wordmark and POLY badge; the WAVE well
    (the shape, and a faint copy per unison voice) beside the UNISON well (a dot
    per voice, by detune and pan); the four wave chips; a row of six dials,
    VOICES / DETUNE / WIDTH under UNISON and ATTACK / RELEASE / OUTPUT under
    AMP, coloured along the spectrum; and a three-octave keyboard to play it.
    Everything is drawn natively (no WebView) and scales proportionally from
    the 480 px design.
*/
class Synth2AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit Synth2AudioProcessorEditor (Synth2AudioProcessor&);
    ~Synth2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int numDials = 6;

    // For the tests: the controls, in display order.
    juce::Slider&  getDial (int index) noexcept { return dials[(size_t) index]; }
    WaveSelector&  getWaveSelector() noexcept   { return waveSelector; }
    NeonKeyboard&  getKeyboard() noexcept       { return keyboard; }
    WaveDisplay&   getWaveDisplay() noexcept    { return waveDisplay; }
    UnisonDisplay& getUnisonDisplay() noexcept  { return unisonDisplay; }

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&, float scale) const;
    void paintDialLabels (juce::Graphics&, float scale) const;
    VoiceDisplayState displayState() const;

    Synth2AudioProcessor& processorRef;
    Synth2LookAndFeel looks;

    WaveDisplay   waveDisplay;
    UnisonDisplay unisonDisplay;
    WaveSelector  waveSelector;
    NeonKeyboard  keyboard;

    std::array<juce::Slider, numDials> dials;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<SliderAttachment>, numDials> attachments;

    // Painted regions computed by resized() and read back by paint().
    struct Layout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> badge;
        juce::Rectangle<int> unisonCaption;
        juce::Rectangle<int> ampCaption;
        std::array<juce::Rectangle<int>, numDials> dialColumns;
        int dialsSeparatorY   = 0;
        int keyboardSeparatorY = 0;
    };
    Layout layout;

    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2AudioProcessorEditor)
};

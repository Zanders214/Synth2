#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Synth2LookAndFeel

    Native JUCE drawing for the Synth2 editor's dials, after the design system's
    Dial: a recessed seat, a 270° track, a value arc in the dial's own spectrum
    colour (read from the slider's rotarySliderFillColourId) with a glow that
    grows with the value, and a filled core that swells and glows with it. The
    name and value are painted beneath each dial by the editor.

    All metrics are authored at the 480 px reference scale and multiplied by
    uiScale (set by the editor from getWidth() / 480) so the face scales cleanly.
*/
class Synth2LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Synth2LookAndFeel();

    void setUiScale (float newScale) noexcept { uiScale = juce::jmax (0.1f, newScale); }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

private:
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth2LookAndFeel)
};

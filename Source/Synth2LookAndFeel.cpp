#include "Synth2LookAndFeel.h"
#include "Synth2Theme.h"

using namespace Synth2;

Synth2LookAndFeel::Synth2LookAndFeel()
{
    // Tooltips and the slider's popup editor, in the panel's colours.
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panelTop);
    setColour (juce::TooltipWindow::textColourId,       Palette::textMid);
    setColour (juce::TooltipWindow::outlineColourId,    Palette::strongLine);
}

void Synth2LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    const float s = uiScale;
    const juce::Point centre ((float) x + (float) width * 0.5f,
                              (float) y + (float) height * 0.5f);

    const float ringRadius = juce::jmin ((float) width, (float) height) * 0.5f - 3.0f * s;
    const float ringThick  = 3.5f * s;
    const float amount     = juce::jlimit (0.0f, 1.0f, sliderPosProportional);
    const float curAngle   = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * amount;

    const juce::Colour accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // --- Recessed seat behind the arc (Dial's rgba(8,11,18,0.6)) -------------
    g.setColour (Palette::dialSeat);
    g.fillEllipse (juce::Rectangle<float> (ringRadius * 2.0f, ringRadius * 2.0f).withCentre (centre));

    // --- Track arc (full sweep) ---------------------------------------------
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (Palette::ringTrack);
        g.strokePath (track, juce::PathStrokeType (ringThick, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // --- Value arc in the dial's colour, with a glow that scales with value --
    if (amount > 0.0005f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                             rotaryStartAngle, curAngle, true);
        const float glow = (3.0f + amount * 6.0f) * s;   // ~ drop-shadow(0 0 3+v*6)
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (ringThick + glow, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (ringThick, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // --- Filled core that swells + glows with value ---------------------------
    {
        const float coreRadius = (4.0f + amount * 5.0f) * s;   // Dial: r = 4 + value*5
        const auto  coreBounds = juce::Rectangle<float> (coreRadius * 2.0f, coreRadius * 2.0f).withCentre (centre);
        juce::Path corePath;
        corePath.addEllipse (coreBounds);
        juce::DropShadow (accent.withAlpha (0.9f), (int) (6.0f * s), {}).drawForPath (g, corePath);
        g.setColour (accent);
        g.fillEllipse (coreBounds);
    }
}

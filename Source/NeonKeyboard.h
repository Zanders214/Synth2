#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Synth2Theme.h"

/**
    NeonKeyboard — the on-screen keyboard along the bottom of the panel, after
    the design system's Keyboard: ivory keys with rounded bottoms and a soft
    shadow at their tips, near-black sharps, and a pressed key that tints violet
    and glows, whether it was clicked or played from the host.

    Three octaves, C3 to C6, always fitted to the component's width.
*/
class NeonKeyboard final : public juce::MidiKeyboardComponent
{
public:
    static constexpr int lowestNote  = 48;   // C3
    static constexpr int highestNote = 84;   // C6
    static constexpr int numWhiteKeys = 22;

    explicit NeonKeyboard (juce::MidiKeyboardState& state)
        : MidiKeyboardComponent (state, horizontalKeyboard)
    {
        setAvailableRange (lowestNote, highestNote);
        setScrollButtonsVisible (false);
        setVelocity (0.8f, false);
        setBlackNoteLengthProportion (0.61f);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);

        // The background (drawn by JUCE, in the white-key colour) shows in the
        // gaps between the keys: make it the panel's, with no shadow over it.
        setColour (whiteNoteColourId, Synth2::Palette::panelBottom);
        setColour (shadowColourId, juce::Colours::transparentBlack);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    void resized() override
    {
        setKeyWidth ((float) getWidth() / (float) numWhiteKeys);
        setLowestVisibleKey (lowestNote);
        MidiKeyboardComponent::resized();
    }

    void drawWhiteNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override
    {
        using namespace Synth2;
        const float s = uiScale;
        auto key = area.reduced (0.5f * s, 0.0f);
        if (isDown)
            key = key.withTrimmedTop (2.0f * s);   // the 2 px press

        juce::Path shape;
        shape.addRoundedRectangle (key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                   4.0f * s, 4.0f * s, false, false, true, true);

        g.setColour (isDown ? Palette::keyWhiteDown : (isOver ? Palette::keyWhite.darker (0.04f) : Palette::keyWhite));
        g.fillPath (shape);

        // inset 0 -7px 9px -7px rgba(0,0,0,.22): a soft shade at the key's tip.
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, key.getBottom() - 8.0f * s,
                                                 juce::Colours::black.withAlpha (0.22f), 0.0f, key.getBottom(), false));
        g.fillPath (shape);

        if (isDown)
        {
            g.setColour (Palette::violet.withAlpha (0.55f));
            g.strokePath (shape, juce::PathStrokeType (2.0f * s));
        }
    }

    void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override
    {
        using namespace Synth2;
        const float s = uiScale;
        auto key = area.reduced (1.0f * s, 0.0f);
        if (isDown)
            key = key.withTrimmedTop (2.0f * s);

        juce::Path shape;
        shape.addRoundedRectangle (key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                   3.0f * s, 3.0f * s, false, false, true, true);

        if (isDown)
            juce::DropShadow (Palette::violet.withAlpha (0.8f), (int) (10.0f * s), {}).drawForPath (g, shape);
        else
            juce::DropShadow (juce::Colours::black.withAlpha (0.4f), (int) (4.0f * s), { 0, (int) (3.0f * s) }).drawForPath (g, shape);

        g.setColour (isDown ? Palette::keyBlackDown : (isOver ? Palette::keyBlack.brighter (0.15f) : Palette::keyBlack));
        g.fillPath (shape);

        // inset 0 -3px 5px rgba(255,255,255,.07): a faint lip at the tip.
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, 0.0f, key.getBottom() - 5.0f * s,
                                                 juce::Colours::white.withAlpha (0.07f), 0.0f, key.getBottom(), false));
        g.fillPath (shape);
    }

private:
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonKeyboard)
};

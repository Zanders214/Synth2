#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "Synth2Theme.h"
#include "Synth2VisualModel.h"

/**
    WaveSelector — the row of four wave chips under the WAVE well: SINE, TRI,
    SAW and SQUARE, each with its shape drawn small beside the name. Click one to
    choose it. The chosen chip fills with the accent gradient and glows, like a
    selected preset chip in the design system; the rest sit on the white-alpha
    fill.

    It is tied to the WAVE choice parameter directly (ParameterAttachment), so
    host automation and undo see each click as one gesture.
*/
class WaveSelector final : public juce::Component
{
public:
    static constexpr int numWaves = 4;

    explicit WaveSelector (juce::RangedAudioParameter& waveParameter)
        : attachment (waveParameter, [this] (float value) { setSelected (juce::roundToInt (value)); })
    {
        setPaintingIsUnclipped (true);   // the chosen chip's glow reaches past the row
        setRepaintsOnMouseActivity (true);
        attachment.sendInitialUpdate();
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    int getSelected() const noexcept { return selected; }

    /** The chip under x (in this component's space). */
    int chipAt (float x) const noexcept
    {
        return juce::jlimit (0, numWaves - 1, (int) (x / chipPitch()));
    }

    juce::Rectangle<float> chipBounds (int index) const noexcept
    {
        const float gap = 6.0f * uiScale;
        return { (float) index * chipPitch(), 0.0f, chipPitch() - gap, (float) getHeight() };
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        attachment.setValueAsCompleteGesture ((float) chipAt (e.position.x));
    }

    void paint (juce::Graphics& g) override
    {
        static constexpr std::array<const char*, numWaves> names { "SINE", "TRI", "SAW", "SQUARE" };
        const float s = uiScale;
        const auto  hover = isMouseOver() ? chipAt (getMouseXYRelative().toFloat().x) : -1;

        for (int i = 0; i < numWaves; ++i)
        {
            const auto chip   = chipBounds (i);
            const bool chosen = i == selected;
            const float radius = 8.0f * s;

            if (chosen)
            {
                juce::Path shape;
                shape.addRoundedRectangle (chip, radius);
                juce::DropShadow (Synth2::Palette::accent.withAlpha (0.45f), (int) (16.0f * s), {}).drawForPath (g, shape);
                g.setGradientFill (juce::ColourGradient (Synth2::Palette::accent, chip.getX(), chip.getY(),
                                                         Synth2::Palette::violet, chip.getX(), chip.getBottom(), false));
                g.fillRoundedRectangle (chip, radius);
                g.setColour (juce::Colour::fromFloatRGBA (0.59f, 0.67f, 1.0f, 0.6f));
                g.drawRoundedRectangle (chip.reduced (0.5f), radius, juce::jmax (1.0f, s));
            }
            else
            {
                g.setColour (i == hover ? Synth2::Palette::idleFill.withMultipliedAlpha (1.6f) : Synth2::Palette::idleFill);
                g.fillRoundedRectangle (chip, radius);
                g.setColour (Synth2::Palette::strongLine);
                g.drawRoundedRectangle (chip.reduced (0.5f), radius, juce::jmax (1.0f, s));
            }

            // The shape, a single 1.5 px stroke, then the name.
            const auto content = chip.reduced (10.0f * s, 0.0f);
            const auto glyph   = content.withWidth (22.0f * s).withSizeKeepingCentre (22.0f * s, 10.0f * s);
            juce::Path wave;
            constexpr int points = 32;
            for (int p = 0; p <= points; ++p)
            {
                const float x01 = (float) p / (float) points;
                const float v   = Oscillator::naive ((Waveform) i, (double) x01);
                const juce::Point pt (glyph.getX() + x01 * glyph.getWidth(), glyph.getCentreY() - v * glyph.getHeight() * 0.5f);
                if (p == 0) wave.startNewSubPath (pt);
                else        wave.lineTo (pt);
            }
            g.setColour (chosen ? juce::Colours::white : Synth2::Palette::textDim);
            g.strokePath (wave, juce::PathStrokeType (1.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (Synth2::Fonts::sans (10.0f * s, chosen, 0.14f));
            g.setColour (chosen ? juce::Colours::white : Synth2::Palette::textMid);
            g.drawText (names[(size_t) i], content.withTrimmedLeft (glyph.getWidth() + 8.0f * s),
                        juce::Justification::centredLeft, false);
        }
    }

private:
    float chipPitch() const noexcept
    {
        const float gap = 6.0f * uiScale;
        return ((float) getWidth() + gap) / (float) numWaves;
    }

    void setSelected (int index)
    {
        selected = juce::jlimit (0, numWaves - 1, index);
        repaint();
    }

    juce::ParameterAttachment attachment;
    int   selected = 0;
    float uiScale  = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSelector)
};

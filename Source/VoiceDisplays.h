#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Synth2Theme.h"
#include "Synth2VisualModel.h"

/** What the two displays show: the sound's wave and its unison stack. */
struct VoiceDisplayState
{
    Waveform wave   = Waveform::saw;
    int      voices = 1;
    float    detune = 0.0f;   // cents
    float    width  = 0.0f;   // 0..1

    bool operator== (const VoiceDisplayState& other) const noexcept
    {
        return wave == other.wave && voices == other.voices
            && juce::exactlyEqual (detune, other.detune) && juce::exactlyEqual (width, other.width);
    }
    bool operator!= (const VoiceDisplayState& other) const noexcept { return ! (*this == other); }
};

/**
    DisplayWell — the inset, near-black window both displays sit in, with a
    small all-caps caption at its top left and a mono readout at its top right.
*/
class DisplayWell : public juce::Component
{
public:
    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    void setState (const VoiceDisplayState& newState)
    {
        if (newState == state)
            return;
        state = newState;
        repaint();
    }

    const VoiceDisplayState& getState() const noexcept { return state; }

protected:
    /** Paints the well and its captions, and returns the area left for the plot. */
    juce::Rectangle<float> paintWell (juce::Graphics& g, const juce::String& caption, const juce::String& readout) const
    {
        const float s      = uiScale;
        const auto  bounds = getLocalBounds().toFloat();
        const float radius = 12.0f * s;

        g.setColour (Synth2::Palette::well);
        g.fillRoundedRectangle (bounds, radius);

        // Inset shadow: darker at the top edge, fading down into the well.
        juce::ColourGradient inset (juce::Colours::black.withAlpha (0.55f), 0.0f, bounds.getY(),
                                    juce::Colours::transparentBlack, 0.0f, bounds.getY() + 10.0f * s, false);
        g.setGradientFill (inset);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (Synth2::Palette::hairline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, juce::jmax (1.0f, s));

        auto inner   = bounds.reduced (12.0f * s, 9.0f * s);
        auto heading = inner.removeFromTop (12.0f * s);

        g.setFont (Synth2::Fonts::sans (9.0f * s, true, 0.22f));
        g.setColour (Synth2::Palette::textFaint);
        g.drawText (caption, heading, juce::Justification::centredLeft, false);

        g.setFont (Synth2::Fonts::mono (9.0f * s, 0.10f));
        g.setColour (Synth2::Palette::textDim);
        g.drawText (readout, heading, juce::Justification::centredRight, false);

        return inner.withTrimmedTop (4.0f * s);
    }

    float uiScale = 1.0f;
    VoiceDisplayState state;
};

/**
    WaveDisplay — the WAVE well: two cycles of the chosen shape in the spectrum
    gradient, glowing, over a faint copy per unison voice, each drawn shifted by
    its detune (a picture of the stack spreading out, see Synth2VisualModel).
*/
class WaveDisplay final : public DisplayWell
{
public:
    WaveDisplay() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        static const juce::StringArray names { "SINE", "TRIANGLE", "SAW", "SQUARE" };
        const float s    = uiScale;
        const auto  plot = paintWell (g, "WAVE", names[(int) state.wave]);

        // Centre line.
        g.setColour (Synth2::Palette::gridLine);
        g.fillRect (juce::Rectangle (plot.getX(), plot.getCentreY(), plot.getWidth(), juce::jmax (1.0f, s)));

        const auto pathFor = [&] (float phaseOffset)
        {
            juce::Path path;
            const int points = juce::jmax (64, (int) plot.getWidth());
            for (int p = 0; p <= points; ++p)
            {
                const float x01 = (float) p / (float) points;
                const float v   = Synth2VisualModel::waveAt (state.wave, x01, phaseOffset);
                const juce::Point pt (plot.getX() + x01 * plot.getWidth(),
                                      plot.getCentreY() - v * plot.getHeight() * 0.42f);
                if (p == 0) path.startNewSubPath (pt);
                else        path.lineTo (pt);
            }
            return path;
        };

        const juce::PathStrokeType thin (1.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType main (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // One faint copy per unison voice, tinted flat (cyan) to sharp (amber).
        if (state.voices > 1)
        {
            for (int i = 0; i < state.voices; ++i)
            {
                const auto m = Synth2VisualModel::marker (i, state.voices, state.detune, state.width);
                g.setColour (Synth2::ColourMath::spectrum (m.tint).withAlpha (0.30f));
                g.strokePath (pathFor (Synth2VisualModel::voicePhaseOffset (i, state.voices, state.detune)), thin);
            }
        }

        // The shape itself: a soft glow under a spectrum-gradient stroke.
        const auto shape = pathFor (0.0f);
        juce::ColourGradient ramp (Synth2::Palette::cyan, plot.getX(), 0.0f, Synth2::Palette::amber, plot.getRight(), 0.0f, false);
        ramp.addColour (1.0 / 3.0, Synth2::Palette::violet);
        ramp.addColour (2.0 / 3.0, Synth2::Palette::pink);

        g.setGradientFill (ramp);
        g.setOpacity (0.22f);
        g.strokePath (shape, juce::PathStrokeType (7.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setOpacity (1.0f);
        g.setGradientFill (ramp);
        g.strokePath (shape, main);
    }
};

/**
    UnisonDisplay — the UNISON well: a glowing dot per voice, placed left to
    right by its detune (on a fixed -50..+50 cent scale) and top to bottom by
    where it sits in the stereo field (L at the top, R at the bottom), each
    tinted flat (cyan) to sharp (amber). With one voice it is a single dot in
    the centre; DETUNE fans the dots out sideways and WIDTH up and down.
*/
class UnisonDisplay final : public DisplayWell
{
public:
    UnisonDisplay() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const float s = uiScale;
        const auto readout = state.voices == 1 ? juce::String ("1 VOICE")
                                               : juce::String (state.voices) + juce::String (juce::CharPointer_UTF8 (" \xc3\x97 ")) + Synth2::Format::cents (state.detune);
        auto plot = paintWell (g, "UNISON", readout);

        // Tick labels along the bottom, pan labels down the left.
        auto ticks = plot.removeFromBottom (10.0f * s);
        auto lanes = plot.removeFromLeft (10.0f * s);
        g.setFont (Synth2::Fonts::mono (8.5f * s));
        g.setColour (Synth2::Palette::textFaint);
        g.drawText ("-50", ticks, juce::Justification::centredLeft, false);
        g.drawText ("0",   ticks, juce::Justification::centred, false);
        g.drawText ("+50", ticks, juce::Justification::centredRight, false);
        g.drawText ("L", lanes.removeFromTop (10.0f * s), juce::Justification::centredLeft, false);
        g.drawText ("R", lanes.removeFromBottom (10.0f * s), juce::Justification::centredLeft, false);

        plot = plot.reduced (4.0f * s, 4.0f * s);

        // Grid: the in-tune line and the centre of the stereo field.
        g.setColour (Synth2::Palette::gridLine);
        const float line = juce::jmax (1.0f, s);
        g.fillRect (juce::Rectangle (plot.getCentreX() - line * 0.5f, plot.getY(), line, plot.getHeight()));
        g.fillRect (juce::Rectangle (plot.getX(), plot.getCentreY() - line * 0.5f, plot.getWidth(), line));
        for (const float x : { 0.25f, 0.75f })
            g.fillRect (juce::Rectangle (plot.getX() + x * plot.getWidth() - line * 0.5f, plot.getCentreY() - 3.0f * s, line, 6.0f * s));

        const float dot = 3.5f * s;
        for (int i = 0; i < state.voices; ++i)
        {
            const auto m      = Synth2VisualModel::marker (i, state.voices, state.detune, state.width);
            const auto colour = Synth2::ColourMath::spectrum (m.tint);
            const juce::Point centre (plot.getX() + m.x * plot.getWidth(),
                                      plot.getCentreY() + m.pan * plot.getHeight() * 0.5f);

            // A faint stem back to the in-tune line shows how far it is detuned.
            g.setColour (colour.withAlpha (0.25f));
            g.drawLine (plot.getCentreX(), centre.y, centre.x, centre.y, juce::jmax (1.0f, s));

            juce::Path body;
            body.addEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (centre));
            juce::DropShadow (colour.withAlpha (0.9f), (int) (7.0f * s), {}).drawForPath (g, body);
            g.setColour (colour);
            g.fillPath (body);
        }
    }
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

#include "Synth2Theme.h"
#include "Synth2VisualModel.h"
#include "SynthVoice.h"

/** What the two displays show: the sound's wave and its unison stack. */
struct VoiceDisplayState
{
    Waveform wave   = Waveform::saw;
    int      voices = 1;
    float    detune = 0.0f;   // cents
    float    width  = 0.0f;   // 0..1

    friend bool operator== (const VoiceDisplayState& a, const VoiceDisplayState& b) noexcept
    {
        return a.wave == b.wave && a.voices == b.voices
            && juce::exactlyEqual (a.detune, b.detune) && juce::exactlyEqual (a.width, b.width);
    }
    friend bool operator!= (const VoiceDisplayState& a, const VoiceDisplayState& b) noexcept { return ! (a == b); }
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
        if (newState == displayState && hasState)
            return;
        displayState = newState;
        hasState     = true;
        stateChanged();
        repaint();
    }

    const VoiceDisplayState& getState() const noexcept { return displayState; }

protected:
    float getUiScale() const noexcept { return uiScale; }

    /** Called when setState() brings something new, before the repaint. */
    virtual void stateChanged()
    {
        // Nothing by default: only a well that animates between states needs to know.
    }

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

private:
    float uiScale = 1.0f;
    VoiceDisplayState displayState;
    bool  hasState = false;
};

/**
    WaveDisplay — an oscillator's well (OSC 1 or OSC 2): two cycles of its
    shape in the spectrum gradient, glowing, over a faint copy per unison
    voice, each drawn shifted by its detune (a picture of the stack spreading
    out, see Synth2VisualModel). Its PITCH squeezes or stretches the cycles
    (twice as many an octave up) and its LEVEL sets their height.

    A new shape doesn't jump in: the old one morphs into it over 150 ms. While
    the wave menu is open, hovering a shape previews it here the same way.
*/
class WaveDisplay final : public DisplayWell,
                          private juce::Timer
{
public:
    WaveDisplay() { setInterceptsMouseClicks (false, false); }

    /** The oscillator as the well draws it. */
    struct Osc
    {
        float cycles = Synth2VisualModel::cyclesShown;   // from its PITCH and FINE
        float level  = 1.0f;                              // its LEVEL, 0..1

        friend bool operator== (const Osc& a, const Osc& b) noexcept
        {
            return juce::exactlyEqual (a.cycles, b.cycles) && juce::exactlyEqual (a.level, b.level);
        }
        friend bool operator!= (const Osc& a, const Osc& b) noexcept { return ! (a == b); }
    };

    void setOsc (const Osc& newOsc)
    {
        if (newOsc == osc)
            return;
        osc = newOsc;
        repaint();
    }

    const Osc& getOsc() const noexcept { return osc; }

    /** The caption at the well's top left ("WAVE"); empty for none. */
    void setCaption (const juce::String& newCaption) { caption = newCaption; repaint(); }

    /** Shows `wave` (0..3) in place of the chosen one, or the chosen one
        again for -1. */
    void setPreview (int wave)
    {
        preview = wave;
        retarget();
    }

    /** The shape the well shows, or is morphing to. */
    Waveform getShownWave() const noexcept { return morphTo; }
    bool isMorphing() const noexcept { return morphProgress() < 1.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto& state = getState();
        const float s    = getUiScale();
        const auto  plot = paintWell (g, preview >= 0 && caption.isNotEmpty() ? caption + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  PREVIEW")) : caption, {});
        const float amount = Synth2VisualModel::ease (morphProgress());

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
                const float v   = Synth2VisualModel::morphAt (morphFrom, morphTo, amount, x01, phaseOffset, osc.cycles);
                const juce::Point pt (plot.getX() + x01 * plot.getWidth(),
                                      plot.getCentreY() - v * osc.level * plot.getHeight() * 0.42f);
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

private:
    void stateChanged() override { retarget(); }

    // Starts a morph to whatever the well should show now, from what it
    // showed last (a change mid-morph starts afresh from the last target).
    void retarget()
    {
        const auto target = preview >= 0 ? (Waveform) preview : getState().wave;
        if (! hasShape)
        {
            // The first shape it is given simply appears.
            morphFrom = target;
            morphTo   = target;
            hasShape  = true;
            repaint();
            return;
        }
        if (target == morphTo)
            return;
        morphFrom  = morphTo;
        morphTo    = target;
        morphStart = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
        repaint();
    }

    float morphProgress() const noexcept
    {
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - morphStart;
        return Synth2VisualModel::clamp01 ((float) (elapsed / (1000.0 * Synth2VisualModel::morphSeconds)));
    }

    void timerCallback() override
    {
        repaint();
        if (! isMorphing())
            stopTimer();
    }

    Osc          osc;
    juce::String caption { "OSC 1" };
    int      preview    = -1;
    bool     hasShape   = false;
    Waveform morphFrom  = Waveform::saw;
    Waveform morphTo    = Waveform::saw;
    double   morphStart = 0.0;
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
        const auto& state = getState();
        const float s = getUiScale();
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

/** What the AMP well shows: the envelope's settings and where each note is in it. */
struct AmpDisplayState
{
    float attackPosition  = 0.0f;   // the ATTACK dial's position, 0..1
    float releasePosition = 0.0f;   // the RELEASE dial's position, 0..1
    float attackSeconds   = 0.0f;
    float releaseSeconds  = 0.0f;
    float outputDb        = 0.0f;
    std::array<NoteActivity, 16> notes {};

    friend bool operator== (const AmpDisplayState& a, const AmpDisplayState& b) noexcept
    {
        const auto same = [] (float x, float y) { return juce::exactlyEqual (x, y); };
        if (! (same (a.attackPosition, b.attackPosition) && same (a.releasePosition, b.releasePosition)
               && same (a.attackSeconds, b.attackSeconds) && same (a.releaseSeconds, b.releaseSeconds)
               && same (a.outputDb, b.outputDb)))
            return false;

        for (size_t i = 0; i < a.notes.size(); ++i)
            if (a.notes[i].stage != b.notes[i].stage || ! same (a.notes[i].level, b.notes[i].level)
                || ! same (a.notes[i].releaseFrom, b.notes[i].releaseFrom))
                return false;
        return true;
    }
    friend bool operator!= (const AmpDisplayState& a, const AmpDisplayState& b) noexcept { return ! (a == b); }

    int numSounding() const noexcept
    {
        int count = 0;
        for (const auto& note : notes)
            count += note.stage != NoteActivity::Stage::idle ? 1 : 0;
        return count;
    }
};

/**
    AmpDisplay — the AMP well: the envelope every note follows, drawn in the
    colours of the dials that shape it (the rise in ATTACK's, the top in
    OUTPUT's, whose level sets its height, the fall in RELEASE's), over a
    faint 0 dB line. Each note that sounds is a glowing dot riding it: up the
    attack, resting on the top while its key (or the pedal) is down, then down
    the release once it lets go.
*/
class AmpDisplay final : public DisplayWell
{
public:
    AmpDisplay() { setInterceptsMouseClicks (false, false); }

    /** The dials' colours, so each part of the envelope matches its dial. */
    void setColours (juce::Colour attack, juce::Colour release, juce::Colour output)
    {
        attackColour  = attack;
        releaseColour = release;
        outputColour  = output;
        repaint();
    }

    void setAmpState (const AmpDisplayState& newState)
    {
        if (newState == amp)
            return;
        amp = newState;
        repaint();
    }

    const AmpDisplayState& getAmpState() const noexcept { return amp; }

    void paint (juce::Graphics& g) override
    {
        const float s = getUiScale();
        auto plot = paintWell (g, "AMP", Synth2::Format::decibels (amp.outputDb));

        // The times along the bottom: attack on the left, release on the right.
        auto times = plot.removeFromBottom (10.0f * s);
        g.setFont (Synth2::Fonts::mono (8.5f * s));
        g.setColour (attackColour.withAlpha (0.85f));
        g.drawText ("A " + Synth2::Format::seconds (amp.attackSeconds), times, juce::Justification::centredLeft, false);
        g.setColour (releaseColour.withAlpha (0.85f));
        g.drawText ("R " + Synth2::Format::seconds (amp.releaseSeconds), times, juce::Justification::centredRight, false);

        plot = plot.reduced (2.0f * s, 4.0f * s);
        const auto e  = Synth2VisualModel::envelope (amp.attackPosition, amp.releasePosition, amp.outputDb);
        const auto at = [&plot] (float x, float y)
        {
            return juce::Point (plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight());
        };

        // Baseline and the 0 dB line.
        const float line = juce::jmax (1.0f, s);
        g.setColour (Synth2::Palette::gridLine);
        g.fillRect (juce::Rectangle (plot.getX(), plot.getBottom() - line, plot.getWidth(), line));
        const float zeroDb = at (0.0f, Synth2VisualModel::heightForDb (0.0f)).y;
        const auto dashes = (int) std::ceil (plot.getWidth() / (6.0f * s));
        for (int i = 0; i < dashes; ++i)
            g.fillRect (juce::Rectangle (plot.getX() + (float) i * 6.0f * s, zeroDb - line * 0.5f, 3.0f * s, line));

        // The shape, softly filled underneath.
        juce::Path shape;
        shape.startNewSubPath (at (0.0f, 0.0f));
        shape.lineTo (at (e.attackEnd, e.peak));
        shape.lineTo (at (e.holdEnd, e.peak));
        shape.lineTo (at (e.releaseEnd, 0.0f));
        juce::Path fill (shape);
        fill.lineTo (at (0.0f, 0.0f));
        fill.closeSubPath();
        juce::ColourGradient wash (outputColour.withAlpha (0.18f), 0.0f, at (0.0f, e.peak).y,
                                   outputColour.withAlpha (0.0f), 0.0f, plot.getBottom(), false);
        g.setGradientFill (wash);
        g.fillPath (fill);

        // Each part in its dial's colour, with a glow under it.
        const auto stroke = [&] (juce::Point<float> from, juce::Point<float> to, juce::Colour colour)
        {
            juce::Path part;
            part.startNewSubPath (from);
            part.lineTo (to);
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (part, juce::PathStrokeType (6.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (part, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        stroke (at (0.0f, 0.0f),           at (e.attackEnd, e.peak),   attackColour);
        stroke (at (e.attackEnd, e.peak),  at (e.holdEnd, e.peak),     outputColour);
        stroke (at (e.holdEnd, e.peak),    at (e.releaseEnd, 0.0f),    releaseColour);

        // A dot per sounding note, in the colour of the part it is on.
        const float dot = 3.5f * s;
        for (const auto& note : amp.notes)
        {
            if (note.stage == NoteActivity::Stage::idle)
                continue;

            const auto d = Synth2VisualModel::noteDot (e, (int) note.stage, note.level, note.releaseFrom);
            const auto colour = colourFor (note.stage);
            juce::Path body;
            body.addEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (at (d.x, d.y)));
            juce::DropShadow (colour.withAlpha (0.9f), (int) (8.0f * s), {}).drawForPath (g, body);
            g.setColour (juce::Colours::white);
            g.fillPath (body);
        }
    }

private:
    /** The colour of the part of the envelope a note in `stage` is on. */
    juce::Colour colourFor (NoteActivity::Stage stage) const noexcept
    {
        switch (stage)
        {
            case NoteActivity::Stage::attack:  return attackColour;
            case NoteActivity::Stage::release: return releaseColour;
            case NoteActivity::Stage::idle:
            case NoteActivity::Stage::hold:    break;
        }
        return outputColour;
    }

    AmpDisplayState amp;
    juce::Colour attackColour  { Synth2::Palette::pink };
    juce::Colour releaseColour { Synth2::Palette::pink };
    juce::Colour outputColour  { Synth2::Palette::amber };
};

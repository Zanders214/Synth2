#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <optional>
#include <vector>

#include "Synth2Theme.h"
#include "Synth2VisualModel.h"
#include "SynthVoice.h"

/** What an oscillator's WAVE well shows: its shape and its unison stack. */
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
    DisplayWell — the inset, near-black window every display sits in, with a
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

    /** The area paintWell leaves for the plot, without painting. */
    juce::Rectangle<float> plotBounds() const noexcept
    {
        const float s = uiScale;
        return getLocalBounds().toFloat().reduced (12.0f * s, 9.0f * s).withTrimmedTop (16.0f * s);
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

/** What the AMP well shows: the envelope's settings and where each note is in it. */
struct AmpDisplayState
{
    Synth2VisualModel::EnvelopeDials dials;   // the dials' positions (and SUSTAIN's level)
    float attackSeconds  = 0.0f;
    float releaseSeconds = 0.0f;
    float outputDb       = 0.0f;
    std::array<NoteActivity, 16> notes {};

    friend bool operator== (const AmpDisplayState& a, const AmpDisplayState& b) noexcept
    {
        const auto same = [] (float x, float y) { return juce::exactlyEqual (x, y); };
        const auto& x = a.dials;
        const auto& y = b.dials;
        if (! (same (x.delay, y.delay) && same (x.attack, y.attack) && same (x.hold, y.hold) && same (x.decay, y.decay)
               && same (x.sustain, y.sustain) && same (x.release, y.release)
               && same (a.attackSeconds, b.attackSeconds) && same (a.releaseSeconds, b.releaseSeconds)
               && same (a.outputDb, b.outputDb)))
            return false;

        for (size_t i = 0; i < a.notes.size(); ++i)
            if (a.notes[i].stage != b.notes[i].stage || ! same (a.notes[i].level, b.notes[i].level)
                || ! same (a.notes[i].progress, b.notes[i].progress))
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
    AmpDisplay — the AMP well: the envelope its oscillator's notes follow,
    each stage drawn in the colour of the dial that shapes it (the wait in
    DELAY's, the rise in ATTACK's, the top in HOLD's, the fall in DECAY's, the
    sustain in SUSTAIN's and the release in RELEASE's), its top at OUTPUT's
    level over a faint 0 dB line. Each note that sounds is a glowing dot
    riding it through the stages, resting on the sustain while its key (or
    the pedal) is down.

    Given its envelope's parameters (setParameters), the shape can be edited
    in place: each corner is a handle (see Synth2VisualModel::Handle) to drag
    sideways for its stage's time, DECAY's corner up and down for SUSTAIN
    too. Shift drags finely, a double-click puts a corner's stage back to
    its default, and the readout names the stage and value under the
    pointer. Each drag is one gesture, so a host records and undoes it like a
    turn of the dial.
*/
class AmpDisplay final : public DisplayWell,
                         public juce::SettableTooltipClient
{
public:
    using Handle = Synth2VisualModel::Handle;

    /** DELAY, ATTACK, HOLD, DECAY, SUSTAIN and RELEASE, in that order. */
    using Parameters = std::array<juce::RangedAudioParameter*, 6>;

    AmpDisplay() { setInterceptsMouseClicks (false, false); }

    void setParameters (const Parameters& newParameters)
    {
        parameters = newParameters;
        setInterceptsMouseClicks (true, false);
        setTooltip ("Drag a corner to shape the envelope (Shift for fine moves); double-click one to reset it");
    }

    /** The plot the envelope is drawn in. */
    juce::Rectangle<float> plotArea() const noexcept
    {
        const float s = getUiScale();
        return plotBounds().withTrimmedBottom (10.0f * s).reduced (2.0f * s, 4.0f * s);
    }

    /** Where handle `h` is drawn, in this component. */
    juce::Point<float> handleCentre (Handle h) const noexcept
    {
        const auto p = Synth2VisualModel::handlePosition (shape(), h);
        return toPlot (p.x, p.y);
    }

    /** The handle under the pointer, or being dragged (-1 for none). */
    int getHotHandle() const noexcept { return drag.has_value() ? (int) drag->handle : hot; }

    void mouseMove (const juce::MouseEvent& e) override { setHot (handleAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override   { setHot (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = handleAt (e.position);
        setHot (h);
        if (h < 0)
            return;
        drag = Drag { (Handle) h, e.position, amp.dials };
        for (auto* p : parametersFor (drag->handle))
            p->beginChangeGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.has_value())
            return;
        const auto  plot   = plotArea();
        const float fine   = e.mods.isShiftDown() ? 0.25f : 1.0f;
        const float width  = juce::jmax (1.0f, plot.getWidth());
        const float height = juce::jmax (1.0f, plot.getHeight());
        const float dx     = fine * (e.position.x - drag->from.x) / width;
        auto dials = drag->dials;
        const auto time = [&dx, h = drag->handle] (float& dial) { dial = Synth2VisualModel::dragDial (dial, dx, h); };
        switch (drag->handle)
        {
            case Handle::delay:   time (dials.delay);   break;
            case Handle::attack:  time (dials.attack);  break;
            case Handle::hold:    time (dials.hold);    break;
            case Handle::release: time (dials.release); break;
            case Handle::decay:
            {
                time (dials.decay);
                const auto start = Synth2VisualModel::envelope (drag->dials, amp.outputDb);
                const float dy   = fine * (drag->from.y - e.position.y) / height;
                dials.sustain = Synth2VisualModel::sustainAt (start, start.sustain + dy, drag->dials.sustain);
                break;
            }
        }
        apply (dials);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! drag.has_value())
            return;
        endDrag();
        setHot (handleAt (e.position));
        repaint();
    }

    // JUCE sends a double-click after the second click's mouseUp, which has
    // already ended that click's gesture; should a drag still be open (from
    // another event order), it is closed first, so gestures never nest.
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = handleAt (e.position);
        if (h < 0)
            return;
        if (drag.has_value())
            endDrag();
        for (auto* p : parametersFor ((Handle) h))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }

    /** The dials' colours (DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE), so
        each stage of the envelope matches its dial. */
    void setColours (const std::array<juce::Colour, 6>& dialColours)
    {
        colours = dialColours;
        repaint();
    }

    void setAmpState (const AmpDisplayState& newState)
    {
        // While a corner is dragged its dials are already where they are
        // going: only the notes and the readouts come from outside.
        auto next = newState;
        if (drag.has_value())
            next.dials = amp.dials;
        if (next == amp)
            return;
        amp = next;
        repaint();
    }

    const AmpDisplayState& getAmpState() const noexcept { return amp; }

    void paint (juce::Graphics& g) override
    {
        const float s = getUiScale();
        auto plot = paintWell (g, "AMP", readout());

        // The times along the bottom: attack on the left, release on the right.
        auto times = plot.removeFromBottom (10.0f * s);
        g.setFont (Synth2::Fonts::mono (8.5f * s));
        g.setColour (colourFor (Stage::attack).withAlpha (0.85f));
        g.drawText ("A " + Synth2::Format::seconds (amp.attackSeconds), times, juce::Justification::centredLeft, false);
        g.setColour (colourFor (Stage::release).withAlpha (0.85f));
        g.drawText ("R " + Synth2::Format::seconds (amp.releaseSeconds), times, juce::Justification::centredRight, false);

        plot = plot.reduced (2.0f * s, 4.0f * s);
        const auto e  = shape();
        const auto at = [this] (float x, float y) { return toPlot (x, y); };

        // Baseline and the 0 dB line.
        const float line = juce::jmax (1.0f, s);
        g.setColour (Synth2::Palette::gridLine);
        g.fillRect (juce::Rectangle (plot.getX(), plot.getBottom() - line, plot.getWidth(), line));
        const float zeroDb = at (0.0f, Synth2VisualModel::heightForDb (0.0f)).y;
        const auto dashes = (int) std::ceil (plot.getWidth() / (6.0f * s));
        for (int i = 0; i < dashes; ++i)
            g.fillRect (juce::Rectangle (plot.getX() + (float) i * 6.0f * s, zeroDb - line * 0.5f, 3.0f * s, line));

        // The shape, softly filled underneath.
        const std::array<std::pair<juce::Point<float>, Stage>, 7> corners { {
            { at (0.0f, 0.0f), Stage::idle },
            { at (e.delayEnd, 0.0f), Stage::delay },
            { at (e.attackEnd, e.peak), Stage::attack },
            { at (e.holdEnd, e.peak), Stage::hold },
            { at (e.decayEnd, e.sustain), Stage::decay },
            { at (e.sustainEnd, e.sustain), Stage::sustain },
            { at (e.releaseEnd, 0.0f), Stage::release } } };
        juce::Path fill;
        fill.startNewSubPath (corners.front().first);
        for (const auto& corner : corners)
            fill.lineTo (corner.first);
        fill.closeSubPath();
        const auto washColour = colourFor (Stage::sustain);
        juce::ColourGradient wash (washColour.withAlpha (0.18f), 0.0f, at (0.0f, e.peak).y,
                                   washColour.withAlpha (0.0f), 0.0f, plot.getBottom(), false);
        g.setGradientFill (wash);
        g.fillPath (fill);

        // Each stage in its dial's colour, with a glow under it (a stage with
        // no length, a delay or hold of 0, isn't drawn).
        for (size_t i = 1; i < corners.size(); ++i)
        {
            const auto from = corners[i - 1].first;
            const auto to   = corners[i].first;
            if (from.getDistanceFrom (to) < 0.5f)
                continue;
            juce::Path part;
            part.startNewSubPath (from);
            part.lineTo (to);
            const auto colour = colourFor (corners[i].second);
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (part, juce::PathStrokeType (6.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (part, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // A dot per sounding note, in the colour of the part it is on.
        const float dot = 3.5f * s;
        for (const auto& note : amp.notes)
        {
            if (note.stage == NoteActivity::Stage::idle)
                continue;

            const auto d = Synth2VisualModel::noteDot (e, note.stage, note.level, note.progress);
            const auto colour = colourFor (note.stage);
            juce::Path body;
            body.addEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (at (d.x, d.y)));
            juce::DropShadow (colour.withAlpha (0.9f), (int) (8.0f * s), {}).drawForPath (g, body);
            g.setColour (juce::Colours::white);
            g.fillPath (body);
        }

        if (parameters[0] != nullptr)
            paintHandles (g);
    }

private:
    using Stage = NoteActivity::Stage;

    struct Drag
    {
        Handle handle;
        juce::Point<float> from;
        Synth2VisualModel::EnvelopeDials dials;   // where they were when the drag began
    };

    Synth2VisualModel::Envelope shape() const noexcept { return Synth2VisualModel::envelope (amp.dials, amp.outputDb); }

    void endDrag()
    {
        for (auto* p : parametersFor (drag->handle))
            p->endChangeGesture();
        drag.reset();
    }

    juce::Point<float> toPlot (float x, float y) const noexcept
    {
        const auto plot = plotArea();
        return { plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight() };
    }

    int handleAt (juce::Point<float> position) const noexcept
    {
        if (parameters[0] == nullptr)
            return -1;
        const auto plot = plotArea();
        const Synth2VisualModel::Dot at { (position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()),
                                          (plot.getBottom() - position.y) / juce::jmax (1.0f, plot.getHeight()) };
        return Synth2VisualModel::handleAt (shape(), at, plot.getWidth(), plot.getHeight(), 9.0f * getUiScale());
    }

    void setHot (int h)
    {
        if (h == hot)
            return;
        hot = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    // Which of DELAY..RELEASE (0..5) handle `h` moves: its stage's time, and
    // for DECAY's corner SUSTAIN as well.
    static std::vector<size_t> indicesFor (Handle h)
    {
        switch (h)
        {
            case Handle::delay:   return { 0 };
            case Handle::attack:  return { 1 };
            case Handle::hold:    return { 2 };
            case Handle::decay:   return { 3, 4 };
            case Handle::release: return { 5 };
        }
        return {};
    }

    std::vector<juce::RangedAudioParameter*> parametersFor (Handle h) const
    {
        std::vector<juce::RangedAudioParameter*> moved;
        for (const auto i : indicesFor (h))
            moved.push_back (parameters[i]);
        return moved;
    }

    // Sets the dragged corner's parameters to `dials` (the times as dial
    // positions, SUSTAIN as its level) and shows them at once.
    void apply (const Synth2VisualModel::EnvelopeDials& dials)
    {
        const std::array<float, 6> positions { dials.delay, dials.attack, dials.hold, dials.decay,
                                               parameters[4]->convertTo0to1 (dials.sustain), dials.release };
        for (const auto i : indicesFor (drag->handle))
            if (! juce::exactlyEqual (parameters[i]->getValue(), positions[i]))
                parameters[i]->setValueNotifyingHost (positions[i]);
        amp.dials = dials;
        repaint();
    }

    // The readout: OUTPUT's level, or the stage (and value) under the pointer.
    juce::String readout() const
    {
        static constexpr std::array<const char*, Synth2VisualModel::numHandles> names { "DELAY", "ATTACK", "HOLD", "DECAY", "RELEASE" };
        const int h = getHotHandle();
        if (h < 0 || parameters[0] == nullptr)
            return Synth2::Format::decibels (amp.outputDb);
        juce::String text (names[(size_t) h]);
        for (auto* p : parametersFor ((Handle) h))
            text << "  " << p->getCurrentValueAsText();
        return text;
    }

    // A ring on each corner that can be dragged, filled on the one under
    // the pointer.
    void paintHandles (juce::Graphics& g) const
    {
        static constexpr std::array<size_t, Synth2VisualModel::numHandles> colourOf { 0, 1, 2, 4, 5 };
        const float s = getUiScale();
        for (int h = 0; h < Synth2VisualModel::numHandles; ++h)
        {
            const auto  colour = colours[colourOf[(size_t) h]];
            const bool  isHot  = h == getHotHandle();
            const float size   = (isHot ? 11.0f : 8.0f) * s;
            const auto  ring   = juce::Rectangle<float> (size, size).withCentre (handleCentre ((Handle) h));
            g.setColour (Synth2::Palette::well);
            g.fillEllipse (ring);
            if (isHot)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.fillEllipse (ring.expanded (3.0f * s));
                g.setColour (colour);
                g.fillEllipse (ring);
            }
            g.setColour (colour);
            g.drawEllipse (ring, juce::jmax (1.0f, 1.4f * s));
        }
    }

    /** The colour of the stage of the envelope a note in `stage` is on. */
    juce::Colour colourFor (Stage stage) const noexcept
    {
        return stage == Stage::idle ? colours[4] : colours[(size_t) stage - 1];
    }

    AmpDisplayState     amp;
    Parameters          parameters {};
    std::optional<Drag> drag;
    int                 hot = -1;
    std::array<juce::Colour, 6> colours { Synth2::Palette::pink, Synth2::Palette::pink, Synth2::Palette::amber,
                                          Synth2::Palette::pink, Synth2::Palette::amber, Synth2::Palette::pink };
};

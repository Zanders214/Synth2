#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "BenchModules.h"
#include "EffectSettings.h"
#include "ParamIDs.h"
#include "Synth2LookAndFeel.h"
#include "Synth2Theme.h"
#include "Synth2VisualModel.h"
#include "VoiceDisplays.h"

/** A switch in the house style, tied to an on/off parameter: a pill with a
    light, lit in its colour while it is on. */
class PillToggle final : public juce::Button
{
public:
    PillToggle (const juce::String& label, juce::Colour onColour, juce::AudioProcessorValueTreeState& state, const char* paramId)
        : juce::Button (label), colour (onColour), attachment (state, paramId, *this)
    {
        setClickingTogglesState (true);
        setComponentID (paramId);
        setTooltip (state.getParameter (paramId)->getName (64));
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  pill = getLocalBounds().toFloat().reduced (0.5f);
        const float r    = pill.getHeight() * 0.5f;
        const bool  on   = getToggleState();

        auto fill = on ? colour.withAlpha (0.16f) : Palette::idleFill;
        if (highlighted || down)
            fill = fill.withMultipliedAlpha (1.6f);
        g.setColour (fill);
        g.fillRoundedRectangle (pill, r);
        g.setColour (on ? colour.withAlpha (0.65f) : Palette::strongLine);
        g.drawRoundedRectangle (pill, r, juce::jmax (1.0f, s));

        const auto light = juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre ({ pill.getX() + r + 1.0f * s, pill.getCentreY() });
        if (on)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (light.expanded (2.5f * s));
        }
        g.setColour (on ? colour : Palette::textFaint);
        g.fillEllipse (light);

        g.setFont (Fonts::sans (9.5f * s, true, 0.14f));
        g.setColour (on ? Palette::textStrong : Palette::textDim);
        g.drawText (getButtonText(), pill.withTrimmedLeft (2.0f * r).withTrimmedRight (r * 0.6f),
                    juce::Justification::centred, false);
    }

private:
    juce::Colour colour;
    float uiScale = 1.0f;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

/** A time mark under an effect's well: the first one starts at its line,
    the last ends at it, the others centre on theirs. */
inline void paintMark (juce::Graphics& g, const juce::String& text, float x, int mark, int last, juce::Rectangle<float> row)
{
    const float width = 40.0f * row.getHeight() / 10.0f;
    if (mark == 0)
        g.drawText (text, row.withX (x).withWidth (width), juce::Justification::centredLeft, false);
    else if (mark == last)
        g.drawText (text, row.withX (x - width).withWidth (width), juce::Justification::centredRight, false);
    else
        g.drawText (text, row.withX (x - 0.5f * width).withWidth (width), juce::Justification::centred, false);
}

/** What the DELAY well shows. */
struct DelayDisplayState
{
    double seconds  = 0.375;
    float  feedback = 0.0f;
    float  tone     = 0.0f;
    float  mix      = 0.0f;
    bool   pingPong = false;
    bool   sync     = false;
    int    division = 0;

    bool operator== (const DelayDisplayState& other) const noexcept
    {
        return ! (seconds < other.seconds) && ! (seconds > other.seconds)
            && juce::exactlyEqual (feedback, other.feedback) && juce::exactlyEqual (tone, other.tone)
            && juce::exactlyEqual (mix, other.mix) && pingPong == other.pingPong && sync == other.sync
            && division == other.division;
    }
    bool operator!= (const DelayDisplayState& other) const noexcept { return ! (*this == other); }
};

/** The DELAY well: the dry sound, then each repeat where it falls across two
    seconds, the left side above the line and the right below, dimming as
    TONE darkens it (see Synth2VisualModel::echoes). */
class EchoDisplay final : public DisplayWell
{
public:
    EchoDisplay() { setInterceptsMouseClicks (false, false); }

    void setColour (juce::Colour newColour) { colour = newColour; repaint(); }

    void setDelayState (const DelayDisplayState& newState)
    {
        if (newState == delay)
            return;
        delay = newState;
        repaint();
    }

    const DelayDisplayState& getDelayState() const noexcept { return delay; }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = getUiScale();
        const auto  time = Format::seconds ((float) delay.seconds);
        auto plot = paintWell (g, "REPEATS", delay.sync ? juce::String (Effects::divisions[(size_t) delay.division].name) + "  " + time : time);

        // Half-second marks, and the line the sides stand on.
        auto marks = plot.removeFromBottom (10.0f * s);
        plot = plot.reduced (4.0f * s, 4.0f * s);
        g.setFont (Fonts::mono (8.5f * s));
        for (int mark = 0; mark <= 4; ++mark)
        {
            const float x = plot.getX() + plot.getWidth() * (float) mark / 4.0f;
            g.setColour (Palette::gridLine);
            g.fillRect (juce::Rectangle (x, plot.getY(), juce::jmax (1.0f, s), plot.getHeight()));
            g.setColour (Palette::textFaint);
            paintMark (g, juce::String (0.5 * mark, 1) + " s", x, mark, 4, marks);
        }
        const float middle = plot.getCentreY();
        const float reach  = plot.getHeight() * 0.5f - 2.0f * s;
        g.setColour (Palette::strongLine);
        g.fillRect (juce::Rectangle (plot.getX(), middle - 0.5f * s, plot.getWidth(), juce::jmax (1.0f, s)));

        g.setFont (Fonts::sans (8.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText ("L", juce::Rectangle (plot.getRight() - 10.0f * s, plot.getY(), 10.0f * s, 10.0f * s), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle (plot.getRight() - 10.0f * s, plot.getBottom() - 10.0f * s, 10.0f * s, 10.0f * s), juce::Justification::centred, false);

        const auto echoes = Synth2VisualModel::echoes (delay.seconds, delay.feedback, delay.tone, delay.mix, delay.pingPong);
        const float barWidth = 4.0f * s;
        for (int i = 0; i < echoes.count; ++i)
        {
            const auto& echo = echoes.taps[(size_t) i];
            const float x    = plot.getX() + echo.x * (plot.getWidth() - barWidth);
            const auto  tint = i == 0 ? Palette::textMid : ColourMath::lerp (Palette::textFaint, colour, echo.brightness);
            const auto  bar  = [&] (float height, bool up)
            {
                if (height <= 0.0f)
                    return;
                const float h = juce::jmax (1.5f * s, height * reach);
                const juce::Rectangle<float> r (x, up ? middle - h : middle, barWidth, h);
                g.setColour (tint.withAlpha (0.25f));
                g.fillRoundedRectangle (r.expanded (2.0f * s, 0.0f), 2.0f * s);
                g.setColour (tint);
                g.fillRoundedRectangle (r, 1.5f * s);
            };
            bar (echo.left, true);
            bar (echo.right, false);
        }
    }

private:
    juce::Colour colour = Synth2::Palette::cyan;
    DelayDisplayState delay;
};

/** What the REVERB well shows. */
struct ReverbDisplayState
{
    float size       = 0.0f;
    float damping    = 0.0f;
    float width      = 0.0f;
    float mix        = 0.0f;
    float preDelayMs = 0.0f;

    bool operator== (const ReverbDisplayState& other) const noexcept
    {
        return juce::exactlyEqual (size, other.size) && juce::exactlyEqual (damping, other.damping)
            && juce::exactlyEqual (width, other.width) && juce::exactlyEqual (mix, other.mix)
            && juce::exactlyEqual (preDelayMs, other.preDelayMs);
    }
    bool operator!= (const ReverbDisplayState& other) const noexcept { return ! (*this == other); }
};

/** The REVERB well: the dry sound, then (after PRE-DELAY) the room ringing
    away across six seconds (on a 60 dB scale), the left side above the line
    and the right below. The wider the room, the more the two sides differ; the more
    DAMPING, the sooner it dulls (see Synth2VisualModel::roomHeight). */
class RoomDisplay final : public DisplayWell
{
public:
    RoomDisplay() { setInterceptsMouseClicks (false, false); }

    void setColour (juce::Colour newColour) { colour = newColour; repaint(); }

    void setReverbState (const ReverbDisplayState& newState)
    {
        if (newState == reverb)
            return;
        reverb = newState;
        repaint();
    }

    const ReverbDisplayState& getReverbState() const noexcept { return reverb; }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = getUiScale();
        auto plot = paintWell (g, "ROOM", "RING " + Format::seconds ((float) Effects::reverbSeconds (reverb.size)));

        auto marks = plot.removeFromBottom (10.0f * s);
        plot = plot.reduced (4.0f * s, 4.0f * s);
        g.setFont (Fonts::mono (8.5f * s));
        for (int mark = 0; mark <= 3; ++mark)
        {
            const float x = plot.getX() + plot.getWidth() * (float) mark / 3.0f;
            g.setColour (Palette::gridLine);
            g.fillRect (juce::Rectangle (x, plot.getY(), juce::jmax (1.0f, s), plot.getHeight()));
            g.setColour (Palette::textFaint);
            paintMark (g, juce::String (2 * mark) + " s", x, mark, 3, marks);
        }
        const float middle = plot.getCentreY();
        const float reach  = plot.getHeight() * 0.5f - 2.0f * s;
        g.setColour (Palette::strongLine);
        g.fillRect (juce::Rectangle (plot.getX(), middle - 0.5f * s, plot.getWidth(), juce::jmax (1.0f, s)));

        // The room, a thin column at a time; each side a little different
        // from the other as WIDTH spreads it.
        const float step    = 2.0f * s;
        const int   columns = juce::jmax (1, (int) (plot.getWidth() / step));
        for (int i = 1; i < columns; ++i)
        {
            const float x      = (float) i / (float) columns;
            const float height = Synth2VisualModel::roomHeight (reverb.size, reverb.mix, x, reverb.preDelayMs);
            if (height <= 0.0f)
                continue;
            const float bright = Synth2VisualModel::roomBrightness (reverb.size, reverb.damping, x);
            const auto  tint   = ColourMath::lerp (Palette::textDim, colour, 0.3f + 0.7f * bright);
            const float left   = height * (1.0f - 0.45f * reverb.width * grain (2 * i));
            const float right  = height * (1.0f - 0.45f * reverb.width * grain (2 * i + 1));
            const float px     = plot.getX() + x * plot.getWidth();
            g.setColour (tint.withAlpha (0.85f));
            g.fillRect (juce::Rectangle (px, middle - left * reach, step * 0.6f, left * reach));
            g.fillRect (juce::Rectangle (px, middle, step * 0.6f, right * reach));
        }

        // The dry sound, at the start.
        const float dry = Effects::dryGain (reverb.mix) * reach;
        g.setColour (Palette::textMid);
        g.fillRoundedRectangle (juce::Rectangle (plot.getX(), middle - dry, 4.0f * s, 2.0f * dry), 1.5f * s);
    }

private:
    // A steady scatter, 0..1, so the room's texture holds still.
    static float grain (int i) noexcept
    {
        const double x = std::sin ((double) i * 12.9898) * 43758.5453;
        return (float) (x - std::floor (x));
    }

    juce::Colour colour = Synth2::Palette::violet;
    ReverbDisplayState reverb;
};

/** What the drive's CURVE well shows. */
struct DriveDisplayState
{
    float amount = 0.4f;
    int   shape  = 0;
    float mix    = 1.0f;

    friend bool operator== (const DriveDisplayState& a, const DriveDisplayState& b) noexcept
    {
        return juce::exactlyEqual (a.amount, b.amount) && a.shape == b.shape && juce::exactlyEqual (a.mix, b.mix);
    }
    friend bool operator!= (const DriveDisplayState& a, const DriveDisplayState& b) noexcept { return ! (a == b); }
};

/**
    CurveDisplay — the drive's well: what comes out (up) for what goes in
    (across), from full scale down to full scale up, MIX included, over the
    faint straight line the sound would follow untouched. The more DRIVE,
    the sooner it bends over; TUBE bends one side sooner than the other.
*/
class CurveDisplay final : public DisplayWell
{
public:
    CurveDisplay() { setInterceptsMouseClicks (false, false); }

    void setColour (juce::Colour newColour) { colour = newColour; repaint(); }

    void setDriveState (const DriveDisplayState& newState)
    {
        if (newState == drive)
            return;
        drive = newState;
        repaint();
    }

    const DriveDisplayState& getDriveState() const noexcept { return drive; }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = getUiScale();
        const auto  readout = "+" + juce::String (Effects::maxDriveDb * drive.amount, 1) + " dB  "
                            + Effects::driveShapeNames[(size_t) juce::jlimit (0, 2, drive.shape)];
        auto plot = paintWell (g, "CURVE", readout).reduced (4.0f * s, 2.0f * s);
        plot = plot.withSizeKeepingCentre (juce::jmin (plot.getWidth(), plot.getHeight() * 1.6f), plot.getHeight());

        // The axes, and the straight line of no drive at all.
        const float line = juce::jmax (1.0f, s);
        g.setColour (Palette::gridLine);
        g.fillRect (juce::Rectangle (plot.getCentreX() - 0.5f * line, plot.getY(), line, plot.getHeight()));
        g.fillRect (juce::Rectangle (plot.getX(), plot.getCentreY() - 0.5f * line, plot.getWidth(), line));
        const auto at = [&plot] (float x, float y)
        {
            return juce::Point (plot.getCentreX() + 0.5f * x * plot.getWidth(), plot.getCentreY() - 0.5f * y * plot.getHeight());
        };
        juce::Path straight;
        straight.startNewSubPath (at (-1.0f, -1.0f));
        straight.lineTo (at (1.0f, 1.0f));
        const float dashes[] { 3.0f * s, 3.0f * s };
        juce::PathStrokeType (line).createDashedStroke (straight, straight, dashes, 2);
        g.setColour (Palette::strongLine.withMultipliedAlpha (2.0f));
        g.fillPath (straight);

        // The curve, glowing.
        Effects::DriveSettings settings;
        settings.amount = drive.amount;
        settings.shape  = drive.shape;
        const float dry = Effects::dryGain (drive.mix);
        const float wet = Effects::wetGain (drive.mix);
        juce::Path curve;
        const int points = juce::jmax (64, (int) plot.getWidth());
        for (int p = 0; p <= points; ++p)
        {
            const float x = -1.0f + 2.0f * (float) p / (float) points;
            const float y = juce::jlimit (-1.0f, 1.0f, dry * x + wet * Effects::driveTransfer (settings, x));
            if (p == 0)
                curve.startNewSubPath (at (x, y));
            else
                curve.lineTo (at (x, y));
        }
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (6.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    DriveDisplayState drive;
    juce::Colour colour { Synth2::Palette::amber };
};

/** A row of pills, one per choice of a choice parameter, the chosen one lit
    in its colour: click one to pick it (as one gesture). */
class SegmentedChoice final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    SegmentedChoice (juce::RangedAudioParameter& parameter, std::vector<juce::String> choiceNames, juce::Colour onColour)
        : names (std::move (choiceNames)), colour (onColour),
          attachment (parameter, [this] (float value) { selected = juce::roundToInt (value); repaint(); })
    {
        setComponentID (parameter.paramID);
        setTooltip (parameter.getName (64));
        setRepaintsOnMouseActivity (true);
        attachment.sendInitialUpdate();
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }
    int  getSelected() const noexcept { return selected; }
    int  getNumChoices() const noexcept { return (int) names.size(); }

    juce::Rectangle<float> segmentBounds (int index) const noexcept
    {
        const float gap   = 6.0f * uiScale;
        const auto  count = (float) names.size();
        const float width = ((float) getWidth() - gap * (count - 1.0f)) / count;
        return { (float) index * (width + gap), 0.0f, width, (float) getHeight() };
    }

    void choose (int index) { attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, getNumChoices() - 1, index)); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < getNumChoices(); ++i)
            if (segmentBounds (i).contains (e.position))
                choose (i);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto  mouse = getMouseXYRelative().toFloat();
        for (int i = 0; i < getNumChoices(); ++i)
        {
            const auto  pill = segmentBounds (i).reduced (0.5f);
            const float r    = pill.getHeight() * 0.5f;
            const bool  on   = i == selected;
            auto fill = on ? colour.withAlpha (0.16f) : Palette::idleFill;
            if (isMouseOver() && pill.contains (mouse))
                fill = fill.withMultipliedAlpha (1.6f);
            g.setColour (fill);
            g.fillRoundedRectangle (pill, r);
            g.setColour (on ? colour.withAlpha (0.65f) : Palette::strongLine);
            g.drawRoundedRectangle (pill, r, juce::jmax (1.0f, s));
            g.setFont (Fonts::sans (9.5f * s, true, 0.14f));
            g.setColour (on ? Palette::textStrong : Palette::textDim);
            g.drawText (names[(size_t) i], pill, juce::Justification::centred, false);
        }
    }

private:
    std::vector<juce::String> names;
    juce::Colour colour;
    juce::ParameterAttachment attachment;
    int   selected = 0;
    float uiScale  = 1.0f;
};

/**
    EffectCard — one effect on the EFFECTS tab: its power switch, its place
    in the chain and its name in the header, its well, a row of dials and any
    switches of its own. Off, everything but the switch greys out. Drag the
    header sideways to move the card to the other place in the chain (unless
    it has a fixed place: see setMovable).

    Its dials sit in rows of three under the well, by slot (0..2 the first
    row, 3..5 the second), each row spread across the card; a card with one
    row of dials gives the other row's room to its well.
*/
class EffectCard : public juce::Component
{
public:
    EffectCard (const juce::String& cardTitle, const char* onParam, juce::Colour cardColour, Synth2LookAndFeel& looksToUse,
                juce::AudioProcessorValueTreeState& stateToUse)
        : title (cardTitle), colour (cardColour), looks (looksToUse), state (stateToUse),
          power (cardColour, stateToUse, onParam)
    {
        addAndMakeVisible (power);
        power.onClick = [this] { showPower(); };
    }

    ~EffectCard() override
    {
        for (auto& dial : dials)
            dial->slider.setLookAndFeel (nullptr);
    }

    /** The header was dragged sideways and let go, the card's centre now at x (in its parent). */
    void onDropped (std::function<void (int)> handler) { droppedHandler = std::move (handler); }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        resized();
        repaint();
    }

    void setPlace (int newPlace) { if (place != newPlace) { place = newPlace; repaint(); } }
    void setMovable (bool canMove) { movable = canMove; repaint(); }
    bool isMovable() const noexcept { return movable; }
    int  getPlace() const noexcept { return place; }
    const juce::String& getTitle() const noexcept { return title; }
    juce::Colour getColour() const noexcept { return colour; }
    PowerSwitch& getPower() noexcept { return power; }
    bool isBeingDragged() const noexcept { return dragging; }

    juce::Slider& getDial (int index) { return dials[(size_t) index]->slider; }
    int getNumDials() const noexcept  { return (int) dials.size(); }

    juce::Rectangle<float> headerBounds() const noexcept { return getLocalBounds().toFloat().withHeight (30.0f * uiScale); }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s      = uiScale;
        const auto  card   = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = 14.0f * s;

        juce::ColourGradient fill (Palette::panelTop.brighter (0.06f), 0.0f, card.getY(),
                                   Palette::panelBottom.brighter (0.04f), 0.0f, card.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (card, radius);
        g.setColour (dragging ? Palette::accent.withAlpha (0.6f) : Palette::strongLine);
        g.drawRoundedRectangle (card, radius, juce::jmax (1.0f, s));

        // Header: the power switch, its place in the chain in a ring, then its name.
        const auto header = headerBounds();
        const auto ring   = juce::Rectangle<float> (18.0f * s, 18.0f * s).withCentre ({ header.getX() + 47.0f * s, header.getCentreY() });
        g.setColour (colour.withAlpha (on() ? 0.9f : 0.4f));
        g.drawEllipse (ring, juce::jmax (1.0f, 1.4f * s));
        g.setFont (Fonts::mono (10.0f * s));
        g.drawText (juce::String (place), ring, juce::Justification::centred, false);
        g.setFont (Fonts::sans (11.0f * s, true, 0.18f));
        g.setColour (Palette::textStrong);
        g.drawText (title, header.withTrimmedLeft (64.0f * s), juce::Justification::centredLeft, false);

        // A grip, saying the card can be dragged.
        if (movable)
        {
            g.setColour (Palette::textFaint);
            const auto grip = header.withSizeKeepingCentre (18.0f * s, 8.0f * s).withX (header.getRight() - 34.0f * s);
            for (int row = 0; row < 2; ++row)
                for (int dot = 0; dot < 3; ++dot)
                    g.fillEllipse (grip.getX() + (float) dot * 7.0f * s, grip.getY() + (float) row * 6.0f * s, 2.2f * s, 2.2f * s);
        }

        g.setColour (Palette::hairline);
        g.fillRect (header.withTop (header.getBottom() - juce::jmax (1.0f, s)).reduced (12.0f * s, 0.0f));

        for (const auto& dial : dials)
            if (dial->slider.isVisible())
                BenchStyle::paintDialLabel (g, dial->column.toFloat(), dial->slider.getBounds().toFloat().getBottom(), dial->name,
                                            BenchStyle::dialText (dial->paramId, state.getRawParameterValue (dial->paramId)->load()),
                                            dial->colour, on() ? 1.0f : 0.4f, s);
    }

    void resized() override
    {
        const float s = uiScale;
        const auto px = [s] (float v) { return juce::roundToInt (v * s); };

        const auto header = getLocalBounds().removeFromTop (px (30.0f));
        power.setBounds (header.getX() + px (12.0f), header.getY() + px (6.0f), px (18.0f), px (18.0f));

        auto area = getLocalBounds().withTrimmedTop (px (30.0f)).reduced (px (12.0f), 0);
        area.removeFromTop (px (10.0f));
        const int rows = numRows();
        layoutWell (area.removeFromTop (px (wellHeight + (float) (maxRows - rows) * rowHeight)));

        // Rows of up to three dials across the card; dials sharing a slot
        // (one shown at a time) take the same place.
        for (int row = 0; row < rows; ++row)
        {
            area.removeFromTop (px (14.0f));
            const auto band  = area.removeFromTop (px (52.0f + 31.0f));
            const int  count = slotsInRow (row);
            const int  columnWidth = band.getWidth() / juce::jmax (1, count);
            for (const auto& dial : dials)
            {
                if (dial->columnIndex / perRow != row)
                    continue;
                const int column = dial->columnIndex % perRow;
                dial->column = band.withTrimmedLeft (column * columnWidth);
                if (column + 1 < count)
                    dial->column.setWidth (columnWidth);
                dial->slider.setBounds (dial->column.withHeight (px (52.0f)).withSizeKeepingCentre (px (52.0f), px (52.0f)));
            }
        }

        area.removeFromTop (px (14.0f));
        layoutSwitches (area.removeFromTop (px (24.0f)));
    }

    /** Greys the insides out while the effect is off. */
    void showPower()
    {
        const float alpha = on() ? 1.0f : 0.4f;
        for (auto* child : getChildren())
            if (child != &power)
                child->setAlpha (alpha);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! movable || ! headerBounds().contains (e.position))
            return;
        dragging  = true;
        dragStart = getPosition();
        toFront (false);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const int right = getParentComponent() != nullptr ? getParentWidth() - getWidth() : dragStart.x;
        setTopLeftPosition (juce::jlimit (0, juce::jmax (0, right), dragStart.x + e.getDistanceFromDragStartX()), dragStart.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;
        dragging = false;
        repaint();
        if (droppedHandler != nullptr)
            droppedHandler (getBounds().getCentreX());
    }

    juce::MouseCursor getMouseCursor() override
    {
        return movable && headerBounds().contains (getMouseXYRelative().toFloat()) ? juce::MouseCursor::DraggingHandCursor
                                                                                    : juce::MouseCursor::NormalCursor;
    }

protected:
    /** Gives the card a dial for `paramId` in slot `columnIndex` (0..2 the
        first row, 3..5 the second). */
    void addDial (const char* paramId, const char* name, juce::Colour dialColour, int columnIndex)
    {
        auto dial = std::make_unique<Dial>();
        dial->paramId     = paramId;
        dial->name        = name;
        dial->colour      = dialColour;
        dial->columnIndex = columnIndex;
        BenchStyle::setUpDial (dial->slider, dialColour, looks, state, paramId);
        dial->slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (dial->slider);
        dial->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, dial->slider);
        dials.push_back (std::move (dial));
    }

    /** Lays the card's well out in `well`. */
    virtual void layoutWell (juce::Rectangle<int> well) = 0;

    /** Lays the card's own switches out along `row` (none by default). */
    virtual void layoutSwitches (juce::Rectangle<int> row) { juce::ignoreUnused (row); }

    float getUiScale() const noexcept { return uiScale; }

private:
    struct Dial
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        const char* paramId = "";
        const char* name    = "";
        juce::Colour colour;
        juce::Rectangle<int> column;
        int columnIndex = 0;
    };

    bool on() const noexcept { return power.getToggleState(); }

    static constexpr int   perRow     = 3;
    static constexpr int   maxRows    = 2;
    static constexpr float wellHeight = 164.0f;          // with two rows of dials under it
    static constexpr float rowHeight  = 14.0f + 52.0f + 31.0f;

    int numRows() const noexcept
    {
        int rows = 0;
        for (const auto& dial : dials)
            rows = juce::jmax (rows, dial->columnIndex / perRow + 1);
        return juce::jmin (rows, maxRows);
    }

    // How many columns row `row` spreads across: up to its last slot in use.
    int slotsInRow (int row) const noexcept
    {
        int count = 0;
        for (const auto& dial : dials)
            if (dial->columnIndex / perRow == row)
                count = juce::jmax (count, dial->columnIndex % perRow + 1);
        return count;
    }

    juce::String title;
    juce::Colour colour;
    Synth2LookAndFeel& looks;
    juce::AudioProcessorValueTreeState& state;
    PowerSwitch power;
    std::vector<std::unique_ptr<Dial>> dials;
    std::function<void (int)> droppedHandler;
    juce::Point<int> dragStart;
    float uiScale  = 1.0f;
    int   place    = 1;
    bool  movable  = true;
    bool  dragging = false;
};

/** The DELAY card: its repeats in the well over TIME and FEEDBACK, TONE and MIX,
    with SYNC (TIME then picks a note length at the host's tempo) and
    PING-PONG beneath. */
class DelayCard final : public EffectCard
{
public:
    DelayCard (Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : EffectCard ("DELAY", ParamID::delayOn, Synth2::Palette::cyan, looksToUse, stateToUse),
          sync ("SYNC", Synth2::Palette::cyan, stateToUse, ParamID::delaySync),
          pingPong ("PING-PONG", Synth2::Palette::cyan, stateToUse, ParamID::delayPingPong)
    {
        addAndMakeVisible (display);
        display.setColour (Synth2::Palette::cyan);
        addDial (ParamID::delayTime,     "TIME",     BenchStyle::dialColour (0.0f),  0);
        addDial (ParamID::delayNote,     "TIME",     BenchStyle::dialColour (0.0f),  0);
        addDial (ParamID::delayFeedback, "FEEDBACK", BenchStyle::dialColour (0.33f), 1);
        addDial (ParamID::delayTone,     "TONE",     BenchStyle::dialColour (0.66f), 3);
        addDial (ParamID::delayMix,      "MIX",      BenchStyle::dialColour (1.0f),  4);
        addAndMakeVisible (sync);
        addAndMakeVisible (pingPong);
    }

    EchoDisplay& getDisplay() noexcept { return display; }
    PillToggle&  getSync() noexcept     { return sync; }
    PillToggle&  getPingPong() noexcept { return pingPong; }

    /** The well follows the settings, and TIME turns milliseconds or note
        lengths as SYNC says. */
    void refresh (const Effects::DelaySettings& delay, double bpm)
    {
        DelayDisplayState shown;
        shown.seconds  = Effects::delaySeconds (delay, bpm);
        shown.feedback = delay.feedback;
        shown.tone     = delay.tone;
        shown.mix      = delay.mix;
        shown.pingPong = delay.pingPong;
        shown.sync     = delay.sync;
        shown.division = juce::jlimit (0, (int) Effects::divisions.size() - 1, delay.division);
        display.setDelayState (shown);

        if (getDial (0).isVisible() == delay.sync || getDial (1).isVisible() != delay.sync)
        {
            getDial (0).setVisible (! delay.sync);
            getDial (1).setVisible (delay.sync);
            repaint();
        }
    }

    void setUiScale (float newScale)
    {
        sync.setUiScale (newScale);
        pingPong.setUiScale (newScale);
        display.setUiScale (newScale);
        EffectCard::setUiScale (newScale);
    }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        display.setBounds (well);
    }

    void layoutSwitches (juce::Rectangle<int> row) override
    {
        const auto px = [s = getUiScale()] (float v) { return juce::roundToInt (v * s); };
        sync.setBounds (row.withWidth (px (78.0f)).withX (row.getX() + px (10.0f)));
        pingPong.setBounds (sync.getBounds().withWidth (px (104.0f)).translated (px (88.0f), 0));
    }

    EchoDisplay display;
    PillToggle  sync;
    PillToggle  pingPong;
};

/** The REVERB card: the room in the well over SIZE (how long it rings),
    PRE-DELAY and DAMPING, WIDTH and MIX. */
class ReverbCard final : public EffectCard
{
public:
    ReverbCard (Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : EffectCard ("REVERB", ParamID::reverbOn, Synth2::Palette::violet, looksToUse, stateToUse)
    {
        addAndMakeVisible (display);
        display.setColour (Synth2::Palette::violet);
        addDial (ParamID::reverbSize,     "SIZE",      BenchStyle::dialColour (0.0f),  0);
        addDial (ParamID::reverbPreDelay, "PRE-DELAY", BenchStyle::dialColour (0.25f), 1);
        addDial (ParamID::reverbDamping,  "DAMPING",   BenchStyle::dialColour (0.5f),  2);
        addDial (ParamID::reverbWidth,    "WIDTH",     BenchStyle::dialColour (0.75f), 3);
        addDial (ParamID::reverbMix,      "MIX",       BenchStyle::dialColour (1.0f),  4);
    }

    RoomDisplay& getDisplay() noexcept { return display; }

    void refresh (const Effects::ReverbSettings& reverb)
    {
        display.setReverbState ({ reverb.size, reverb.damping, reverb.width, reverb.mix, reverb.preDelayMs });
    }

    void setUiScale (float newScale)
    {
        display.setUiScale (newScale);
        EffectCard::setUiScale (newScale);
    }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        display.setBounds (well);
    }

    RoomDisplay display;
};

/** The DRIVE card: its curve in the well over DRIVE, TONE and MIX, with
    its shape (WARM, TUBE or HARD) beneath. It always comes first. */
class DriveCard final : public EffectCard
{
public:
    DriveCard (Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : EffectCard ("DRIVE", ParamID::driveOn, Synth2::Palette::amber, looksToUse, stateToUse),
          shape (*stateToUse.getParameter (ParamID::driveShape), { "WARM", "TUBE", "HARD" }, Synth2::Palette::amber)
    {
        setMovable (false);
        addAndMakeVisible (display);
        display.setColour (Synth2::Palette::amber);
        addDial (ParamID::driveAmount, "DRIVE", BenchStyle::dialColour (0.0f), 0);
        addDial (ParamID::driveTone,   "TONE",  BenchStyle::dialColour (0.5f), 1);
        addDial (ParamID::driveMix,    "MIX",   BenchStyle::dialColour (1.0f), 2);
        addAndMakeVisible (shape);
    }

    CurveDisplay&    getDisplay() noexcept { return display; }
    SegmentedChoice& getShape() noexcept   { return shape; }

    void refresh (const Effects::DriveSettings& drive)
    {
        display.setDriveState ({ drive.amount, drive.shape, drive.mix });
    }

    void setUiScale (float newScale)
    {
        shape.setUiScale (newScale);
        display.setUiScale (newScale);
        EffectCard::setUiScale (newScale);
    }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        display.setBounds (well);
    }

    void layoutSwitches (juce::Rectangle<int> row) override
    {
        const auto px = [s = getUiScale()] (float v) { return juce::roundToInt (v * s); };
        shape.setBounds (row.reduced (px (10.0f), 0));
    }

    CurveDisplay    display;
    SegmentedChoice shape;
};

/**
    EffectsView — the EFFECTS tab: the effects every note goes through
    together, as a chain read left to right, from the notes to OUTPUT: the
    drive first, then the delay and the reverb. Drag the delay or the reverb
    past the other to swap them.
*/
class EffectsView final : public juce::Component
{
public:
    static constexpr float cardWidth  = 292.0f;
    static constexpr float cardHeight = 448.0f;
    static constexpr float stub       = 29.0f;   // the arrows into, between and out of the cards

    EffectsView (juce::AudioProcessorValueTreeState& stateToUse, Synth2LookAndFeel& looks)
        : state (stateToUse), drive (looks, stateToUse), delay (looks, stateToUse), reverb (looks, stateToUse)
    {
        reverbFirst = state.getRawParameterValue (ParamID::effectsOrder)->load() >= 0.5f;
        shownOn     = { drive.getPower().getToggleState(), delay.getPower().getToggleState(), reverb.getPower().getToggleState() };
        for (auto* card : cards())
        {
            card->showPower();
            addAndMakeVisible (*card);
            card->onDropped ([this, card] (int centreX)
            {
                // The delay and the reverb share the second and third places.
                const int  between    = (slotBounds (2).getCentreX() + slotBounds (3).getCentreX()) / 2;
                const bool wantsFirst = centreX < between;
                if (card->isMovable() && wantsFirst != (card->getPlace() == 2))
                    swapOrder();
                layOut();
            });
        }
    }

    DriveCard&  getDrive() noexcept  { return drive; }
    DelayCard&  getDelay() noexcept  { return delay; }
    ReverbCard& getReverb() noexcept { return reverb; }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        drive.setUiScale (newScale);
        delay.setUiScale (newScale);
        reverb.setUiScale (newScale);
        layOut();
        repaint();
    }

    /** The cards follow the settings, however they change. */
    void refresh (const Effects::Settings& settings)
    {
        drive.refresh (settings.drive);
        delay.refresh (settings.delay, settings.bpm);
        reverb.refresh (settings.reverb);
        if (const std::array<bool, 3> on { settings.drive.on, settings.delay.on, settings.reverb.on }; on != shownOn)
        {
            shownOn = on;
            for (auto* card : cards())
                card->showPower();
            repaint();
        }
        if (settings.reverbFirst != reverbFirst)
        {
            reverbFirst = settings.reverbFirst;
            layOut();
            repaint();
        }
    }

    /** Puts the other effect first. */
    void swapOrder()
    {
        auto* order = state.getParameter (ParamID::effectsOrder);
        order->beginChangeGesture();
        order->setValueNotifyingHost (order->getValue() < 0.5f ? 1.0f : 0.0f);
        order->endChangeGesture();
        reverbFirst = order->getValue() >= 0.5f;
        layOut();
        repaint();
    }

    /** Where the card in `place` (1 to 3) sits. */
    juce::Rectangle<int> slotBounds (int place) const noexcept
    {
        const float s = uiScale;
        const float x = stub + (float) (place - 1) * (cardWidth + stub);
        return juce::Rectangle<float> (x * s, cardTop() * s, cardWidth * s, cardHeight * s).toNearestInt();
    }

    void resized() override { layOut(); }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = uiScale;

        // The chain in words over the cards, each effect in its own colour.
        const auto& second = reverbFirst ? static_cast<const EffectCard&> (reverb) : static_cast<const EffectCard&> (delay);
        const auto& third  = reverbFirst ? static_cast<const EffectCard&> (delay) : static_cast<const EffectCard&> (reverb);
        const auto  line   = juce::Rectangle<float> (stub * s, (cardTop() - 40.0f) * s, (3.0f * cardWidth + 2.0f * stub) * s, 16.0f * s);
        const auto  arrow  = juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  "));
        juce::AttributedString chain;
        const auto font = Fonts::sans (10.0f * s, true, 0.18f);
        chain.append ("THE NOTES", font, Palette::textDim);
        for (const auto* card : { static_cast<const EffectCard*> (&drive), &second, &third })
        {
            chain.append (arrow, font, Palette::textDim);
            chain.append (card->getTitle(), font, card->getColour());
        }
        chain.append (arrow + "OUTPUT", font, Palette::textDim);
        chain.setJustification (juce::Justification::centredLeft);
        chain.draw (g, line);

        g.setFont (Fonts::sans (9.5f * s, false, 0.04f));
        g.setColour (Palette::textFaint);
        g.drawText ("Drag DELAY or REVERB by its header to swap them", line, juce::Justification::centredRight, false);

        // Arrows into the first card, between them, and out to OUTPUT.
        const float y = (cardTop() + 30.0f + 10.0f + 82.0f) * s;   // level with the wells' middles
        g.setColour (Palette::strongLine.withMultipliedAlpha (2.5f));
        for (int gap = 0; gap <= 3; ++gap)
        {
            const float from = (float) gap * (stub + cardWidth) + 4.0f;
            g.drawArrow ({ from * s, y, (from + stub - 8.0f) * s, y }, juce::jmax (1.0f, 1.5f * s), 7.0f * s, 7.0f * s);
        }
    }

private:
    std::array<EffectCard*, 3> cards() noexcept { return { &drive, &delay, &reverb }; }

    float cardTop() const noexcept { return 0.5f * ((float) Bench::areaHeight - cardHeight) + 20.0f; }

    // Each card in its place, unless it is being dragged.
    void layOut()
    {
        drive.setPlace (1);
        delay.setPlace (reverbFirst ? 3 : 2);
        reverb.setPlace (reverbFirst ? 2 : 3);
        for (auto* card : cards())
            if (! card->isBeingDragged())
                card->setBounds (slotBounds (card->getPlace()));
    }

    juce::AudioProcessorValueTreeState& state;
    DriveCard  drive;
    DelayCard  delay;
    ReverbCard reverb;
    std::array<bool, 3> shownOn {};   // drive, delay, reverb
    float uiScale     = 1.0f;
    bool  reverbFirst = false;
};

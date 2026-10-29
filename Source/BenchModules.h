#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Bench.h"
#include "ParamIDs.h"
#include "Synth2Engine.h"
#include "Synth2LookAndFeel.h"
#include "Synth2Theme.h"
#include "VoiceDisplays.h"
#include "WavePicker.h"

namespace BenchStyle
{
    // Each module's dials run part of the spectrum, cyan to amber.
    inline juce::Colour dialColour (float position) { return Synth2::ColourMath::spectrum (position); }

    /** A dial's value as it reads under the dial. */
    inline juce::String dialText (const juce::String& id, float value)
    {
        using namespace Synth2;
        for (const auto& osc : ParamID::oscillators)
        {
            if (id == osc.pitch)  return Format::semitones (juce::roundToInt (value));
            if (id == osc.fine)   return Format::fineCents (value);
            if (id == osc.voices) return juce::String (juce::roundToInt (value));
            if (id == osc.detune) return Format::cents (value);
        }
        for (const auto& env : ParamID::envelopes)
            if (id == env.attack || id == env.release)
                return Format::seconds (value);
        if (id == ParamID::output)
            return Format::decibels (value);
        return Format::percent (value);   // the levels and WIDTH
    }

    /** A dial in the house style, tied to its parameter. */
    inline void setUpDial (juce::Slider& dial, juce::Colour colour, juce::LookAndFeel& looks,
                           juce::AudioProcessorValueTreeState& state, const char* paramId)
    {
        dial.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        // 270 degree sweep from 7:30 to 4:30. JUCE wants non-negative angles, so
        // -135..135 degrees is written as the identical 225..495.
        dial.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        dial.setMouseDragSensitivity (200);
        dial.setColour (juce::Slider::rotarySliderFillColourId, colour);
        dial.setLookAndFeel (&looks);
        dial.setComponentID (paramId);
        dial.setPaintingIsUnclipped (true);   // the arc's glow reaches past the dial

        const auto* parameter = state.getParameter (paramId);
        dial.setDoubleClickReturnValue (true, state.getParameterRange (paramId).convertFrom0to1 (parameter->getDefaultValue()));
        dial.setTooltip (parameter->getName (64));
    }

    /** A jack: a dark socket in a ring, lit in `colour` when something is
        plugged in. */
    inline void paintJack (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool plugged, bool hot, float s)
    {
        using namespace Synth2;
        g.setColour (Palette::well);
        g.fillEllipse (area);
        g.setColour (plugged || hot ? colour : Palette::strongLine.withMultipliedAlpha (2.0f));
        g.drawEllipse (area.reduced (0.75f * s), juce::jmax (1.0f, 1.5f * s));
        g.setColour (plugged ? colour : Palette::textFaint);
        g.fillEllipse (area.reduced (area.getWidth() * 0.32f));
    }
}

/**
    ChoiceTray — a small floating list of choices, opened over the editor at
    a point: the bench's "add a module" menu and the signal-flow settings.

    Each choice has a name and a line of detail; a choice can be greyed out
    (it can't be picked) or ticked (the current one). Click a choice to pick
    it; click anywhere else, or press Escape, to close. The arrow keys move
    through the choices and Return picks.
*/
class ChoiceTray final : public juce::Component
{
public:
    struct Choice
    {
        juce::String name;
        juce::String detail;
        bool enabled = true;
        bool ticked  = false;
    };

    void onChoose (std::function<void (int)> handler) { chooseHandler = std::move (handler); }

    ChoiceTray()
    {
        setVisible (false);
        setWantsKeyboardFocus (true);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    /** Opens with `newChoices` under `title`, its top left at `at` (moved
        in, if need be, to stay inside the tray's area). */
    void open (const juce::String& newTitle, std::vector<Choice> newChoices, juce::Point<int> at)
    {
        title       = newTitle;
        choices     = std::move (newChoices);
        origin      = at;
        highlighted = -1;
        juce::Desktop::getInstance().getAnimator().fadeIn (this, 80);   // --dur-fast
        toFront (true);
        grabKeyboardFocus();
    }

    void close() { setVisible (false); }
    bool isOpen() const noexcept { return isVisible(); }
    int  getNumChoices() const noexcept { return (int) choices.size(); }
    const Choice& getChoice (int index) const { return choices[(size_t) index]; }
    const juce::String& getTitle() const noexcept { return title; }

    juce::Rectangle<float> trayBounds() const noexcept
    {
        const float s = uiScale;
        const float width  = 260.0f * s;
        const float height = (34.0f + 44.0f * (float) choices.size() + 8.0f) * s;
        const auto  area   = getLocalBounds().toFloat().reduced (8.0f * s);
        return juce::Rectangle<float> ((float) origin.x, (float) origin.y, width, height).constrainedWithin (area);
    }

    juce::Rectangle<float> choiceBounds (int index) const noexcept
    {
        const float s = uiScale;
        return trayBounds().reduced (8.0f * s, 0.0f).withTrimmedTop (34.0f * s)
                           .withHeight (44.0f * s).translated (0.0f, 44.0f * s * (float) index);
    }

    int choiceAt (juce::Point<float> position) const noexcept
    {
        for (int i = 0; i < (int) choices.size(); ++i)
            if (choiceBounds (i).contains (position))
                return i;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override { setHighlighted (choiceAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override   { setHighlighted (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int choice = choiceAt (e.position); choice >= 0)
            pick (choice);
        else if (! trayBounds().contains (e.position))
            close();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
            close();
        else if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
            setHighlighted ((juce::jmax (0, highlighted) + (key == juce::KeyPress::downKey ? (highlighted < 0 ? 0 : 1) : -1)
                             + (int) choices.size()) % juce::jmax (1, (int) choices.size()));
        else if (key == juce::KeyPress::returnKey && highlighted >= 0)
            pick (highlighted);
        else
            return false;
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = uiScale;

        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillAll();

        const auto tray = trayBounds();
        juce::Path shape;
        shape.addRoundedRectangle (tray, 12.0f * s);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (24.0f * s), { 0, (int) (10.0f * s) }).drawForPath (g, shape);
        g.setColour (Palette::panelTop);
        g.fillPath (shape);
        g.setColour (Palette::strongLine);
        g.strokePath (shape, juce::PathStrokeType (juce::jmax (1.0f, s)));

        g.setFont (Fonts::sans (9.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText (title, tray.reduced (16.0f * s, 0.0f).withHeight (34.0f * s), juce::Justification::centredLeft, false);

        for (int i = 0; i < (int) choices.size(); ++i)
        {
            const auto& choice = choices[(size_t) i];
            const auto  row    = choiceBounds (i);
            if (i == highlighted && choice.enabled)
            {
                g.setColour (Palette::idleFill.withMultipliedAlpha (1.6f));
                g.fillRoundedRectangle (row, 8.0f * s);
            }

            auto text = row.reduced (10.0f * s, 6.0f * s);
            const auto mark = text.removeFromRight (16.0f * s).withSizeKeepingCentre (8.0f * s, 8.0f * s);
            if (choice.ticked)
            {
                g.setColour (Palette::accent);
                g.fillEllipse (mark);
            }

            g.setFont (Fonts::sans (12.0f * s, true));
            g.setColour (choice.enabled ? Palette::textStrong : Palette::textFaint);
            g.drawText (choice.name, text.removeFromTop (17.0f * s), juce::Justification::centredLeft, false);
            g.setFont (Fonts::sans (10.0f * s));
            g.setColour (choice.enabled ? Palette::textDim : Palette::textFaint);
            g.drawText (choice.detail, text, juce::Justification::centredLeft, true);
        }
    }

private:
    void setHighlighted (int index)
    {
        if (index == highlighted)
            return;
        highlighted = index;
        repaint();
    }

    void pick (int index)
    {
        if (! choices[(size_t) index].enabled)
            return;
        close();
        if (chooseHandler != nullptr)
            chooseHandler (index);
    }

    std::function<void (int)> chooseHandler;
    juce::String        title;
    std::vector<Choice> choices;
    juce::Point<int>    origin;
    int   highlighted = -1;
    float uiScale     = 1.0f;
};

/**
    BenchModule — one module on the bench: a card with a header bar (its
    name, which drags it about, and a cross that takes it off the bench) over
    its display well and its dials, in rows of three, their names and values
    beneath.

    When sound follows the cables, the header carries the module's jacks: an
    OUT jack to drag a cable from, and on an envelope an IN jack to patch an
    oscillator into. When it follows the layout, the header shows where the
    module comes in the order; an envelope's header can also name what it
    shapes ("> OSC 2").
*/
class BenchModule : public juce::Component
{
public:
    BenchModule (Bench::ModuleId moduleId, const juce::String& moduleTitle, Synth2LookAndFeel& looksToUse,
                 juce::AudioProcessorValueTreeState& stateToUse)
        : id (moduleId), title (moduleTitle), looks (looksToUse), state (stateToUse)
    {
        setRepaintsOnMouseActivity (true);
    }

    ~BenchModule() override
    {
        for (auto& dial : dials)
            dial->slider.setLookAndFeel (nullptr);
    }

    const Bench::ModuleId& getId() const noexcept { return id; }
    const juce::String& getTitle() const noexcept { return title; }

    enum class Jack { out, in };
    enum JackPhase { jackDown, jackDrag, jackUp };

    /** The cross was clicked. */
    void onRemove (std::function<void()> handler) { removeHandler = std::move (handler); }
    /** The header was dragged and let go, the module's top left now here (in its parent). */
    void onMoved (std::function<void (juce::Point<int>)> handler) { movedHandler = std::move (handler); }
    /** A jack was pressed, dragged or let go (the event is the module's). */
    void onJack (std::function<void (Jack, const juce::MouseEvent&, int phase)> handler) { jackHandler = std::move (handler); }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        resized();
        repaint();
    }

    void setOrder (int newOrder)                { if (order != newOrder) { order = newOrder; repaint(); } }
    void setTag (const juce::String& newTag)    { if (tag != newTag) { tag = newTag; repaint(); } }
    void setJacksShown (bool out, bool in)      { if (jacks.out != out || jacks.in != in) { jacks.out = out; jacks.in = in; repaint(); } }
    void setPlugged (bool out, bool in)         { if (jacks.outPlugged != out || jacks.inPlugged != in) { jacks.outPlugged = out; jacks.inPlugged = in; repaint(); } }
    void setJackColour (juce::Colour c)         { jacks.colour = c; }
    int  getOrder() const noexcept              { return order; }
    const juce::String& getTag() const noexcept { return tag; }
    bool isJackShown (Jack which = Jack::out) const noexcept { return which == Jack::out ? jacks.out : jacks.in; }
    juce::Colour getJackColour() const noexcept { return jacks.colour; }

    /** Greys the module's insides out (not heard, or not shaping anything),
        saying why in the dials' tooltips. */
    void setDimmed (bool shouldBeDimmed, const juce::String& why = {})
    {
        if (dimmed == shouldBeDimmed && dimmedWhy == why)
            return;
        dimmed    = shouldBeDimmed;
        dimmedWhy = why;
        for (auto* child : getChildren())
            child->setAlpha (dimmed ? 0.4f : 1.0f);
        for (auto& dial : dials)
            dial->slider.setTooltip (dimmed ? why : state.getParameter (dial->paramId)->getName (64));
        repaint();
    }
    bool isDimmed() const noexcept { return dimmed; }

    bool isBeingDragged() const noexcept { return dragging; }

    juce::Rectangle<float> headerBounds() const noexcept { return getLocalBounds().toFloat().withHeight (30.0f * uiScale); }
    juce::Rectangle<float> closeBounds() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - 30.0f * s, 5.0f * s, 20.0f * s, 20.0f * s };
    }
    juce::Rectangle<float> jackBounds (Jack which = Jack::out) const noexcept
    {
        const float s = uiScale;
        if (which == Jack::out)
            return jacks.out ? juce::Rectangle<float> ((float) getWidth() - 56.0f * s, 6.0f * s, 18.0f * s, 18.0f * s)
                             : juce::Rectangle<float>();
        return jacks.in ? juce::Rectangle<float> ((float) getWidth() - 108.0f * s, 6.0f * s, 18.0f * s, 18.0f * s)
                        : juce::Rectangle<float>();
    }

    juce::Slider& getDial (int index) { return dials[(size_t) index]->slider; }
    int getNumDials() const noexcept  { return (int) dials.size(); }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  card = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = 14.0f * s;

        juce::ColourGradient fill (Palette::panelTop.brighter (0.06f), 0.0f, card.getY(),
                                   Palette::panelBottom.brighter (0.04f), 0.0f, card.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (card, radius);
        g.setColour (dragging ? Palette::accent.withAlpha (0.6f) : Palette::strongLine);
        g.drawRoundedRectangle (card, radius, juce::jmax (1.0f, s));

        paintHeader (g);

        g.setColour (Palette::hairline);
        g.fillRect (headerBounds().withTop (headerBounds().getBottom() - juce::jmax (1.0f, s)).reduced (12.0f * s, 0.0f));

        paintDialLabels (g);
    }

    void resized() override
    {
        const float s = uiScale;
        const auto px = [s] (float v) { return juce::roundToInt (v * s); };

        auto area = getLocalBounds().withTrimmedTop (px (30.0f)).reduced (px (12.0f), 0);
        area.removeFromTop (px (8.0f));
        layoutWell (area.removeFromTop (px (96.0f)));

        // Rows of (up to) three dials across the card, each with its name and
        // value beneath.
        constexpr size_t perRow = 3;
        for (size_t first = 0; first < dials.size(); first += perRow)
        {
            const size_t last = juce::jmin (dials.size(), first + perRow);
            const int columnWidth = area.getWidth() / (int) (last - first);
            area.removeFromTop (px (12.0f));
            auto band = area.removeFromTop (px (52.0f + 31.0f));
            for (size_t i = first; i < last; ++i)
            {
                auto column = i + 1 == last ? band : band.removeFromLeft (columnWidth);
                dials[i]->column = column;
                dials[i]->slider.setBounds (column.withHeight (px (52.0f)).withSizeKeepingCentre (px (52.0f), px (52.0f)));
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        heldJack.reset();
        for (const auto which : { Jack::out, Jack::in })
            if (jackBounds (which).contains (e.position))
            {
                heldJack = which;
                if (jackHandler != nullptr)
                    jackHandler (which, e, jackDown);
                return;
            }
        if (closeBounds().contains (e.position))
        {
            if (removeHandler != nullptr)
                removeHandler();
            return;
        }
        if (headerBounds().contains (e.position))
        {
            dragging = true;
            dragStart = getPosition();
            toFront (false);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (heldJack.has_value())
        {
            if (jackHandler != nullptr)
                jackHandler (*heldJack, e, jackDrag);
            return;
        }
        if (! dragging)
            return;

        auto* parent = getParentComponent();
        auto topLeft = dragStart + e.getOffsetFromDragStart();
        if (parent != nullptr)
            topLeft = juce::Point<int> (juce::jlimit (0, juce::jmax (0, parent->getWidth() - getWidth()), topLeft.x),
                                        juce::jlimit (0, juce::jmax (0, parent->getHeight() - getHeight()), topLeft.y));
        setTopLeftPosition (topLeft);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (heldJack.has_value())
        {
            const auto which = *heldJack;
            heldJack.reset();
            if (jackHandler != nullptr)
                jackHandler (which, e, jackUp);
            return;
        }
        if (! dragging)
            return;
        dragging = false;
        repaint();
        if (movedHandler != nullptr)
            movedHandler (getPosition());
    }

    juce::MouseCursor getMouseCursor() override
    {
        const auto mouse = getMouseXYRelative().toFloat();
        if (jackBounds (Jack::out).contains (mouse) || jackBounds (Jack::in).contains (mouse) || closeBounds().contains (mouse))
            return juce::MouseCursor::PointingHandCursor;
        if (headerBounds().contains (mouse))
            return juce::MouseCursor::DraggingHandCursor;
        return juce::MouseCursor::NormalCursor;
    }

protected:
    /** Gives the module a dial for `paramId`, named `name`, in `colour`. */
    void addDial (const char* paramId, const char* name, juce::Colour colour)
    {
        auto dial = std::make_unique<Dial>();
        dial->paramId = paramId;
        dial->name    = name;
        dial->colour  = colour;
        BenchStyle::setUpDial (dial->slider, colour, looks, state, paramId);
        // The names and values are painted under the dials, so keep them in
        // step during a drag rather than trailing by a timer tick.
        dial->slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (dial->slider);
        dial->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, dial->slider);
        dials.push_back (std::move (dial));
    }

    /** Lays the module's display well (and anything over it) out in `well`. */
    virtual void layoutWell (juce::Rectangle<int> well) = 0;

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
    };

    // Which jacks show (when sound follows the cables), what is plugged in,
    // and the colour they light in.
    struct Jacks
    {
        bool out        = false;
        bool in         = false;
        bool outPlugged = false;
        bool inPlugged  = false;
        juce::Colour colour { Synth2::Palette::accent };
    };

    // Header: the order (following the layout), the name, what it shapes,
    // the jacks, the cross.
    void paintHeader (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto  mouse = getMouseXYRelative().toFloat();
        const bool  over  = isMouseOver (true);

        auto header = headerBounds().reduced (12.0f * s, 0.0f);
        if (order > 0)
        {
            const auto badge = header.removeFromLeft (18.0f * s).withSizeKeepingCentre (18.0f * s, 18.0f * s);
            g.setColour (Palette::accentSoft);
            g.fillEllipse (badge);
            g.setColour (Palette::accent);
            g.setFont (Fonts::mono (10.0f * s));
            g.drawText (juce::String (order), badge, juce::Justification::centred, false);
            header.removeFromLeft (6.0f * s);
        }
        g.setFont (Fonts::sans (10.0f * s, true, 0.22f));
        g.setColour (dimmed ? Palette::textFaint : Palette::textMid);
        g.drawText (title, header, juce::Justification::centredLeft, false);

        if (tag.isNotEmpty())
        {
            g.setFont (Fonts::sans (9.0f * s, true, 0.14f));
            g.setColour (dimmed ? Palette::textFaint : Palette::accent);
            g.drawText (tag, header.withRight (closeBounds().getX() - 8.0f * s), juce::Justification::centredRight, false);
        }

        const auto jack = [&] (Jack which, const char* label, bool plugged)
        {
            const auto area = jackBounds (which);
            g.setFont (Fonts::sans (8.0f * s, true, 0.22f));
            g.setColour (Palette::textFaint);
            g.drawText (label, area.withX (area.getX() - 28.0f * s).withWidth (24.0f * s), juce::Justification::centredRight, false);
            BenchStyle::paintJack (g, area, jacks.colour, plugged, over && area.contains (mouse), s);
        };
        if (jacks.in)
            jack (Jack::in, "IN", jacks.inPlugged);
        if (jacks.out)
            jack (Jack::out, "OUT", jacks.outPlugged);

        const auto cross = closeBounds();
        const bool crossHot = over && cross.contains (mouse);
        if (crossHot)
        {
            g.setColour (Palette::idleFill.withMultipliedAlpha (2.0f));
            g.fillEllipse (cross);
        }
        g.setColour (crossHot ? Palette::textStrong : Palette::textDim);
        const auto x = cross.reduced (6.5f * s);
        g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, juce::jmax (1.0f, 1.4f * s));
        g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, juce::jmax (1.0f, 1.4f * s));
    }

    // Dial convention: name + mono value stacked beneath the arc, the name in
    // grey, the value in the dial's own colour.
    void paintDialLabels (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        for (const auto& dial : dials)
        {
            const auto  column  = dial->column.toFloat();
            const float textTop = dial->slider.getBounds().toFloat().getBottom() + 6.0f * s;
            const float alpha   = dimmed ? 0.4f : 1.0f;

            g.setFont (Fonts::sans (10.0f * s, true, 0.10f));
            g.setColour (Palette::textMid.withMultipliedAlpha (alpha));
            g.drawText (dial->name, juce::Rectangle (column.getX(), textTop, column.getWidth(), 12.0f * s),
                        juce::Justification::centredTop, false);

            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (dial->colour.withMultipliedAlpha (alpha));
            g.drawText (BenchStyle::dialText (dial->paramId, state.getRawParameterValue (dial->paramId)->load()),
                        juce::Rectangle (column.getX(), textTop + 13.0f * s, column.getWidth(), 13.0f * s),
                        juce::Justification::centredTop, false);
        }
    }

    Bench::ModuleId id;
    juce::String    title;
    Synth2LookAndFeel& looks;
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<Dial>> dials;

    std::function<void()> removeHandler;
    std::function<void (juce::Point<int>)> movedHandler;
    std::function<void (Jack, const juce::MouseEvent&, int)> jackHandler;

    Jacks               jacks;
    std::optional<Jack> heldJack;   // the jack a press began on
    juce::String        tag;
    juce::String        dimmedWhy;
    juce::Point<int>    dragStart;
    float uiScale  = 1.0f;
    int   order    = 0;
    bool  dimmed   = false;
    bool  dragging = false;
};

/** An oscillator: its WAVE well, where the shape is picked too, over PITCH /
    FINE / LEVEL and its own unison stack, VOICES / DETUNE / WIDTH. */
class OscillatorModule final : public BenchModule
{
public:
    OscillatorModule (int slot, Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : BenchModule ({ Bench::Kind::oscillator, slot }, "OSC " + juce::String (slot + 1), looksToUse, stateToUse),
          picker (*stateToUse.getParameter (ParamID::oscillators[slot].wave))
    {
        display.setCaption ("WAVE");
        addAndMakeVisible (display);
        addAndMakeVisible (picker);
        const auto& ids = ParamID::oscillators[slot];
        addDial (ids.pitch,  "PITCH",  BenchStyle::dialColour (0.0f));
        addDial (ids.fine,   "FINE",   BenchStyle::dialColour (0.2f));
        addDial (ids.level,  "LEVEL",  BenchStyle::dialColour (0.4f));
        addDial (ids.voices, "VOICES", BenchStyle::dialColour (0.6f));
        addDial (ids.detune, "DETUNE", BenchStyle::dialColour (0.8f));
        addDial (ids.width,  "WIDTH",  BenchStyle::dialColour (1.0f));
        setJackColour (BenchStyle::dialColour (0.25f * (float) slot));
    }

    WaveDisplay& getDisplay() noexcept { return display; }
    WavePicker&  getPicker() noexcept  { return picker; }

    /** Shows the oscillator as it plays: its shape, with a faint copy for
        each of its unison voices, its pitch and its level. */
    void refresh (const Synth2Engine::OscillatorSettings& osc)
    {
        VoiceDisplayState shown;
        shown.wave   = osc.wave;
        shown.voices = Unison::clampVoices (osc.voices);
        shown.detune = osc.detuneCents;
        shown.width  = osc.width;
        display.setState (shown);
        display.setOsc ({ Synth2VisualModel::cyclesFor (osc.semitones, osc.fineCents), osc.level });
    }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        picker.setUiScale (getUiScale());
        display.setBounds (well);
        picker.setBounds (well);
    }

    WaveDisplay display;
    WavePicker  picker;
};

/** An envelope: its AMP well over ATTACK / RELEASE. It shapes one oscillator. */
class EnvelopeModule final : public BenchModule
{
public:
    EnvelopeModule (int slot, Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : BenchModule ({ Bench::Kind::envelope, slot }, "ENV " + juce::String (slot + 1), looksToUse, stateToUse)
    {
        addAndMakeVisible (display);
        display.setColours (BenchStyle::dialColour (0.6f), BenchStyle::dialColour (0.8f), BenchStyle::dialColour (1.0f));
        const auto& ids = ParamID::envelopes[slot];
        addDial (ids.attack,  "ATTACK",  BenchStyle::dialColour (0.6f));
        addDial (ids.release, "RELEASE", BenchStyle::dialColour (0.8f));
        setJackColour (BenchStyle::dialColour (0.6f + 0.1f * (float) slot));
    }

    AmpDisplay& getDisplay() noexcept { return display; }
    void refresh (const AmpDisplayState& amp) { display.setAmpState (amp); }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        display.setBounds (well);
    }

    AmpDisplay display;
};

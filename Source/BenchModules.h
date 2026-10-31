#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
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

    /** The bench's (and the effects') area: a dark rounded well with a faint
        dot grid, texture only, nothing snaps to it. */
    inline void paintArea (juce::Graphics& g, juce::Rectangle<float> area, float s)
    {
        using namespace Synth2;
        g.setColour (Palette::well.withAlpha (0.55f));
        g.fillRoundedRectangle (area, 16.0f * s);
        g.setColour (Palette::hairline);
        g.drawRoundedRectangle (area.reduced (0.5f), 16.0f * s, juce::jmax (1.0f, s));

        g.setColour (Palette::gridLine.withMultipliedAlpha (1.6f));
        const float spacing = 24.0f * s;
        const float dot     = juce::jmax (1.5f, 2.0f * s);
        const int   rows    = (int) std::ceil ((area.getHeight() - spacing * 1.5f) / spacing);
        const int   columns = (int) std::ceil ((area.getWidth()  - spacing * 1.5f) / spacing);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                g.fillEllipse (area.getX() + spacing * (float) (column + 1) - dot * 0.5f,
                               area.getY() + spacing * (float) (row + 1) - dot * 0.5f, dot, dot);
    }

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
            if (id == env.delay || id == env.attack || id == env.hold || id == env.decay || id == env.release)
                return Format::seconds (value);
        for (const auto& filter : ParamID::filters)
        {
            if (id == filter.cutoff)    return Format::hertz (value);
            if (id == filter.envAmount) return Format::semitones (juce::roundToInt (value));
        }
        if (id == ParamID::output)
            return Format::decibels (value);
        if (id == ParamID::delayTime || id == ParamID::reverbPreDelay)
            return juce::String (juce::roundToInt (value)) + " ms";
        if (id == ParamID::reverbSize)
            return Format::seconds ((float) Effects::reverbSeconds (value));
        if (id == ParamID::driveAmount)
            return "+" + juce::String (Effects::maxDriveDb * value, 1) + " dB";
        if (id == ParamID::delayNote)
            return Effects::divisions[(size_t) juce::jlimit (0, (int) Effects::divisions.size() - 1, juce::roundToInt (value))].name;
        return Format::percent (value);   // the levels, WIDTH, SUSTAIN and the effects' amounts
    }

    /** A dial's name and value, stacked in `under` (its column, from the
        dial's bottom down): the name in grey, the value in the dial's own
        colour. */
    inline void paintDialLabel (juce::Graphics& g, juce::Rectangle<float> under, const juce::String& name,
                                const juce::String& value, juce::Colour colour, float alpha, float s)
    {
        using namespace Synth2;
        const auto  column  = under;
        const float textTop = under.getY() + 6.0f * s;
        g.setFont (Fonts::sans (10.0f * s, true, 0.10f));
        g.setColour (Palette::textMid.withMultipliedAlpha (alpha));
        g.drawText (name, juce::Rectangle (column.getX(), textTop, column.getWidth(), 12.0f * s),
                    juce::Justification::centredTop, false);

        g.setFont (Fonts::mono (10.0f * s));
        g.setColour (colour.withMultipliedAlpha (alpha));
        g.drawText (value, juce::Rectangle (column.getX(), textTop + 13.0f * s, column.getWidth(), 13.0f * s),
                    juce::Justification::centredTop, false);
    }

    /** A dial in the house style, tied to its parameter. */
    inline void setUpDial (juce::Slider& dial, juce::Colour colour, juce::LookAndFeel& looks,
                           const juce::AudioProcessorValueTreeState& state, const char* paramId)
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
    PowerSwitch — a module's or an effect's power switch, at the left of its
    header: the power symbol in a ring, lit in its colour while it is on and
    grey while it is off. Tied to an on/off parameter.
*/
class PowerSwitch final : public juce::Button
{
public:
    PowerSwitch (juce::Colour onColour, juce::AudioProcessorValueTreeState& state, const char* paramId)
        : juce::Button ("Power"), colour (onColour), attachment (state, paramId, *this)
    {
        setClickingTogglesState (true);
        setComponentID (paramId);
        setTooltip ("Switch " + state.getParameter (paramId)->getName (64).upToLastOccurrenceOf (" ", false, false) + " on or off");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace Synth2;
        const float s    = (float) getHeight() / 18.0f;
        const auto  area = getLocalBounds().toFloat().reduced (1.0f * s);
        const bool  on   = getToggleState();
        auto tint = on ? colour : Palette::textFaint;
        if (highlighted || down)
            tint = on ? tint.brighter (0.25f) : Palette::textDim;

        if (on)
        {
            g.setColour (colour.withAlpha (0.18f));
            g.fillEllipse (area);
        }
        g.setColour (on ? colour.withAlpha (0.6f) : Palette::strongLine.withMultipliedAlpha (2.0f));
        g.drawEllipse (area, juce::jmax (1.0f, s));

        // The power symbol: a ring open at the top, and a stroke through the gap.
        const auto  c = area.getCentre();
        const float r = area.getWidth() * 0.27f;
        juce::Path symbol;
        symbol.addCentredArc (c.x, c.y, r, r, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
        symbol.startNewSubPath (c.x, c.y - r * 1.25f);
        symbol.lineTo (c.x, c.y - r * 0.15f);
        g.setColour (tint);
        g.strokePath (symbol, juce::PathStrokeType (juce::jmax (1.0f, 1.4f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    juce::Colour colour;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

/**
    ChoiceTray — a small floating list of choices, opened over the editor at
    a point: the bench's "add a module" menu and the gear menu's settings.

    Each choice has a name and a line of detail; a choice can be greyed out
    (it can't be picked) or ticked (the current one), and can start a new
    section under a heading of its own. Click a choice to pick
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
        juce::String heading = {};   // starts a new section under this heading
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
    const juce::String& getCaption() const noexcept { return title; }

    juce::Rectangle<float> trayBounds() const noexcept
    {
        const float s = uiScale;
        const float width  = 260.0f * s;
        const float height = (rowTop ((int) choices.size()) + 8.0f) * s;
        const auto  area   = getLocalBounds().toFloat().reduced (8.0f * s);
        return juce::Rectangle<float> ((float) origin.x, (float) origin.y, width, height).constrainedWithin (area);
    }

    juce::Rectangle<float> choiceBounds (int index) const noexcept
    {
        const float s = uiScale;
        return trayBounds().reduced (8.0f * s, 0.0f).withTrimmedTop (rowTop (index) * s).withHeight (44.0f * s);
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
            setHighlighted (stepHighlight (key == juce::KeyPress::downKey ? 1 : -1));
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
            if (choice.heading.isNotEmpty())
            {
                const auto above = row.withY (row.getY() - headingHeight * s).withHeight (headingHeight * s).reduced (8.0f * s, 0.0f);
                g.setColour (Palette::hairline);
                g.fillRect (above.withHeight (juce::jmax (1.0f, s)).translated (0.0f, 4.0f * s));
                g.setFont (Fonts::sans (9.0f * s, true, 0.22f));
                g.setColour (Palette::textFaint);
                g.drawText (choice.heading, above.withTrimmedTop (6.0f * s), juce::Justification::centredLeft, false);
            }
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
    static constexpr float headingHeight = 30.0f;

    // How far below the tray's top (design pixels) choice `index` starts:
    // under the title, the choices before it and any section headings.
    float rowTop (int index) const noexcept
    {
        float top = 34.0f + 44.0f * (float) index;
        const int upTo = juce::jmin (index + 1, (int) choices.size());
        for (int i = 0; i < upTo; ++i)
            if (choices[(size_t) i].heading.isNotEmpty())
                top += headingHeight;
        return top;
    }

    // The choice `direction` (1 down, -1 up) from the highlighted one,
    // wrapping round; down from none is the first.
    int stepHighlight (int direction) const noexcept
    {
        const int count = juce::jmax (1, (int) choices.size());
        if (highlighted < 0 && direction > 0)
            return 0;
        return (juce::jmax (0, highlighted) + direction + count) % count;
    }

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
    power switch, its name, which drags it about, and a cross that takes it
    off the bench) over its display well and its dials, in rows of three,
    their names and values beneath.

    Switched off, a module stays where it is with its settings, greyed out
    (an oscillator falls silent; an envelope lets its oscillator through
    unshaped).

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
                 juce::AudioProcessorValueTreeState& stateToUse, const char* activeParam, const juce::String& offWhy)
        : id (moduleId), title (moduleTitle), looks (looksToUse), state (stateToUse),
          power (Synth2::Palette::accent, stateToUse, activeParam)
    {
        setRepaintsOnMouseActivity (true);
        dim.offWhy = offWhy;
        addAndMakeVisible (power);
        power.onClick = [this] { showDim(); };
    }

    ~BenchModule() override
    {
        for (const auto& dial : dials)
            dial->slider.setLookAndFeel (nullptr);
    }

    const Bench::ModuleId& getId() const noexcept { return id; }
    const juce::String& getCaption() const noexcept { return title; }

    enum class Jack { out, in };
    enum class JackPhase { down, drag, up };

    /** What the header shows besides the module's name: its place in the
        order (when sound follows the layout; 0 shows none), a tag naming
        what it shapes, and, when sound follows the cables, its jacks: which
        show, what is plugged in, and the colour they light in. A change
        repaints the module. */
    class Header
    {
    public:
        explicit Header (juce::Component& moduleToRepaint) : owner (moduleToRepaint) {}

        void setOrder (int newOrder)                { if (order != newOrder) { order = newOrder; owner.repaint(); } }
        void setTag (const juce::String& newTag)    { if (tag != newTag) { tag = newTag; owner.repaint(); } }
        void setJacksShown (bool out, bool in)      { if (jacks.out != out || jacks.in != in) { jacks.out = out; jacks.in = in; owner.repaint(); } }
        void setPlugged (bool out, bool in)         { if (jacks.outPlugged != out || jacks.inPlugged != in) { jacks.outPlugged = out; jacks.inPlugged = in; owner.repaint(); } }
        void setJackColour (juce::Colour c)         { jacks.colour = c; }
        int  getOrder() const noexcept              { return order; }
        const juce::String& getTag() const noexcept { return tag; }
        bool isJackShown (Jack which = Jack::out) const noexcept { return which == Jack::out ? jacks.out : jacks.in; }
        bool isPlugged (Jack which) const noexcept  { return which == Jack::out ? jacks.outPlugged : jacks.inPlugged; }
        juce::Colour getJackColour() const noexcept { return jacks.colour; }

    private:
        struct Jacks
        {
            bool out        = false;
            bool in         = false;
            bool outPlugged = false;
            bool inPlugged  = false;
            juce::Colour colour { Synth2::Palette::accent };
        };

        juce::Component& owner;
        juce::String     tag;
        Jacks            jacks;
        int              order = 0;
    };

    /** The cross was clicked. */
    void onRemove (std::function<void()> handler) { removeHandler = std::move (handler); }
    /** The header was dragged and let go, the module's top left now here (in its parent). */
    void onMoved (std::function<void (juce::Point<int>)> handler) { movedHandler = std::move (handler); }
    /** A jack was pressed, dragged or let go (the event is the module's). */
    void onJack (std::function<void (Jack, const juce::MouseEvent&, JackPhase)> handler) { jackHandler = std::move (handler); }
    /** Where in its parent a drag can take the module (all of the parent when empty). */
    void setDragArea (juce::Rectangle<int> area) noexcept { dragArea = area; }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        resized();
        repaint();
    }

    /** Shows the card as the gear menu's view says: with or without its
        well or its dials (see Bench::View). Its height follows (cardHeight). */
    void setView (const Bench::View& newView)
    {
        if (newView == view)
            return;
        view = newView;
        const bool withDials = Bench::showsDials (id.kind, view);
        for (const auto& dial : dials)
            dial->slider.setVisible (withDials);
        resized();
        repaint();
    }
    const Bench::View& getView() const noexcept { return view; }

    /** How tall the card is drawn (design pixels) in its view. */
    float cardHeight() const noexcept { return Bench::cardHeightOf (id.kind, view); }

    Header&       getHeader() noexcept       { return headerState; }
    const Header& getHeader() const noexcept { return headerState; }

    /** Greys the module's insides out because of how sound flows (not
        heard, or not shaping anything), saying why in the dials' tooltips.
        A module switched off is greyed out whatever this says. */
    void setDimmed (bool shouldBeDimmed, const juce::String& why = {})
    {
        dim.flow    = shouldBeDimmed;
        dim.flowWhy = why;
        showDim();
    }
    bool isDimmed() const noexcept { return dim.shown; }

    PowerSwitch& getPower() noexcept { return power; }

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
            return headerState.isJackShown (Jack::out) ? juce::Rectangle<float> ((float) getWidth() - 56.0f * s, 6.0f * s, 18.0f * s, 18.0f * s)
                             : juce::Rectangle<float>();
        return headerState.isJackShown (Jack::in) ? juce::Rectangle<float> ((float) getWidth() - 108.0f * s, 6.0f * s, 18.0f * s, 18.0f * s)
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

        power.setBounds (px (10.0f), px (6.0f), px (18.0f), px (18.0f));

        auto area = getLocalBounds().withTrimmedTop (px (30.0f)).reduced (px (12.0f), 0);
        area.removeFromTop (px (8.0f));
        layoutWell (area.removeFromTop (px (Bench::wellHeightOf (id.kind, view))));
        if (! Bench::showsDials (id.kind, view))
            return;

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
                    jackHandler (which, e, JackPhase::down);
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
                jackHandler (*heldJack, e, JackPhase::drag);
            return;
        }
        if (! dragging)
            return;

        const auto* parent = getParentComponent();
        auto topLeft = dragStart + e.getOffsetFromDragStart();
        if (const auto area = dragArea.isEmpty() && parent != nullptr ? parent->getLocalBounds() : dragArea; ! area.isEmpty())
            topLeft = juce::Point<int> (juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - getWidth()), topLeft.x),
                                        juce::jlimit (area.getY(), juce::jmax (area.getY(), area.getBottom() - getHeight()), topLeft.y));
        setTopLeftPosition (topLeft);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (heldJack.has_value())
        {
            const auto which = *heldJack;
            heldJack.reset();
            if (jackHandler != nullptr)
                jackHandler (which, e, JackPhase::up);
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
        showDim (true);
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

    // Header: the order (following the layout), the name, what it shapes,
    // the jacks, the cross.
    void paintHeader (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto  mouse = getMouseXYRelative().toFloat();
        const bool  over  = isMouseOver (true);

        auto header = headerBounds().reduced (12.0f * s, 0.0f);
        header.removeFromLeft (26.0f * s);   // the power switch
        if (const int order = headerState.getOrder(); order > 0)
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
        g.setColour (dim.shown ? Palette::textFaint : Palette::textMid);
        g.drawText (title, header, juce::Justification::centredLeft, false);

        if (const auto& tag = headerState.getTag(); tag.isNotEmpty())
        {
            g.setFont (Fonts::sans (9.0f * s, true, 0.14f));
            g.setColour (dim.shown ? Palette::textFaint : Palette::accent);
            g.drawText (tag, header.withRight (closeBounds().getX() - 8.0f * s), juce::Justification::centredRight, false);
        }

        const auto jack = [&] (Jack which, const char* label)
        {
            const auto area = jackBounds (which);
            g.setFont (Fonts::sans (8.0f * s, true, 0.22f));
            g.setColour (Palette::textFaint);
            g.drawText (label, area.withX (area.getX() - 28.0f * s).withWidth (24.0f * s), juce::Justification::centredRight, false);
            BenchStyle::paintJack (g, area, headerState.getJackColour(), headerState.isPlugged (which), over && area.contains (mouse), s);
        };
        if (headerState.isJackShown (Jack::in))
            jack (Jack::in, "IN");
        if (headerState.isJackShown (Jack::out))
            jack (Jack::out, "OUT");

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
        for (const auto& dial : dials)
            if (dial->slider.isVisible())
                BenchStyle::paintDialLabel (g, dial->column.toFloat().withTop (dial->slider.getBounds().toFloat().getBottom()), dial->name,
                                            BenchStyle::dialText (dial->paramId, state.getRawParameterValue (dial->paramId)->load()),
                                            dial->colour, dim.shown ? 0.4f : 1.0f, uiScale);
    }

    Bench::ModuleId id;
    juce::String    title;
    Synth2LookAndFeel& looks;
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<Dial>> dials;

    std::function<void()> removeHandler;
    std::function<void (juce::Point<int>)> movedHandler;
    std::function<void (Jack, const juce::MouseEvent&, JackPhase)> jackHandler;

    // Why the module is greyed out: switched off, or left out of the flow.
    struct Dim
    {
        bool         flow  = false;
        bool         shown = false;
        juce::String flowWhy;
        juce::String offWhy;
        juce::String shownWhy;
    };

    // Greys the insides out (all but the power switch) when the module is
    // switched off or out of the flow; being off is the reason given.
    // `again` reapplies it to children added since.
    void showDim (bool again = false)
    {
        const bool   off = ! power.getToggleState();
        const bool   now = off || dim.flow;
        const auto&  why = off ? dim.offWhy : dim.flowWhy;
        if (! again && now == dim.shown && why == dim.shownWhy)
            return;
        dim.shown    = now;
        dim.shownWhy = why;
        for (auto* child : getChildren())
            if (child != &power)
                child->setAlpha (now ? 0.4f : 1.0f);
        for (const auto& dial : dials)
            dial->slider.setTooltip (now ? why : state.getParameter (dial->paramId)->getName (64));
        repaint();
    }

    PowerSwitch         power;
    Header              headerState { *this };
    std::optional<Jack> heldJack;   // the jack a press began on
    Dim                 dim;
    Bench::View         view;
    juce::Point<int>     dragStart;
    juce::Rectangle<int> dragArea;
    float uiScale  = 1.0f;
    bool  dragging = false;
};

/** An oscillator: its WAVE well, where the shape is picked too, over PITCH /
    FINE / LEVEL and its own unison stack, VOICES / DETUNE / WIDTH. */
class OscillatorModule final : public BenchModule
{
public:
    OscillatorModule (int slot, Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : BenchModule ({ Bench::Kind::oscillator, slot }, "OSC " + juce::String (slot + 1), looksToUse, stateToUse,
                       ParamID::oscillator (slot).active, "Switched off: silent until you switch it back on"),
          picker (*stateToUse.getParameter (ParamID::oscillator (slot).wave))
    {
        display.setCaption ("WAVE");
        addAndMakeVisible (display);
        addAndMakeVisible (picker);
        const auto& ids = ParamID::oscillator (slot);
        addDial (ids.pitch,  "PITCH",  BenchStyle::dialColour (0.0f));
        addDial (ids.fine,   "FINE",   BenchStyle::dialColour (0.2f));
        addDial (ids.level,  "LEVEL",  BenchStyle::dialColour (0.4f));
        addDial (ids.voices, "VOICES", BenchStyle::dialColour (0.6f));
        addDial (ids.detune, "DETUNE", BenchStyle::dialColour (0.8f));
        addDial (ids.width,  "WIDTH",  BenchStyle::dialColour (1.0f));
        getHeader().setJackColour (BenchStyle::dialColour (0.25f * (float) slot));
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
        const bool compact = getView().oscillators == Bench::OscillatorView::compact;
        display.setUiScale (getUiScale());
        picker.setUiScale (getUiScale());
        picker.setCompact (compact);
        display.setVisible (! compact);
        display.setBounds (well);
        picker.setBounds (well);
    }

    WaveDisplay display;
    WavePicker  picker;
};

/** An envelope: its AMP well over DELAY / ATTACK / HOLD and DECAY / SUSTAIN /
    RELEASE. It shapes one oscillator. */
class EnvelopeModule final : public BenchModule
{
public:
    EnvelopeModule (int slot, Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : BenchModule ({ Bench::Kind::envelope, slot }, "ENV " + juce::String (slot + 1), looksToUse, stateToUse,
                       ParamID::envelope (slot).active, "Switched off: its oscillator plays as if it had no envelope")
    {
        addAndMakeVisible (display);
        const auto& ids = ParamID::envelope (slot);
        const std::array<std::pair<const char*, const char*>, 6> stages { {
            { ids.delay, "DELAY" }, { ids.attack, "ATTACK" }, { ids.hold, "HOLD" },
            { ids.decay, "DECAY" }, { ids.sustain, "SUSTAIN" }, { ids.release, "RELEASE" } } };
        std::array<juce::Colour, 6> colours;
        for (size_t i = 0; i < stages.size(); ++i)
        {
            colours[i] = BenchStyle::dialColour (0.2f * (float) i);
            addDial (stages[i].first, stages[i].second, colours[i]);
        }
        display.setColours (colours);
        AmpDisplay::Parameters parameters {};
        for (size_t i = 0; i < stages.size(); ++i)
            parameters[i] = stateToUse.getParameter (stages[i].first);
        parameters[6] = stateToUse.getParameter (ids.attackCurve);
        parameters[7] = stateToUse.getParameter (ids.decayCurve);
        parameters[8] = stateToUse.getParameter (ids.releaseCurve);
        display.setParameters (parameters);
        getHeader().setJackColour (BenchStyle::dialColour (0.6f + 0.1f * (float) slot));
    }

    AmpDisplay& getDisplay() noexcept { return display; }
    void refresh (const AmpDisplayState& amp) { display.setAmpState (amp); }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        display.setUiScale (getUiScale());
        display.setVisible (! well.isEmpty());
        display.setBounds (well);
    }

    AmpDisplay display;
};

/** A filter: its response well, where TYPE is picked and the cutoff and
    resonance dragged, over CUTOFF / RESONANCE / ENV AMT, and its SWEEP well,
    the envelope that moves the cutoff, edited like an AMP well. It filters
    one oscillator. */
class FilterModule final : public BenchModule
{
public:
    FilterModule (int slot, Synth2LookAndFeel& looksToUse, juce::AudioProcessorValueTreeState& stateToUse)
        : BenchModule ({ Bench::Kind::filter, slot }, "FILTER " + juce::String (slot + 1), looksToUse, stateToUse,
                       ParamID::filter (slot).active, "Switched off: its oscillator plays unfiltered")
    {
        addAndMakeVisible (response);
        addAndMakeVisible (sweep);
        const auto& ids = ParamID::filter (slot);
        const auto cutoffColour    = BenchStyle::dialColour (0.1f);
        const auto resonanceColour = BenchStyle::dialColour (0.45f);
        addDial (ids.cutoff,    "CUTOFF",    cutoffColour);
        addDial (ids.resonance, "RESONANCE", resonanceColour);
        addDial (ids.envAmount, "ENV AMT",   BenchStyle::dialColour (0.8f));
        response.setColours (cutoffColour, resonanceColour);
        response.setParameters ({ stateToUse.getParameter (ids.type), stateToUse.getParameter (ids.cutoff),
                                  stateToUse.getParameter (ids.resonance) });

        const auto& stages = ids.sweep;
        const std::array<const char*, 6> stageIds { stages.delay, stages.attack, stages.hold, stages.decay, stages.sustain, stages.release };
        std::array<juce::Colour, 6> colours;
        AmpDisplay::Parameters parameters {};
        for (size_t i = 0; i < stageIds.size(); ++i)
        {
            colours[i]    = BenchStyle::dialColour (0.5f + 0.1f * (float) i);
            parameters[i] = stateToUse.getParameter (stageIds[i]);
        }
        parameters[6] = stateToUse.getParameter (stages.attackCurve);
        parameters[7] = stateToUse.getParameter (stages.decayCurve);
        parameters[8] = stateToUse.getParameter (stages.releaseCurve);
        sweep.setColours (colours);
        sweep.setParameters (parameters);
        sweep.setCaption ("SWEEP");
        getHeader().setJackColour (BenchStyle::dialColour (0.3f + 0.1f * (float) slot));
    }

    FilterDisplay& getResponse() noexcept { return response; }
    AmpDisplay&    getSweep() noexcept    { return sweep; }

    /** Shows the filter as it plays, and its sweep with a dot per note. */
    void refresh (const FilterDisplayState& filter, const AmpDisplayState& envelope)
    {
        response.setFilterState (filter);
        sweep.setAmpState (envelope);
        sweep.setCaption ("SWEEP", "ENV " + Synth2::Format::semitones (juce::roundToInt (filter.envAmount)));
    }

    void resized() override
    {
        BenchModule::resized();
        // The SWEEP well fills the room under the row of dials.
        const float s  = getUiScale();
        const auto  px = [s] (float v) { return juce::roundToInt (v * s); };
        const int   top = px (30.0f + 8.0f + Bench::filterWellHeight + 12.0f + 52.0f + 31.0f + 8.0f);
        sweep.setUiScale (s);
        sweep.setBounds (juce::Rectangle<int> (px (12.0f), top, getWidth() - px (24.0f), getHeight() - px (12.0f) - top));
    }

private:
    void layoutWell (juce::Rectangle<int> well) override
    {
        response.setUiScale (getUiScale());
        response.setBounds (well);
    }

    FilterDisplay response;
    AmpDisplay    sweep;
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>

#include "Arp.h"
#include "ArpModel.h"
#include "BenchModules.h"
#include "EffectsView.h"
#include "ParamIDs.h"
#include "Synth2LookAndFeel.h"
#include "Synth2Theme.h"

/**
    ArpGrid — the arp's pattern, drawn to be drawn on: a column for each
    step, a row for each place in the chord (the lowest key held at the
    bottom), and under them each step's velocity and length.

    Click a cell to put a note there or take it out, and drag to paint more
    the same way; drag across the velocity bars to set them; click a
    step's length to go through short, half, long and tied (dragging on
    sets the steps passed over the same). Click a step's number to end the
    pattern there. While it plays, the step playing lights up, and the rows
    are labelled with the notes the keys held give them.
*/
class ArpGrid final : public juce::Component
{
public:
    static constexpr float labelWidth   = 64.0f;   // design pixels
    static constexpr float numbersHeight = 22.0f;
    static constexpr float rowHeight    = 38.0f;
    static constexpr float laneGap      = 14.0f;
    static constexpr float velocityHeight = 76.0f;
    static constexpr float lengthHeight = 28.0f;

    explicit ArpGrid (juce::AudioProcessorValueTreeState& stateToUse) : model (stateToUse)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    /** The pattern to show (as the state has it). */
    void setPattern (const Arp::Pattern& newPattern)
    {
        if (newPattern == pattern)
            return;
        pattern = newPattern;
        repaint();
    }

    const Arp::Pattern& getPattern() const noexcept { return pattern; }

    /** The step playing (-1: none) and the notes the rows play now. */
    void setPlaying (int step, const std::array<int, Arp::numRows>& notes)
    {
        if (step == playing && notes == rowNotes)
            return;
        playing  = step;
        rowNotes = notes;
        repaint();
    }

    // ---- Where everything is (in this component) ----------------------------------

    float columnWidth() const noexcept { return ((float) getWidth() - labelWidth * uiScale) / (float) Arp::maxSteps; }
    float columnX (int step) const noexcept { return labelWidth * uiScale + (float) step * columnWidth(); }
    float cellsTop() const noexcept { return (numbersHeight + 4.0f) * uiScale; }

    juce::Rectangle<float> numberBounds (int step) const noexcept
    {
        return { columnX (step), 0.0f, columnWidth(), numbersHeight * uiScale };
    }

    /** Row 0, the lowest key held, at the bottom. */
    juce::Rectangle<float> cellBounds (int step, int row) const noexcept
    {
        return { columnX (step), cellsTop() + (float) (Arp::numRows - 1 - row) * rowHeight * uiScale, columnWidth(), rowHeight * uiScale };
    }

    juce::Rectangle<float> velocityBounds (int step) const noexcept
    {
        const float top = cellsTop() + ((float) Arp::numRows * rowHeight + laneGap) * uiScale;
        return { columnX (step), top, columnWidth(), velocityHeight * uiScale };
    }

    juce::Rectangle<float> lengthBounds (int step) const noexcept
    {
        return velocityBounds (step).withY (velocityBounds (step).getBottom() + 8.0f * uiScale).withHeight (lengthHeight * uiScale);
    }

    /** How tall it is, in design pixels. */
    static constexpr float designHeight() noexcept
    {
        return numbersHeight + 4.0f + (float) Arp::numRows * rowHeight + laneGap + velocityHeight + 8.0f + lengthHeight;
    }

    // ---- Drawing on it ---------------------------------------------------------------

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = Dragging::none;
        const int step = stepAt (e.position.x);
        if (step < 0)
            return;
        if (numberBounds (step).contains (e.position))
        {
            pattern.length = step + 1;
            model.setLength (step + 1);
            repaint();
            return;
        }
        if (velocityBounds (step).contains (e.position))
        {
            dragging = Dragging::velocity;
            mouseDrag (e);
            return;
        }
        if (lengthBounds (step).contains (e.position))
        {
            auto changed = pattern[step];
            changed.length = (Arp::Length) (((int) changed.length + 1) % Arp::numLengths);
            lengthToPaint = changed.length;
            dragging = Dragging::length;
            lastStep = step;
            edit (step, changed);
            return;
        }
        if (const int row = rowAt (e.position.y); row >= 0)
        {
            paintOn  = ! pattern[step].plays (row);
            dragging = Dragging::cells;
            mouseDrag (e);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int step = juce::jlimit (0, Arp::maxSteps - 1, stepAt (juce::jlimit (labelWidth * uiScale, (float) getWidth() - 1.0f, e.position.x)));
        auto changed = pattern[step];
        switch (dragging)
        {
            case Dragging::cells:
                if (const int row = rowAt (e.position.y); row >= 0)
                {
                    const auto bit = (std::uint8_t) (1u << row);
                    changed.rows = paintOn ? (std::uint8_t) (changed.rows | bit) : (std::uint8_t) (changed.rows & ~bit);
                }
                break;
            case Dragging::velocity:
            {
                const auto bar = velocityBounds (step);
                changed.velocity = juce::jlimit (1.0f / 127.0f, 1.0f, (bar.getBottom() - e.position.y) / bar.getHeight());
                break;
            }
            case Dragging::length:
                if (step == lastStep)
                    return;
                lastStep = step;
                changed.length = lengthToPaint;
                break;
            case Dragging::none:
                return;
        }
        if (changed != pattern[step])
            edit (step, changed);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = Dragging::none; }

    void paint (juce::Graphics& g) override
    {
        paintColumns (g);
        paintNumbers (g);
        paintLabels (g);
        for (int step = 0; step < Arp::maxSteps; ++step)
        {
            paintCells (g, step);
            paintVelocity (g, step);
            paintLength (g, step);
        }
    }

private:
    enum class Dragging { none, cells, velocity, length };

    // Shown at once, and into the state (where the arp reads it).
    void edit (int step, const Arp::Step& changed)
    {
        pattern[step] = changed;
        model.setStep (step, changed);
        repaint();
    }

    int stepAt (float x) const noexcept
    {
        if (x < labelWidth * uiScale || columnWidth() <= 0.0f)
            return -1;
        return juce::jlimit (0, Arp::maxSteps - 1, (int) ((x - labelWidth * uiScale) / columnWidth()));
    }

    int rowAt (float y) const noexcept
    {
        const float fromTop = (y - cellsTop()) / (rowHeight * uiScale);
        if (fromTop < 0.0f || fromTop >= (float) Arp::numRows)
            return -1;
        return Arp::numRows - 1 - (int) fromTop;
    }

    static juce::Colour rowColour (int row) { return BenchStyle::dialColour ((float) row / (float) (Arp::numRows - 1)); }

    bool inPattern (int step) const noexcept { return step < pattern.length; }

    // Each beat's four steps a shade apart, the pattern's end marked, and
    // the step playing lit.
    void paintColumns (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        const float top    = cellsTop();
        const float bottom = lengthBounds (0).getBottom();
        for (int step = 0; step < Arp::maxSteps; ++step)
        {
            const juce::Rectangle column (columnX (step), top, columnWidth(), bottom - top);
            if ((step / 4) % 2 == 0)
            {
                g.setColour (Palette::idleFill.withMultipliedAlpha (0.5f));
                g.fillRect (column);
            }
            if (step == playing)
            {
                g.setColour (Palette::accent.withAlpha (0.14f));
                g.fillRoundedRectangle (column.withTop (0.0f), 6.0f * s);
            }
        }
        const float end = columnX (pattern.length);
        g.setColour (Palette::accent.withAlpha (0.7f));
        g.fillRect (juce::Rectangle (end - 1.0f * s, 0.0f, juce::jmax (1.0f, 2.0f * s), bottom));
    }

    void paintNumbers (juce::Graphics& g) const
    {
        using namespace Synth2;
        g.setFont (Fonts::mono (10.0f * uiScale));
        for (int step = 0; step < Arp::maxSteps; ++step)
        {
            auto colour = inPattern (step) ? Palette::textMid : Palette::textFaint;
            if (step == playing)
                colour = Palette::accent;
            g.setColour (colour);
            g.drawText (juce::String (step + 1), numberBounds (step), juce::Justification::centred, false);
        }
    }

    // The rows' names: the notes they play with the keys held, or their
    // places in the chord with none.
    void paintLabels (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        for (int row = 0; row < Arp::numRows; ++row)
        {
            const auto box = cellBounds (0, row).withX (0.0f).withWidth ((labelWidth - 10.0f) * s);
            const int  note = rowNotes[(size_t) row];
            g.setFont (note >= 0 ? Fonts::mono (11.0f * s) : Fonts::sans (11.0f * s, true));
            g.setColour (note >= 0 ? rowColour (row) : Palette::textDim);
            g.drawText (note >= 0 ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::String (row + 1),
                        box, juce::Justification::centredRight, false);
        }
        g.setFont (Fonts::sans (9.0f * s, true, 0.14f));
        g.setColour (Palette::textFaint);
        g.drawText ("VELOCITY", velocityBounds (0).withX (0.0f).withWidth ((labelWidth - 10.0f) * s), juce::Justification::centredRight, false);
        g.drawText ("LENGTH", lengthBounds (0).withX (0.0f).withWidth ((labelWidth - 10.0f) * s), juce::Justification::centredRight, false);
    }

    void paintCells (juce::Graphics& g, int step) const
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto& value = pattern[step];
        const float alpha = inPattern (step) ? 1.0f : 0.35f;
        for (int row = 0; row < Arp::numRows; ++row)
        {
            const auto cell = cellBounds (step, row).reduced (2.5f * s);
            if (! value.plays (row))
            {
                g.setColour (Palette::idleFill.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (cell, 5.0f * s);
                continue;
            }
            const auto colour = rowColour (row);
            // A tie runs into the next step's cell, if it plays this row too.
            auto lit = cell;
            if (value.length == Arp::Length::tie && step + 1 < Arp::maxSteps && pattern[step + 1].plays (row))
                lit = lit.withRight (cellBounds (step + 1, row).getX() + 2.5f * s + 5.0f * s);
            g.setColour (colour.withAlpha ((0.10f + 0.18f * value.velocity) * alpha));
            g.fillRoundedRectangle (lit.expanded (2.0f * s), 7.0f * s);
            g.setColour (colour.withAlpha ((0.45f + 0.55f * value.velocity) * alpha));
            g.fillRoundedRectangle (lit, 5.0f * s);
        }
    }

    void paintVelocity (juce::Graphics& g, int step) const
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto& value = pattern[step];
        const auto  box   = velocityBounds (step).reduced (6.0f * s, 2.0f * s);
        const float alpha = inPattern (step) ? 1.0f : 0.35f;
        g.setColour (Palette::idleFill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, 3.0f * s);
        int lowest = 0;
        while (lowest < Arp::numRows - 1 && ! value.plays (lowest))
            ++lowest;
        const auto colour = value.isRest() ? Palette::textFaint : rowColour (lowest);
        g.setColour (colour.withAlpha ((value.isRest() ? 0.35f : 0.8f) * alpha));
        g.fillRoundedRectangle (box.withTop (box.getBottom() - box.getHeight() * value.velocity), 3.0f * s);
    }

    void paintLength (juce::Graphics& g, int step) const
    {
        using namespace Synth2;
        const float s     = uiScale;
        const auto& value = pattern[step];
        const auto  box   = lengthBounds (step).reduced (3.0f * s, 8.0f * s);
        const float alpha = inPattern (step) ? 1.0f : 0.35f;
        g.setColour (Palette::idleFill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        const bool tie = value.length == Arp::Length::tie;
        auto bar = box.withWidth (box.getWidth() * (float) Arp::gateOf (value.length));
        if (tie)
            bar = bar.withRight (box.getRight() + 6.0f * s);
        g.setColour ((tie ? Palette::pink : Palette::textDim).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bar, box.getHeight() * 0.5f);
    }

    Arp::Model   model;
    Arp::Pattern pattern;
    std::array<int, Arp::numRows> rowNotes { -1, -1, -1, -1, -1, -1, -1, -1 };
    int   playing  = -1;
    float uiScale  = 1.0f;

    Dragging     dragging    = Dragging::none;
    bool         paintOn     = true;
    Arp::Length  lengthToPaint = Arp::Length::half;
    int          lastStep    = -1;
};

/** A button in the arp's row of patterns: a pill, lit while the mouse is over it. */
class ArpButton final : public juce::Button
{
public:
    ArpButton (const juce::String& name, juce::Colour colourToUse) : juce::Button (name), colour (colourToUse)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  pill = getLocalBounds().toFloat().reduced (0.5f);
        const float r    = pill.getHeight() * 0.5f;
        g.setColour (highlighted || down ? colour.withAlpha (0.16f) : Palette::idleFill);
        g.fillRoundedRectangle (pill, r);
        g.setColour (highlighted || down ? colour.withAlpha (0.65f) : Palette::strongLine);
        g.drawRoundedRectangle (pill, r, juce::jmax (1.0f, s));
        g.setFont (Fonts::sans (9.5f * s, true, 0.14f));
        g.setColour (highlighted || down ? Palette::textStrong : Palette::textMid);
        g.drawText (getButtonText().toUpperCase(), pill, juce::Justification::centred, false);
    }

private:
    juce::Colour colour;
    float uiScale = 1.0f;
};

/**
    ArpView — the ARP tab: the arpeggiator, over the keyboard like the bench
    and the effects. Along the top its power switch, RATE (how long a step
    is), OCTAVES (how many the pattern climbs through, once round in each)
    and SWING; under them a row of patterns to start from (Up, Down,
    Up-Down, Random and Chord), Vary (a step or two changed), Clear and LATCH;
    and the grid itself (see ArpGrid), which plays while keys are held. The
    pattern is the state's (see Arp::Model), so it is saved with the
    session and in a user preset, and a factory preset can bring its own.
*/
class ArpView final : public juce::Component
{
public:
    ArpView (juce::AudioProcessorValueTreeState& stateToUse, Synth2LookAndFeel& looks)
        : model (stateToUse), grid (stateToUse),
          power (Synth2::Palette::amber, stateToUse, ParamID::arpOn),
          rate (*stateToUse.getParameter (ParamID::arpRate), rateNames(), Synth2::Palette::cyan),
          octaves (*stateToUse.getParameter (ParamID::arpOctaves), octaveNames(), Synth2::Palette::amber),
          latch ("LATCH", Synth2::Palette::pink, stateToUse, ParamID::arpLatch)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (grid);
        addAndMakeVisible (power);
        addAndMakeVisible (rate);
        addAndMakeVisible (octaves);
        addAndMakeVisible (latch);
        for (auto* control : std::initializer_list<juce::Component*> { &power, &latch })
        {
            control->setWantsKeyboardFocus (false);
            control->setMouseClickGrabsKeyboardFocus (false);
        }

        BenchStyle::setUpDial (swing, Synth2::Palette::violet, looks, stateToUse, ParamID::arpSwing);
        swing.onValueChange = [this] { repaint (swingLabelBounds()); };
        swing.setWantsKeyboardFocus (false);
        swing.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (swing);
        swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (stateToUse, ParamID::arpSwing, swing);

        for (size_t i = 0; i < shapes.size(); ++i)
        {
            auto& button = shapes[i];
            button.onClick = [this, i] { model.setPattern (Arp::make ((Arp::Shape) i, grid.getPattern().length, random)); };
            addAndMakeVisible (button);
        }
        vary.onClick  = [this] { model.setPattern (Arp::vary (grid.getPattern(), random)); };
        clear.onClick = [this]
        {
            Arp::Pattern empty;
            empty.length = grid.getPattern().length;
            model.setPattern (empty);
        };
        addAndMakeVisible (vary);
        addAndMakeVisible (clear);

        grid.setPattern (model.pattern());
    }

    ArpGrid&    getGrid() noexcept               { return grid; }
    ArpButton&  getShape (Arp::Shape shape)      { return shapes[(size_t) shape]; }
    ArpButton&  getVary() noexcept               { return vary; }
    ArpButton&  getClear() noexcept              { return clear; }
    PowerSwitch& getPower() noexcept             { return power; }
    SegmentedChoice& getRate() noexcept          { return rate; }
    SegmentedChoice& getOctaves() noexcept       { return octaves; }
    PillToggle& getLatch() noexcept              { return latch; }
    juce::Slider& getSwing() noexcept            { return swing; }

    /** Follows the pattern in the state, however it changed (the grid, a
        preset, a restored session), and the arp as it plays. From the
        editor's timer. */
    void refresh (const Arp::Player& player)
    {
        if (const auto pattern = model.pattern(); pattern != grid.getPattern())
        {
            grid.setPattern (pattern);
            repaint (footerBounds());
        }
        grid.setPlaying (player.playingStep(), player.rowNotes());
        if (const bool on = power.getToggleState(); on != shownOn)
        {
            shownOn = on;
            repaint();
        }
    }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        grid.setUiScale (newScale);
        rate.setUiScale (newScale);
        octaves.setUiScale (newScale);
        latch.setUiScale (newScale);
        for (auto& button : shapes)
            button.setUiScale (newScale);
        vary.setUiScale (newScale);
        clear.setUiScale (newScale);
        resized();
        repaint();
    }

    void resized() override
    {
        const float s  = uiScale;
        const auto  px = [s] (float x, float y, float w, float h) { return (juce::Rectangle<float> (x, y, w, h) * s).toNearestInt(); };

        power.setBounds (px (24.0f, 30.0f, 20.0f, 20.0f));
        rate.setBounds (px (300.0f, 40.0f, 8.0f * 44.0f + 7.0f * 6.0f, 26.0f));
        octaves.setBounds (px (718.0f, 40.0f, 4.0f * 34.0f + 3.0f * 6.0f, 26.0f));
        swing.setBounds (px (904.0f, 20.0f, 48.0f, 48.0f));
        latch.setBounds (px (872.0f, 112.0f, 96.0f, 26.0f));

        float x = 108.0f;
        for (auto& button : shapes)
        {
            button.setBounds (px (x, 112.0f, 76.0f, 26.0f));
            x += 84.0f;
        }
        vary.setBounds (px (x + 16.0f, 112.0f, 76.0f, 26.0f));
        clear.setBounds (px (x + 100.0f, 112.0f, 76.0f, 26.0f));

        grid.setBounds (px (24.0f, 160.0f, Bench::viewWidth - 48.0f, ArpGrid::designHeight()));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = uiScale;
        BenchStyle::paintArea (g, getLocalBounds().toFloat(), s);

        // The title, and what to do.
        g.setFont (Fonts::sans (15.0f * s, true, 0.04f));
        g.setColour (power.getToggleState() ? Palette::textStrong : Palette::textDim);
        g.drawText ("ARPEGGIATOR", juce::Rectangle (54.0f * s, 22.0f * s, 220.0f * s, 20.0f * s), juce::Justification::centredLeft, false);
        g.setFont (Fonts::sans (11.0f * s));
        g.setColour (Palette::textDim);
        g.drawText (power.getToggleState() ? "Hold keys to play the pattern" : "Switch it on, then hold keys",
                    juce::Rectangle (54.0f * s, 42.0f * s, 230.0f * s, 16.0f * s), juce::Justification::centredLeft, false);

        const auto label = [&g, s] (const juce::String& text, juce::Rectangle<float> box)
        {
            g.setFont (Fonts::sans (9.0f * s, true, 0.14f));
            g.setColour (Palette::textFaint);
            g.drawText (text, box * s, juce::Justification::centredLeft, false);
        };
        label ("RATE", { 300.0f, 22.0f, 100.0f, 14.0f });
        label ("OCTAVES", { 718.0f, 22.0f, 100.0f, 14.0f });
        label ("START FROM", { 24.0f, 112.0f, 84.0f, 26.0f });

        BenchStyle::paintDialLabel (g, swingLabelBounds().toFloat().withTrimmedTop (0.0f), "SWING",
                                    Format::percent ((float) swing.getValue()), Palette::violet, 1.0f, s);

        // Under the grid, how it reads, and how long the pattern is.
        const auto footer = footerBounds().toFloat();
        g.setFont (Fonts::sans (11.0f * s));
        g.setColour (Palette::textFaint);
        g.drawText ("The rows climb the chord you hold, 1 your lowest key, on into the octave above",
                    footer, juce::Justification::centredLeft, false);
        g.setColour (Palette::textDim);
        const int  length = grid.getPattern().length;
        const auto steps  = juce::String (length) + (length == 1 ? " step" : " steps");
        g.drawText (steps + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 click a number to end it there")),
                    footer, juce::Justification::centredRight, false);
    }

private:
    static std::vector<juce::String> octaveNames()
    {
        std::vector<juce::String> names;
        for (int count = 1; count <= Arp::maxOctaves; ++count)
            names.emplace_back (count);
        return names;
    }

    static std::vector<juce::String> rateNames()
    {
        std::vector<juce::String> names;
        for (const auto& r : Arp::rates)
            names.emplace_back (r.name);
        return names;
    }

    juce::Rectangle<int> swingLabelBounds() const
    {
        return swing.getBounds().withY (swing.getBottom()).withHeight (juce::roundToInt (32.0f * uiScale))
                                .withSizeKeepingCentre (juce::roundToInt (72.0f * uiScale), juce::roundToInt (32.0f * uiScale));
    }

    juce::Rectangle<int> footerBounds() const
    {
        const float top = 160.0f + ArpGrid::designHeight() + 12.0f;
        return (juce::Rectangle<float> (24.0f + ArpGrid::labelWidth, top, Bench::viewWidth - 48.0f - ArpGrid::labelWidth, 18.0f) * uiScale)
                   .toNearestInt();
    }

    Arp::Model model;
    ArpGrid    grid;
    PowerSwitch     power;
    SegmentedChoice rate;
    SegmentedChoice octaves;
    PillToggle      latch;
    juce::Slider    swing;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;
    std::array<ArpButton, 5> shapes {{ { Arp::shapeNames[0], Synth2::Palette::cyan },   { Arp::shapeNames[1], Synth2::Palette::cyan },
                                       { Arp::shapeNames[2], Synth2::Palette::violet }, { Arp::shapeNames[3], Synth2::Palette::pink },
                                       { Arp::shapeNames[4], Synth2::Palette::amber } }};
    ArpButton  vary  { "Vary", Synth2::Palette::accent };
    ArpButton  clear { "Clear", Synth2::Palette::textDim };
    juce::Random random;
    float uiScale = 1.0f;
    bool  shownOn = false;
};

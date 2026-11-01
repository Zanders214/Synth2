#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "Presets.h"
#include "Synth2Theme.h"
#include "WavePicker.h"

/**
    PresetBrowser — the factory presets, laid over the bench (or the effects)
    while the keyboard below stays playable, so each preset can be heard as
    it is picked.

    Down its left, the categories: ALL first, then each with how many presets
    it holds (one with none yet is greyed out). In the middle, the presets in
    the category picked, narrowed by the search box over them, which matches
    names, descriptions and categories, every word. On the right, the loaded
    preset: its category, name and description, its oscillators (each one's
    shape, pitch and stack, and its filter or envelope), how its notes play
    (POLY or MONO, and GLIDE) and the effects it goes through.

    A click on a preset loads it and keeps the browser open; a double-click
    loads it and closes it. The up and down arrow keys load the previous and
    next preset in the list, Return closes, and Escape clears the search or,
    with none, closes. Keys it has no use for go on to the editor, so the
    computer's keys still play. The wheel scrolls the list.
*/
class PresetBrowser final : public juce::Component,
                            private juce::KeyListener
{
public:
    static constexpr int all = -1;   // the category that shows every preset

    PresetBrowser()
    {
        using namespace Synth2;
        setVisible (false);
        setWantsKeyboardFocus (true);

        search.setTextToShowWhenEmpty ("Search presets", Palette::textFaint);
        search.setColour (juce::TextEditor::backgroundColourId, Palette::idleFill);
        search.setColour (juce::TextEditor::outlineColourId, Palette::hairline);
        search.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        search.setColour (juce::TextEditor::textColourId, Palette::textStrong);
        search.setColour (juce::TextEditor::highlightColourId, Palette::accentSoft.withMultipliedAlpha (3.0f));
        search.setColour (juce::CaretComponent::caretColourId, Palette::accent);
        search.setEscapeAndReturnKeysConsumed (false);
        search.onTextChange = [this] { refilter(); };
        search.addKeyListener (this);
        addAndMakeVisible (search);
        refilter();
    }

    ~PresetBrowser() override { search.removeKeyListener (this); }

    void onChoose (std::function<void (int)> handler) { chooseHandler = std::move (handler); }
    void onClose (std::function<void()> handler)      { closeHandler = std::move (handler); }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        search.applyFontToAllText (Synth2::Fonts::sans (13.0f * uiScale));
        search.setIndents (juce::roundToInt (12.0f * uiScale), juce::roundToInt (8.0f * uiScale));
        resized();
        repaint();
    }

    /** Opens on the list as it was left, with `loaded` (the preset loaded) in view. */
    void open (int loaded)
    {
        current = loaded;
        refilter();
        scrollToShow (current);
        juce::Desktop::getInstance().getAnimator().fadeIn (this, 80);   // --dur-fast
        toFront (false);
        if (isShowing())
            grabKeyboardFocus();
    }

    void close()
    {
        if (! isVisible())
            return;
        search.giveAwayKeyboardFocus();
        setVisible (false);
        if (closeHandler != nullptr)
            closeHandler();
    }

    bool isOpen() const noexcept { return isVisible(); }

    /** The loaded preset changed (stepped to with the picker's arrows, say). */
    void setCurrent (int index)
    {
        if (index == current)
            return;
        current = index;
        scrollToShow (current);
        repaint();
    }

    /** Shows `category` (all, or a Presets::Category), if it has any presets. */
    void showCategory (int category)
    {
        if (category == shown || (category != all && countIn (category) == 0))
            return;
        shown  = category;
        scroll = 0.0f;
        refilter();
        scrollToShow (current);
    }

    int getCategory() const noexcept { return shown; }
    int getCurrent() const noexcept  { return current; }
    juce::TextEditor& getSearch() noexcept { return search; }

    /** The presets listed, by program number, in the order they show. */
    const std::vector<int>& getListed() const noexcept { return listed; }

    /** Where the `row`th preset listed sits (it may be scrolled out of the list). */
    juce::Rectangle<float> rowBounds (int row) const noexcept
    {
        const float s = uiScale;
        const auto  list = listArea();
        return { list.getX(), list.getY() + ((float) row * rowHeight - scroll) * s, list.getWidth() - 10.0f * s, rowHeight * s };
    }

    /** Where `category`'s (all, or a Presets::Category) row sits. */
    juce::Rectangle<float> categoryBounds (int category) const noexcept
    {
        const float s = uiScale;
        return categoryArea().withTrimmedTop ((float) (category + 1) * categoryHeight * s).withHeight (categoryHeight * s);
    }

    juce::Rectangle<float> closeBounds() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - 48.0f * s, 12.0f * s, 32.0f * s, 32.0f * s };
    }

    /** The keys the browser answers to (see above); false for the rest. */
    bool handleKey (const juce::KeyPress& key)
    {
        if (! isOpen())
            return false;
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            step (key == juce::KeyPress::downKey ? 1 : -1);
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            close();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            if (search.isEmpty())
                close();
            else
                search.clear();
            refilter();
            return true;
        }
        return false;
    }

    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }

    void resized() override
    {
        const float s = uiScale;
        search.setBounds (listArea().getSmallestIntegerContainer().withY (juce::roundToInt (12.0f * s))
                                    .withHeight (juce::roundToInt (32.0f * s)).withWidth (juce::roundToInt (300.0f * s)));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const Hover now { rowAt (e.position), categoryAt (e.position), closeBounds().contains (e.position) };
        if (now.row != hover.row || now.category != hover.category || now.close != hover.close)
        {
            hover = now;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = {};
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (closeBounds().contains (e.position))
            close();
        else if (const auto category = categoryAt (e.position); category.has_value())
            showCategory (*category);
        else if (const int row = rowAt (e.position); row >= 0)
            choose (listed[(size_t) row]);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (rowAt (e.position) >= 0)
            close();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! listArea().contains (e.position))
            return;
        scrollTo (scroll - wheel.deltaY * 240.0f);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = uiScale;
        const auto  area = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (Palette::panelTop, 0.0f, 0.0f, Palette::panelBottom, 0.0f, area.getHeight() * 0.6f, false));
        g.fillRoundedRectangle (area, 16.0f * s);
        g.setColour (Palette::strongLine);
        g.drawRoundedRectangle (area.reduced (0.5f), 16.0f * s, juce::jmax (1.0f, s));

        // The top bar: the title, the search box (a component), the count and the close cross.
        g.setFont (Fonts::sans (10.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText ("PRESETS", juce::Rectangle (24.0f * s, 12.0f * s, 160.0f * s, 32.0f * s), juce::Justification::centredLeft, false);
        g.setFont (Fonts::mono (10.0f * s));
        g.drawText (juce::String ((int) listed.size()) + ((int) listed.size() == 1 ? " PRESET" : " PRESETS"),
                    juce::Rectangle ((float) search.getRight() + 16.0f * s, 12.0f * s, 160.0f * s, 32.0f * s), juce::Justification::centredLeft, false);
        paintClose (g, closeBounds());
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (16.0f * s, 56.0f * s, area.getWidth() - 32.0f * s, juce::jmax (1.0f, s)));

        paintCategories (g);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (listArea().getX() - 10.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        paintList (g);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (detailArea().getX() - 14.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        if (juce::isPositiveAndBelow (current, Presets::numPresets))
            paintDetail (g, Presets::factory[(size_t) current]);
    }

private:
    static constexpr float categoryHeight = 34.0f;
    static constexpr float rowHeight      = 54.0f;

    struct Hover
    {
        int row = -1;
        std::optional<int> category;
        bool close = false;
    };

    juce::Rectangle<float> categoryArea() const noexcept
    {
        const float s = uiScale;
        return { 16.0f * s, 68.0f * s, 176.0f * s, (float) getHeight() - 84.0f * s };
    }

    juce::Rectangle<float> listArea() const noexcept
    {
        const float s = uiScale;
        return { 212.0f * s, 68.0f * s, (float) getWidth() - 212.0f * s - 312.0f * s, (float) getHeight() - 84.0f * s };
    }

    juce::Rectangle<float> detailArea() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - 284.0f * s, 68.0f * s, 260.0f * s, (float) getHeight() - 92.0f * s };
    }

    static int countIn (int category) noexcept
    {
        int count = 0;
        for (const auto& preset : Presets::factory)
            count += (int) preset.category == category ? 1 : 0;
        return count;
    }

    // Whether `preset` has every word of the search in its name, description or category.
    bool matches (const Presets::Preset& preset) const
    {
        const auto words = juce::StringArray::fromTokens (search.getText(), true);
        const juce::String text = juce::String (preset.name) + " " + preset.detail + " " + Presets::nameOf (preset.category);
        for (const auto& word : words)
            if (word.isNotEmpty() && ! text.containsIgnoreCase (word))
                return false;
        return true;
    }

    // Lists the presets in the category showing that match the search: by
    // category, then in the order they were added.
    void refilter()
    {
        listed.clear();
        for (int category = 0; category < Presets::numCategories; ++category)
            if (shown == all || shown == category)
                for (int i = 0; i < Presets::numPresets; ++i)
                    if (const auto& preset = Presets::factory[(size_t) i]; (int) preset.category == category && matches (preset))
                        listed.push_back (i);
        hover.row = -1;
        scrollTo (scroll);
        repaint();
    }

    float maxScroll() const noexcept
    {
        return juce::jmax (0.0f, (float) listed.size() * rowHeight - listArea().getHeight() / uiScale);
    }

    void scrollTo (float to)
    {
        const float clamped = juce::jlimit (0.0f, maxScroll(), to);
        if (! juce::exactlyEqual (clamped, scroll))
        {
            scroll = clamped;
            repaint();
        }
    }

    // Scrolls preset `index` into the list's view, if it is listed.
    void scrollToShow (int index)
    {
        const auto at = std::find (listed.begin(), listed.end(), index);
        if (at == listed.end())
            return;
        const float top    = (float) (at - listed.begin()) * rowHeight;
        const float height = listArea().getHeight() / uiScale;
        if (top < scroll)
            scrollTo (top);
        else if (top + rowHeight > scroll + height)
            scrollTo (top + rowHeight - height);
    }

    void choose (int index)
    {
        current = index;
        scrollToShow (index);
        repaint();
        if (chooseHandler != nullptr)
            chooseHandler (index);
    }

    // Loads the preset `direction` (1 down, -1 up) from the loaded one in
    // the list, wrapping round; from one not listed, the first or last.
    void step (int direction)
    {
        if (listed.empty())
            return;
        const int count = (int) listed.size();
        const auto at = std::find (listed.begin(), listed.end(), current);
        const int row = at == listed.end() ? (direction > 0 ? 0 : count - 1)
                                           : ((int) (at - listed.begin()) + direction + count) % count;
        choose (listed[(size_t) row]);
    }

    int rowAt (juce::Point<float> position) const noexcept
    {
        if (! listArea().contains (position))
            return -1;
        for (int row = 0; row < (int) listed.size(); ++row)
            if (rowBounds (row).contains (position))
                return row;
        return -1;
    }

    std::optional<int> categoryAt (juce::Point<float> position) const noexcept
    {
        for (int category = all; category < Presets::numCategories; ++category)
            if (categoryBounds (category).contains (position))
                return category;
        return std::nullopt;
    }

    void paintClose (juce::Graphics& g, juce::Rectangle<float> box) const
    {
        using namespace Synth2;
        const float s = uiScale;
        if (hover.close)
        {
            g.setColour (Palette::idleFill.withMultipliedAlpha (1.6f));
            g.fillEllipse (box);
        }
        const auto c = box.getCentre();
        const float r = 5.0f * s;
        juce::Path cross;
        cross.startNewSubPath (c.x - r, c.y - r);
        cross.lineTo (c.x + r, c.y + r);
        cross.startNewSubPath (c.x + r, c.y - r);
        cross.lineTo (c.x - r, c.y + r);
        g.setColour (hover.close ? Palette::textStrong : Palette::textDim);
        g.strokePath (cross, juce::PathStrokeType (juce::jmax (1.0f, 1.6f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintCategories (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        for (int category = all; category < Presets::numCategories; ++category)
        {
            const auto row     = categoryBounds (category);
            const int  count   = category == all ? Presets::numPresets : countIn (category);
            const bool showing = category == shown;
            if (showing)
            {
                g.setColour (Palette::accentSoft);
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f * s), 8.0f * s);
            }
            else if (hover.category == category && count > 0)
            {
                g.setColour (Palette::idleFill.withMultipliedAlpha (1.6f));
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f * s), 8.0f * s);
            }
            auto text = row.reduced (12.0f * s, 0.0f);
            const auto tint = count == 0 ? Palette::textFaint : (showing ? Palette::accent : Palette::textStrong);
            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (count == 0 ? Palette::textFaint : (showing ? Palette::accent : Palette::textDim));
            g.drawText (juce::String (count), text.removeFromRight (24.0f * s), juce::Justification::centredRight, false);
            g.setFont (Fonts::sans (13.0f * s, true));
            g.setColour (tint);
            g.drawText (category == all ? juce::String ("All") : juce::String (Presets::categoryNames[(size_t) category]), text,
                        juce::Justification::centredLeft, true);
        }
    }

    // A small chip: `text` in `colour` on a faint fill of it, its left at `x`;
    // returns its width.
    float paintChip (juce::Graphics& g, const juce::String& text, float x, float centreY, juce::Colour colour) const
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  font = Fonts::mono (9.0f * s, 0.06f);
        const float w    = Fonts::width (font, text) + 14.0f * s;
        const juce::Rectangle chip (x, centreY - 9.0f * s, w, 18.0f * s);
        g.setColour (colour.withAlpha (0.14f));
        g.fillRoundedRectangle (chip, 9.0f * s);
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text, chip, juce::Justification::centred, false);
        return w;
    }

    // An oscillator's shape, small, in its own colour of the spectrum.
    void paintGlyph (juce::Graphics& g, Waveform wave, juce::Rectangle<float> box, int slot, bool withWell) const
    {
        using namespace Synth2;
        const float s = uiScale;
        if (withWell)
        {
            g.setColour (Palette::well);
            g.fillRoundedRectangle (box, 6.0f * s);
            box = box.reduced (6.0f * s, 5.0f * s);
        }
        g.setColour (ColourMath::spectrum ((float) slot / 3.0f));
        g.strokePath (WaveGlyph::path (wave, box, 1.5f),
                      juce::PathStrokeType (juce::jmax (1.0f, 1.5f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintList (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  list = listArea();
        const juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (list.getSmallestIntegerContainer());

        if (listed.empty())
        {
            g.setFont (Fonts::sans (13.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("No presets match \"" + search.getText() + "\"", list.withHeight (60.0f * s), juce::Justification::centred, true);
            return;
        }

        for (int row = 0; row < (int) listed.size(); ++row)
        {
            const auto box = rowBounds (row);
            if (! box.intersects (list))
                continue;
            const int   index  = listed[(size_t) row];
            const auto& preset = Presets::factory[(size_t) index];
            const bool  loaded = index == current;
            if (loaded || row == hover.row)
            {
                g.setColour (loaded ? Palette::accentSoft : Palette::idleFill.withMultipliedAlpha (1.6f));
                g.fillRoundedRectangle (box.reduced (0.0f, 2.0f * s), 10.0f * s);
            }
            if (loaded)
            {
                g.setColour (Palette::accent);
                g.fillRoundedRectangle (box.reduced (0.0f, 14.0f * s).withWidth (3.0f * s), 1.5f * s);
            }

            auto inside = box.reduced (16.0f * s, 8.0f * s);

            // At the right: its oscillators' shapes, MONO if it is, and its category.
            float right = inside.getRight();
            const auto category = juce::String (Presets::categoryNames[(size_t) preset.category]).toUpperCase();
            const float chipWidth = Fonts::width (Fonts::mono (9.0f * s, 0.06f), category) + 14.0f * s;
            right -= chipWidth;
            paintChip (g, category, right, inside.getCentreY(), Palette::textDim);
            if (preset.mono)
            {
                const float monoWidth = Fonts::width (Fonts::mono (9.0f * s, 0.06f), "MONO") + 14.0f * s;
                right -= monoWidth + 6.0f * s;
                paintChip (g, "MONO", right, inside.getCentreY(), Palette::violet);
            }
            for (int slot = Bench::numOscillators - 1; slot >= 0; --slot)
                if (const auto& osc = preset.oscillators[(size_t) slot]; osc.has_value())
                {
                    right -= 26.0f * s;
                    paintGlyph (g, (Waveform) osc->wave, juce::Rectangle (right, inside.getCentreY() - 6.0f * s, 20.0f * s, 12.0f * s), slot, false);
                }

            inside.setRight (right - 12.0f * s);
            g.setFont (Fonts::sans (14.0f * s, true));
            g.setColour (Palette::textStrong);
            g.drawText (preset.name, inside.removeFromTop (20.0f * s), juce::Justification::centredLeft, true);
            g.setFont (Fonts::sans (11.0f * s));
            g.setColour (Palette::textDim);
            g.drawText (preset.detail, inside, juce::Justification::centredLeft, true);
        }

        // A thin scroll bar, when the list is longer than its box.
        if (const float most = maxScroll(); most > 0.0f)
        {
            const float total = (float) listed.size() * rowHeight * s;
            const float thumb = list.getHeight() * list.getHeight() / total;
            const float y     = list.getY() + (list.getHeight() - thumb) * (scroll / most);
            g.setColour (Palette::strongLine);
            g.fillRoundedRectangle (juce::Rectangle (list.getRight() - 4.0f * s, y, 3.0f * s, thumb), 1.5f * s);
        }
    }

    // A section's label, under a little space, moving `area`'s top past it.
    void paintLabel (juce::Graphics& g, juce::Rectangle<float>& area, const juce::String& label) const
    {
        using namespace Synth2;
        const float s = uiScale;
        area.removeFromTop (14.0f * s);
        g.setFont (Fonts::sans (9.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText (label, area.removeFromTop (18.0f * s), juce::Justification::centredLeft, false);
    }

    static juce::String envelopeText (const Presets::Envelope& env)
    {
        using namespace Synth2;
        return "A " + Format::seconds (env.attackSeconds) + "  D " + Format::seconds (env.decaySeconds)
             + "  S " + Format::percent (env.sustainLevel) + "  R " + Format::seconds (env.releaseSeconds);
    }

    void paintDetail (juce::Graphics& g, const Presets::Preset& preset) const
    {
        using namespace Synth2;
        const float s = uiScale;
        auto area = detailArea();

        paintChip (g, juce::String (Presets::categoryNames[(size_t) preset.category]).toUpperCase(), area.getX(),
                   area.getY() + 9.0f * s, Palette::accent);
        area.removeFromTop (26.0f * s);
        g.setFont (Fonts::sans (24.0f * s, true));
        g.setColour (Palette::textStrong);
        g.drawText (preset.name, area.removeFromTop (34.0f * s), juce::Justification::centredLeft, true);
        g.setFont (Fonts::sans (12.0f * s));
        g.setColour (Palette::textDim);
        g.drawFittedText (preset.detail, area.removeFromTop (34.0f * s).getSmallestIntegerContainer(), juce::Justification::topLeft, 2, 1.0f);

        paintLabel (g, area, "OSCILLATORS");
        static constexpr std::array<const char*, 6> filterNames { "LOW 24", "LOW 12", "BAND", "HIGH 12", "HIGH 24", "NOTCH" };
        bool any = false;
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
        {
            const auto& osc = preset.oscillators[(size_t) slot];
            if (! osc.has_value())
                continue;
            any = true;
            auto row = area.removeFromTop (44.0f * s);
            paintGlyph (g, (Waveform) osc->wave, row.removeFromLeft (48.0f * s).withSizeKeepingCentre (48.0f * s, 30.0f * s), slot, true);
            row.removeFromLeft (10.0f * s);
            const int voices = osc->unison.voices;
            g.setFont (Fonts::sans (12.0f * s, true));
            g.setColour (Palette::textMid);
            g.drawText (juce::String (WaveGlyph::longNames[(size_t) juce::jlimit (0, 3, osc->wave)]) + "  " + Format::semitones (osc->semitones)
                            + "  " + juce::String (voices) + (voices == 1 ? " voice" : " voices"),
                        row.removeFromTop (22.0f * s).withTrimmedTop (4.0f * s), juce::Justification::centredLeft, true);
            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (Palette::textDim);
            const auto line = osc->filter.has_value()
                                ? juce::String (filterNames[(size_t) juce::jlimit (0, 5, osc->filter->type)]) + "  " + Format::hertz (osc->filter->cutoffHz)
                                      + "  RES " + Format::percent (osc->filter->resonance)
                                : (osc->envelope.has_value() ? envelopeText (*osc->envelope) : juce::String ("No envelope"));
            g.drawText (line, row.removeFromTop (16.0f * s), juce::Justification::centredLeft, true);
        }
        if (! any)
        {
            g.setFont (Fonts::sans (12.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("Nothing on the bench yet", area.removeFromTop (24.0f * s), juce::Justification::centredLeft, false);
        }

        paintLabel (g, area, "PLAYING");
        g.setFont (Fonts::sans (12.0f * s, true));
        g.setColour (Palette::textMid);
        juce::String playing = preset.mono ? "MONO" : "POLY";
        if (preset.glideSeconds > 0.0f)
            playing += "  GLIDE " + Format::seconds (preset.glideSeconds);
        playing += "  OUTPUT " + Format::decibels (preset.outputDb);
        g.drawText (playing, area.removeFromTop (22.0f * s), juce::Justification::centredLeft, true);

        paintLabel (g, area, "EFFECTS");
        const auto chips = area.removeFromTop (22.0f * s);
        float x = chips.getX();
        if (preset.drive.has_value())
            x += paintChip (g, "DRIVE", x, chips.getCentreY(), Palette::amber) + 6.0f * s;
        if (preset.delay.has_value())
            x += paintChip (g, "DELAY " + juce::String (Effects::divisions[(size_t) preset.delay->division].name).toUpperCase(), x,
                            chips.getCentreY(), Palette::pink) + 6.0f * s;
        if (preset.reverb.has_value())
            x += paintChip (g, "REVERB", x, chips.getCentreY(), Palette::violet) + 6.0f * s;
        if (juce::exactlyEqual (x, chips.getX()))
        {
            g.setFont (Fonts::sans (12.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("None: dry", chips, juce::Justification::centredLeft, false);
        }

        // How to get about, at the foot.
        g.setFont (Fonts::sans (10.0f * s));
        g.setColour (Palette::textFaint);
        g.drawFittedText ("Click a preset to hear it; double-click to load it and close.\nUp and down step through the list; Esc closes.",
                          detailArea().removeFromBottom (32.0f * s).getSmallestIntegerContainer(), juce::Justification::bottomLeft, 2, 1.0f);
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        // The search box sends up, down, Return and Escape here first.
        return handleKey (key);
    }

    juce::TextEditor search;
    std::function<void (int)> chooseHandler;
    std::function<void()>     closeHandler;
    std::vector<int> listed;
    Hover hover;
    int   shown   = all;
    int   current = 0;
    float scroll  = 0.0f;   // design pixels down the list
    float uiScale = 1.0f;
};

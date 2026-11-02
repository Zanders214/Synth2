#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

#include "Presets.h"
#include "Synth2Theme.h"

/**
    PresetSavePanel — the preset browser's save panel: over the browser, a
    name, a line about the sound and its category, and SAVE (REPLACE, for a
    name the user has saved under already, which it says it will) and
    CANCEL. Return saves, Escape cancels, and so does a click off it.
*/
class PresetSavePanel final : public juce::Component,
                              private juce::KeyListener
{
public:
    struct Handlers
    {
        /** Saves under the name, the line and the category given; true if it was. */
        std::function<bool (const juce::String&, const juce::String&, Presets::Category)> save;
        /** Whether the user has a preset of that name already. */
        std::function<bool (const juce::String&)> isTaken;
        /** It closed, saved or not. */
        std::function<void()> closed;
    };

    PresetSavePanel()
    {
        using namespace Synth2;
        setVisible (false);
        setWantsKeyboardFocus (true);
        nameBox.setTextToShowWhenEmpty ("Name", Palette::textFaint);
        detailBox.setTextToShowWhenEmpty ("A line about how it sounds (optional)", Palette::textFaint);
        nameBox.setInputRestrictions (40);
        detailBox.setInputRestrictions (80);
        nameBox.onTextChange = [this] { repaint(); };
        for (auto* box : { &nameBox, &detailBox })
        {
            styleBox (*box);
            box->addKeyListener (this);
            addAndMakeVisible (*box);
        }
    }

    ~PresetSavePanel() override
    {
        nameBox.removeKeyListener (this);
        detailBox.removeKeyListener (this);
    }

    void setHandlers (Handlers newHandlers) { handlers = std::move (newHandlers); }

    void open (const juce::String& name, const juce::String& detail, Presets::Category category)
    {
        nameBox.setText (name, false);
        detailBox.setText (detail, false);
        chosen = category == Presets::Category::init ? Presets::Category::lead : category;
        hover = {};
        setVisible (true);
        toFront (false);
        if (isShowing())
            nameBox.grabKeyboardFocus();
        nameBox.selectAll();
        repaint();
    }

    void cancel()
    {
        if (! isVisible())
            return;
        nameBox.giveAwayKeyboardFocus();
        detailBox.giveAwayKeyboardFocus();
        setVisible (false);
        if (handlers.closed != nullptr)
            handlers.closed();
    }

    /** Saves under the name typed and closes; with no name, or if it
        can't be saved, it stays open. */
    bool save()
    {
        if (! canSave() || handlers.save == nullptr || ! handlers.save (nameBox.getText().trim(), detailBox.getText().trim(), chosen))
            return false;
        cancel();
        return true;
    }

    bool isOpen() const noexcept { return isVisible(); }
    bool canSave() const { return nameBox.getText().trim().isNotEmpty(); }
    bool replaces() const { return canSave() && handlers.isTaken != nullptr && handlers.isTaken (nameBox.getText()); }

    juce::TextEditor& getNameBox() noexcept   { return nameBox; }
    juce::TextEditor& getDetailBox() noexcept { return detailBox; }
    Presets::Category getCategory() const noexcept { return chosen; }

    /** Return saves, Escape cancels; false for the rest. */
    bool handleKey (const juce::KeyPress& key)
    {
        if (! isOpen())
            return false;
        if (key == juce::KeyPress::returnKey)
        {
            save();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            cancel();
            return true;
        }
        return false;
    }

    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        for (auto* box : { &nameBox, &detailBox })
        {
            box->applyFontToAllText (Synth2::Fonts::sans (13.0f * uiScale));
            box->setIndents (juce::roundToInt (12.0f * uiScale), juce::roundToInt (9.0f * uiScale));
        }
        resized();
        repaint();
    }

    juce::Rectangle<float> cardBounds() const noexcept
    {
        const float s = uiScale;
        return getLocalBounds().toFloat().withSizeKeepingCentre (440.0f * s, 344.0f * s);
    }

    /** Where `category`'s chip sits (any but Init). */
    juce::Rectangle<float> chipBounds (Presets::Category category) const noexcept
    {
        const float s  = uiScale;
        const int   at = (int) category - 1;
        const float w  = (440.0f - 48.0f - 4.0f * 6.0f) / 5.0f;
        const auto  card = cardBounds();
        return { card.getX() + (24.0f + (float) (at % 5) * (w + 6.0f)) * s, card.getY() + (188.0f + (float) (at / 5) * 34.0f) * s,
                 w * s, 28.0f * s };
    }

    juce::Rectangle<float> saveBounds() const noexcept
    {
        const float s = uiScale;
        const auto  card = cardBounds();
        return { card.getRight() - (24.0f + 110.0f) * s, card.getY() + 290.0f * s, 110.0f * s, 34.0f * s };
    }

    juce::Rectangle<float> cancelBounds() const noexcept
    {
        const float s = uiScale;
        return saveBounds().translated (-(96.0f + 8.0f) * s, 0.0f).withWidth (96.0f * s);
    }

    void resized() override
    {
        const float s = uiScale;
        const auto  card = cardBounds();
        const auto  box  = [&card, s] (float y) { return juce::Rectangle (card.getX() + 24.0f * s, card.getY() + y * s, card.getWidth() - 48.0f * s, 34.0f * s); };
        nameBox.setBounds (box (66.0f).getSmallestIntegerContainer());
        detailBox.setBounds (box (126.0f).getSmallestIntegerContainer());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const Hover now { chipAt (e.position), saveBounds().contains (e.position), cancelBounds().contains (e.position) };
        if (now.chip != hover.chip || now.save != hover.save || now.cancel != hover.cancel)
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
        if (! cardBounds().contains (e.position) || cancelBounds().contains (e.position))
            cancel();
        else if (saveBounds().contains (e.position))
            save();
        else if (const auto chip = chipAt (e.position); chip.has_value())
        {
            chosen = *chip;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  card = cardBounds();

        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        g.setGradientFill (juce::ColourGradient (Palette::panelTop, 0.0f, card.getY(), Palette::panelBottom, 0.0f, card.getBottom(), false));
        g.fillRoundedRectangle (card, 14.0f * s);
        g.setColour (Palette::strongLine);
        g.drawRoundedRectangle (card.reduced (0.5f), 14.0f * s, juce::jmax (1.0f, s));

        g.setFont (Fonts::sans (16.0f * s, true));
        g.setColour (Palette::textStrong);
        g.drawText ("Save preset", juce::Rectangle (card.getX() + 24.0f * s, card.getY() + 18.0f * s, card.getWidth() - 48.0f * s, 24.0f * s),
                    juce::Justification::centredLeft, false);
        paintLabel (g, "NAME", 48.0f, Palette::textFaint);
        paintLabel (g, "DESCRIPTION", 108.0f, Palette::textFaint);
        paintLabel (g, "CATEGORY", 168.0f, Palette::textFaint);
        paintChips (g);
        if (replaces())
            paintLabel (g, "REPLACES YOUR PRESET OF THAT NAME", 262.0f, Palette::amber);
        paintButtons (g);
    }

private:
    struct Hover
    {
        std::optional<Presets::Category> chip;
        bool save   = false;
        bool cancel = false;
    };

    static void styleBox (juce::TextEditor& box)
    {
        using namespace Synth2;
        box.setColour (juce::TextEditor::backgroundColourId, Palette::idleFill);
        box.setColour (juce::TextEditor::outlineColourId, Palette::hairline);
        box.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        box.setColour (juce::TextEditor::textColourId, Palette::textStrong);
        box.setColour (juce::TextEditor::highlightColourId, Palette::accentSoft.withMultipliedAlpha (3.0f));
        box.setColour (juce::CaretComponent::caretColourId, Palette::accent);
        box.setEscapeAndReturnKeysConsumed (false);
    }

    std::optional<Presets::Category> chipAt (juce::Point<float> position) const noexcept
    {
        for (int c = 1; c < Presets::numCategories; ++c)
            if (chipBounds ((Presets::Category) c).contains (position))
                return (Presets::Category) c;
        return std::nullopt;
    }

    // A label across the card, `y` design pixels down it.
    void paintLabel (juce::Graphics& g, const juce::String& text, float y, juce::Colour colour) const
    {
        const float s    = uiScale;
        const auto  card = cardBounds();
        g.setFont (Synth2::Fonts::sans (9.0f * s, true, 0.22f));
        g.setColour (colour);
        g.drawText (text, juce::Rectangle (card.getX() + 24.0f * s, card.getY() + y * s, card.getWidth() - 48.0f * s, 16.0f * s),
                    juce::Justification::centredLeft, false);
    }

    // The categories, the one picked lit.
    void paintChips (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        for (int c = 1; c < Presets::numCategories; ++c)
        {
            const auto category = (Presets::Category) c;
            const auto chip     = chipBounds (category);
            const bool picked   = category == chosen;
            auto fill = Palette::idleFill.withMultipliedAlpha (hover.chip == category ? 1.8f : 1.0f);
            if (picked)
                fill = Palette::accentSoft.withMultipliedAlpha (2.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (chip, 8.0f * s);
            if (picked)
            {
                g.setColour (Palette::accent);
                g.drawRoundedRectangle (chip.reduced (0.5f), 8.0f * s, juce::jmax (1.0f, s));
            }
            g.setFont (Fonts::sans (12.0f * s, true));
            g.setColour (picked ? Palette::accent : Palette::textMid);
            g.drawText (Presets::categoryNames[(size_t) c], chip, juce::Justification::centred, false);
        }
    }

    // CANCEL, and SAVE (or REPLACE), greyed out without a name.
    void paintButtons (juce::Graphics& g) const
    {
        using namespace Synth2;
        const float s = uiScale;
        const auto  cancelBox = cancelBounds();
        g.setColour (Palette::idleFill.withMultipliedAlpha (hover.cancel ? 1.8f : 1.0f));
        g.fillRoundedRectangle (cancelBox, cancelBox.getHeight() * 0.5f);
        g.setFont (Fonts::sans (12.0f * s, true));
        g.setColour (Palette::textMid);
        g.drawText ("Cancel", cancelBox, juce::Justification::centred, false);

        const auto saveBox = saveBounds();
        const bool can     = canSave();
        auto fill = Palette::idleFill;
        if (can)
            fill = Palette::accent.withMultipliedAlpha (hover.save ? 1.0f : 0.85f);
        g.setColour (fill);
        g.fillRoundedRectangle (saveBox, saveBox.getHeight() * 0.5f);
        g.setColour (can ? Palette::panelBottom : Palette::textFaint);
        g.drawText (replaces() ? "Replace" : "Save", saveBox, juce::Justification::centred, false);
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        // The boxes send Return and Escape here first.
        return handleKey (key);
    }

    juce::TextEditor nameBox;
    juce::TextEditor detailBox;
    Handlers handlers;
    Presets::Category chosen = Presets::Category::lead;
    Hover hover;
    float uiScale = 1.0f;
};

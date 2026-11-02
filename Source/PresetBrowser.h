#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "Presets.h"
#include "Synth2Theme.h"
#include "UserPresets.h"
#include "WavePicker.h"

/**
    PresetBrowser — the factory presets and the ones saved by the user, laid
    over the bench (or the effects) while the keyboard below stays playable,
    so each preset can be heard as it is picked.

    Down its left, the categories: ALL first, then USER (the presets saved
    here), then each category with how many presets it holds (one with none
    yet is greyed out). In the middle, the presets in the category picked,
    the user's own after the factory's and marked USER, narrowed by the
    search box over them, which matches names, descriptions and categories,
    every word. On the right, the loaded preset: its category, name and
    description, its oscillators (each one's shape, pitch and stack, and its
    filter or envelope), how its notes play (POLY or MONO, and GLIDE) and the
    effects it goes through; and, for one of the user's, DELETE, which asks
    again before it does.

    SAVE, at the top, opens the save panel: a name, a line about it and its
    category, then the synth as it is now is kept under that name (replacing
    one of the user's of that name, which it says it will).

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
    static constexpr int all  = -1;   // the category that shows every preset
    static constexpr int user = -2;   // the one that shows the user's own

    /** A preset listed: a factory one, by program number, or one of the
        user's, by its place in UserPresets::all(). */
    struct Item
    {
        int factory = -1;
        int saved   = -1;

        bool isUser() const noexcept { return saved >= 0; }
        bool operator== (const Item& other) const noexcept { return factory == other.factory && saved == other.saved; }
    };

    /** The save panel: over the browser, a name, a line about the sound and
        its category, and SAVE (REPLACE, for a name the user has saved under
        already) and CANCEL. Return saves, Escape cancels, and so does a
        click off it. */
    class SavePanel final : public juce::Component,
                            private juce::KeyListener
    {
    public:
        SavePanel()
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

        ~SavePanel() override
        {
            nameBox.removeKeyListener (this);
            detailBox.removeKeyListener (this);
        }

        /** Saves under the name, the line and the category given; true if it was. */
        std::function<bool (const juce::String&, const juce::String&, Presets::Category)> onSave;
        /** Whether the user has a preset of that name already. */
        std::function<bool (const juce::String&)> isTaken;
        /** It closed, saved or not. */
        std::function<void()> onClosed;

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
            if (onClosed != nullptr)
                onClosed();
        }

        /** Saves under the name typed and closes; with no name, or if it
            can't be saved, it stays open. */
        bool save()
        {
            if (! canSave() || onSave == nullptr || ! onSave (nameBox.getText().trim(), detailBox.getText().trim(), chosen))
                return false;
            cancel();
            return true;
        }

        bool isOpen() const noexcept { return isVisible(); }
        bool canSave() const { return nameBox.getText().trim().isNotEmpty(); }
        bool replaces() const { return canSave() && isTaken != nullptr && isTaken (nameBox.getText()); }

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
            const float s = uiScale;
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

            const auto label = [&g, &card, s] (const juce::String& text, float y, juce::Colour colour)
            {
                g.setFont (Fonts::sans (9.0f * s, true, 0.22f));
                g.setColour (colour);
                g.drawText (text, juce::Rectangle (card.getX() + 24.0f * s, card.getY() + y * s, card.getWidth() - 48.0f * s, 16.0f * s),
                            juce::Justification::centredLeft, false);
            };
            g.setFont (Fonts::sans (16.0f * s, true));
            g.setColour (Palette::textStrong);
            g.drawText ("Save preset", juce::Rectangle (card.getX() + 24.0f * s, card.getY() + 18.0f * s, card.getWidth() - 48.0f * s, 24.0f * s),
                        juce::Justification::centredLeft, false);
            label ("NAME", 48.0f, Palette::textFaint);
            label ("DESCRIPTION", 108.0f, Palette::textFaint);
            label ("CATEGORY", 168.0f, Palette::textFaint);

            for (int c = 1; c < Presets::numCategories; ++c)
            {
                const auto category = (Presets::Category) c;
                const auto chip     = chipBounds (category);
                const bool picked   = category == chosen;
                g.setColour (picked ? Palette::accentSoft.withMultipliedAlpha (2.0f)
                                    : Palette::idleFill.withMultipliedAlpha (hover.chip == category ? 1.8f : 1.0f));
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

            if (replaces())
                label ("REPLACES YOUR PRESET OF THAT NAME", 262.0f, Palette::amber);

            const auto cancelBox = cancelBounds();
            g.setColour (Palette::idleFill.withMultipliedAlpha (hover.cancel ? 1.8f : 1.0f));
            g.fillRoundedRectangle (cancelBox, cancelBox.getHeight() * 0.5f);
            g.setFont (Fonts::sans (12.0f * s, true));
            g.setColour (Palette::textMid);
            g.drawText ("Cancel", cancelBox, juce::Justification::centred, false);

            const auto saveBox = saveBounds();
            const bool can     = canSave();
            g.setColour (can ? Palette::accent.withMultipliedAlpha (hover.save ? 1.0f : 0.85f) : Palette::idleFill);
            g.fillRoundedRectangle (saveBox, saveBox.getHeight() * 0.5f);
            g.setColour (can ? Palette::panelBottom : Palette::textFaint);
            g.drawText (replaces() ? "Replace" : "Save", saveBox, juce::Justification::centred, false);
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

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            // The boxes send Return and Escape here first.
            return handleKey (key);
        }

        juce::TextEditor nameBox, detailBox;
        Presets::Category chosen = Presets::Category::lead;
        Hover hover;
        float uiScale = 1.0f;
    };

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

        savePanel.isTaken  = [this] (const juce::String& name) { return store != nullptr && store->find (name) != nullptr; };
        savePanel.onSave   = [this] (const juce::String& name, const juce::String& detail, Presets::Category category)
        {
            if (saveHandler == nullptr || ! saveHandler (name, detail, category))
                return false;
            // Shown among the user's own, loaded.
            currentUser  = name;
            deleteArmed  = false;
            shown        = user;
            search.clear();
            scroll = 0.0f;
            refilter();
            scrollToShow (currentItem());
            return true;
        };
        savePanel.onClosed = [this]
        {
            if (isShowing())
                grabKeyboardFocus();
        };
        addChildComponent (savePanel);
        refilter();
    }

    ~PresetBrowser() override { search.removeKeyListener (this); }

    /** Where the user's presets are kept; the browser reads them again each time it opens. */
    void setUserPresets (UserPresets& presets)
    {
        store = &presets;
        refilter();
    }

    void onChoose (std::function<void (int)> handler)                         { chooseHandler = std::move (handler); }
    void onChooseUser (std::function<void (const UserPresets::Entry&)> handler) { chooseUserHandler = std::move (handler); }
    void onClose (std::function<void()> handler)                              { closeHandler = std::move (handler); }

    /** Saving: given the name, the line and the category, true if it was saved. */
    void onSave (std::function<bool (const juce::String&, const juce::String&, Presets::Category)> handler) { saveHandler = std::move (handler); }

    /** Deleting one of the user's presets (it has asked first). */
    void onDelete (std::function<void (const UserPresets::Entry&)> handler) { deleteHandler = std::move (handler); }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        search.applyFontToAllText (Synth2::Fonts::sans (13.0f * uiScale));
        search.setIndents (juce::roundToInt (12.0f * uiScale), juce::roundToInt (8.0f * uiScale));
        savePanel.setUiScale (newScale);
        resized();
        repaint();
    }

    /** Opens on the list as it was left, with the preset loaded (factory
        preset `loaded`, or the user's `loadedUser` if it isn't empty) in view. */
    void open (int loaded, const juce::String& loadedUser = {})
    {
        current     = loaded;
        currentUser = loadedUser;
        deleteArmed = false;
        if (store != nullptr)
            store->rescan();
        refilter();
        scrollToShow (currentItem());
        juce::Desktop::getInstance().getAnimator().fadeIn (this, 80);   // --dur-fast
        toFront (false);
        if (isShowing())
            grabKeyboardFocus();
    }

    void close()
    {
        if (! isVisible())
            return;
        savePanel.cancel();
        search.giveAwayKeyboardFocus();
        setVisible (false);
        if (closeHandler != nullptr)
            closeHandler();
    }

    bool isOpen() const noexcept { return isVisible(); }

    /** The loaded preset changed (stepped to with the picker's arrows, say):
        factory preset `index`, or the user's `userName` if it isn't empty. */
    void setCurrent (int index, const juce::String& userName = {})
    {
        if (index == current && userName == currentUser)
            return;
        current     = index;
        currentUser = userName;
        deleteArmed = false;
        scrollToShow (currentItem());
        repaint();
    }

    /** Shows `category` (all, user, or a Presets::Category), if it has any
        presets (the user's always shows, to say how to save one). */
    void showCategory (int category)
    {
        if (category == shown || (category != all && category != user && countIn (category) == 0))
            return;
        shown  = category;
        scroll = 0.0f;
        refilter();
        scrollToShow (currentItem());
    }

    /** Opens the save panel, on the name, line and category of the preset
        loaded if it is one of the user's, otherwise on just its category. */
    void openSavePanel()
    {
        // Another instance may have saved since the browser opened.
        if (store != nullptr)
        {
            store->rescan();
            refilter();
        }
        const auto* entry = currentEntry();
        const auto  category = entry != nullptr ? entry->category
                             : juce::isPositiveAndBelow (current, Presets::numPresets) ? Presets::factory[(size_t) current].category
                                                                                      : Presets::Category::lead;
        savePanel.open (entry != nullptr ? entry->name : juce::String(), entry != nullptr ? entry->detail : juce::String(), category);
    }

    int getCategory() const noexcept { return shown; }
    int getCurrent() const noexcept  { return current; }
    const juce::String& getCurrentUser() const noexcept { return currentUser; }
    bool isDeleteArmed() const noexcept { return deleteArmed; }
    juce::TextEditor& getSearch() noexcept { return search; }
    SavePanel& getSavePanel() noexcept { return savePanel; }

    /** The presets listed, in the order they show. */
    const std::vector<Item>& getListed() const noexcept { return listed; }

    /** Where the `row`th preset listed sits (it may be scrolled out of the list). */
    juce::Rectangle<float> rowBounds (int row) const noexcept
    {
        const float s = uiScale;
        const auto  list = listArea();
        return { list.getX(), list.getY() + ((float) row * rowHeight - scroll) * s, list.getWidth() - 10.0f * s, rowHeight * s };
    }

    /** Where `category`'s (all, user, or a Presets::Category) row sits. */
    juce::Rectangle<float> categoryBounds (int category) const noexcept
    {
        const float s     = uiScale;
        const int   place = category == all ? 0 : category == user ? 1 : category + 2;
        return categoryArea().withTrimmedTop ((float) place * categoryHeight * s).withHeight (categoryHeight * s);
    }

    juce::Rectangle<float> closeBounds() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - 48.0f * s, 12.0f * s, 32.0f * s, 32.0f * s };
    }

    /** SAVE, in the top bar, by the close cross. */
    juce::Rectangle<float> saveBounds() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - (48.0f + 10.0f + 84.0f) * s, 14.0f * s, 84.0f * s, 28.0f * s };
    }

    /** DELETE, under the details of one of the user's presets (none for a factory one). */
    std::optional<juce::Rectangle<float>> deleteBounds() const noexcept
    {
        if (currentEntry() == nullptr)
            return std::nullopt;
        const float s = uiScale;
        const auto  detail = detailArea();
        return juce::Rectangle (detail.getX(), detail.getBottom() - (32.0f + 12.0f + 30.0f) * s, 176.0f * s, 30.0f * s);
    }

    /** The keys the browser answers to (see above); false for the rest. */
    bool handleKey (const juce::KeyPress& key)
    {
        if (! isOpen())
            return false;
        if (savePanel.isOpen())
            return savePanel.handleKey (key);
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
        savePanel.setBounds (getLocalBounds());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto removal = deleteBounds();
        const Hover now { rowAt (e.position), categoryAt (e.position), closeBounds().contains (e.position),
                          saveBounds().contains (e.position), removal.has_value() && removal->contains (e.position) };
        if (now.row != hover.row || now.category != hover.category || now.close != hover.close || now.save != hover.save
            || now.remove != hover.remove)
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
        const auto removal = deleteBounds();
        if (closeBounds().contains (e.position))
            close();
        else if (saveBounds().contains (e.position))
            openSavePanel();
        else if (removal.has_value() && removal->contains (e.position))
            deleteCurrent();
        else if (const auto category = categoryAt (e.position); category.has_value())
            showCategory (*category);
        else if (const int row = rowAt (e.position); row >= 0)
            choose (listed[(size_t) row]);
        else if (deleteArmed)
        {
            deleteArmed = false;
            repaint();
        }
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
        paintSave (g, saveBounds());
        paintClose (g, closeBounds());
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (16.0f * s, 56.0f * s, area.getWidth() - 32.0f * s, juce::jmax (1.0f, s)));

        paintCategories (g);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (listArea().getX() - 10.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        paintList (g);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (detailArea().getX() - 14.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        if (const auto* entry = currentEntry())
            paintDetail (g, entry->sound, entry->name, entry->detail, true);
        else if (juce::isPositiveAndBelow (current, Presets::numPresets))
            paintDetail (g, Presets::factory[(size_t) current], Presets::factory[(size_t) current].name, Presets::factory[(size_t) current].detail, false);
    }

private:
    static constexpr float categoryHeight = 34.0f;
    static constexpr float rowHeight      = 54.0f;

    struct Hover
    {
        int row = -1;
        std::optional<int> category;
        bool close  = false;
        bool save   = false;
        bool remove = false;
    };

    // What an item is: its sound, name, line and whether it is the user's.
    struct Shown
    {
        const Presets::Preset& sound;
        juce::String name, detail;
        bool isUser;
    };

    Shown describe (const Item& item) const
    {
        if (item.isUser())
        {
            const auto& entry = store->all()[(size_t) item.saved];
            return { entry.sound, entry.name, entry.detail, true };
        }
        const auto& preset = Presets::factory[(size_t) item.factory];
        return { preset, preset.name, preset.detail, false };
    }

    int userCount() const noexcept { return store != nullptr ? (int) store->all().size() : 0; }

    // The user's preset loaded, if one is (and is still saved).
    const UserPresets::Entry* currentEntry() const
    {
        return store != nullptr && currentUser.isNotEmpty() ? store->find (currentUser) : nullptr;
    }

    Item currentItem() const
    {
        if (const auto* entry = currentEntry())
            return { -1, (int) (entry - store->all().data()) };
        return { current, -1 };
    }

    // DELETE: the first click asks, the second deletes.
    void deleteCurrent()
    {
        const auto* entry = currentEntry();
        if (entry == nullptr)
            return;
        if (! deleteArmed)
        {
            deleteArmed = true;
            repaint();
            return;
        }
        deleteArmed = false;
        const auto doomed = *entry;
        currentUser.clear();
        if (deleteHandler != nullptr)
            deleteHandler (doomed);
        if (store != nullptr)
            store->rescan();
        if (shown != all && shown != user && countIn (shown) == 0)
            shown = all;
        refilter();
    }

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

    int countIn (int category) const noexcept
    {
        if (category == user)
            return userCount();
        int count = 0;
        for (const auto& preset : Presets::factory)
            count += (int) preset.category == category ? 1 : 0;
        if (store != nullptr)
            for (const auto& entry : store->all())
                count += (int) entry.category == category ? 1 : 0;
        return count;
    }

    // Whether `item` has every word of the search in its name, description or
    // category (or USER, for the user's own).
    bool matches (const Item& item) const
    {
        const auto words = juce::StringArray::fromTokens (search.getText(), true);
        const auto shownAs = describe (item);
        const juce::String text = shownAs.name + " " + shownAs.detail + " " + Presets::nameOf (shownAs.sound.category)
                                + (shownAs.isUser ? " user" : "");
        for (const auto& word : words)
            if (word.isNotEmpty() && ! text.containsIgnoreCase (word))
                return false;
        return true;
    }

    // Lists the presets in the category showing that match the search: by
    // category, the factory's in the order they were added, then the user's
    // by name.
    void refilter()
    {
        listed.clear();
        const auto add = [this] (const Item& item)
        {
            if (matches (item))
                listed.push_back (item);
        };
        for (int category = 0; category < Presets::numCategories; ++category)
        {
            if (shown == user || (shown != all && shown != category))
                continue;
            for (int i = 0; i < Presets::numPresets; ++i)
                if ((int) Presets::factory[(size_t) i].category == category)
                    add ({ i, -1 });
            for (int i = 0; i < userCount(); ++i)
                if ((int) store->all()[(size_t) i].category == category)
                    add ({ -1, i });
        }
        if (shown == user)
            for (int i = 0; i < userCount(); ++i)
                add ({ -1, i });
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

    // Scrolls `item` into the list's view, if it is listed.
    void scrollToShow (const Item& item)
    {
        const auto at = std::find (listed.begin(), listed.end(), item);
        if (at == listed.end())
            return;
        const float top    = (float) (at - listed.begin()) * rowHeight;
        const float height = listArea().getHeight() / uiScale;
        if (top < scroll)
            scrollTo (top);
        else if (top + rowHeight > scroll + height)
            scrollTo (top + rowHeight - height);
    }

    void choose (const Item& item)
    {
        deleteArmed = false;
        if (item.isUser())
        {
            const auto entry = store->all()[(size_t) item.saved];
            currentUser = entry.name;
            scrollToShow (item);
            repaint();
            if (chooseUserHandler != nullptr)
                chooseUserHandler (entry);
            return;
        }
        current = item.factory;
        currentUser.clear();
        scrollToShow (item);
        repaint();
        if (chooseHandler != nullptr)
            chooseHandler (item.factory);
    }

    // Loads the preset `direction` (1 down, -1 up) from the loaded one in
    // the list, wrapping round; from one not listed, the first or last.
    void step (int direction)
    {
        if (listed.empty())
            return;
        const int count = (int) listed.size();
        const auto at = std::find (listed.begin(), listed.end(), currentItem());
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
        for (int category = user; category < Presets::numCategories; ++category)
            if (categoryBounds (category).contains (position))
                return category;
        return std::nullopt;
    }

    // SAVE: an accent pill, lit under the pointer.
    void paintSave (juce::Graphics& g, juce::Rectangle<float> box) const
    {
        using namespace Synth2;
        const float s = uiScale;
        g.setColour (Palette::accentSoft.withMultipliedAlpha (hover.save ? 2.4f : 1.4f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (Palette::accent.withAlpha (hover.save ? 0.9f : 0.5f));
        g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, juce::jmax (1.0f, s));
        g.setFont (Fonts::sans (10.0f * s, true, 0.18f));
        g.setColour (Palette::accent);
        g.drawText ("SAVE", box, juce::Justification::centred, false);
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
        for (int category = user; category < Presets::numCategories; ++category)
        {
            const auto row     = categoryBounds (category);
            const int  count   = category == all ? Presets::numPresets + userCount() : countIn (category);
            const bool showing = category == shown;
            const bool live    = count > 0 || category == user;
            if (category == user)
            {
                g.setColour (Palette::hairline);
                g.fillRect (row.withY (row.getBottom() - 0.5f * s).withHeight (juce::jmax (1.0f, s)).reduced (8.0f * s, 0.0f));
            }
            if (showing)
            {
                g.setColour (Palette::accentSoft);
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f * s), 8.0f * s);
            }
            else if (hover.category == category && live)
            {
                g.setColour (Palette::idleFill.withMultipliedAlpha (1.6f));
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f * s), 8.0f * s);
            }
            auto text = row.reduced (12.0f * s, 0.0f);
            const auto tint = ! live ? Palette::textFaint : (showing ? Palette::accent : Palette::textStrong);
            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (count == 0 ? Palette::textFaint : (showing ? Palette::accent : Palette::textDim));
            g.drawText (juce::String (count), text.removeFromRight (24.0f * s), juce::Justification::centredRight, false);
            g.setFont (Fonts::sans (13.0f * s, true));
            g.setColour (tint);
            const auto name = category == all ? juce::String ("All") : category == user ? juce::String ("User")
                                                                    : juce::String (Presets::categoryNames[(size_t) category]);
            g.drawText (name, text, juce::Justification::centredLeft, true);
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
            if (shown == user && userCount() == 0)
                g.drawFittedText ("No presets of your own yet.\nSAVE keeps the sound as it is now, to come back to.",
                                  list.withHeight (60.0f * s).getSmallestIntegerContainer(), juce::Justification::centred, 2, 1.0f);
            else
                g.drawText ("No presets match \"" + search.getText() + "\"", list.withHeight (60.0f * s), juce::Justification::centred, true);
            return;
        }

        for (int row = 0; row < (int) listed.size(); ++row)
        {
            const auto box = rowBounds (row);
            if (! box.intersects (list))
                continue;
            const auto  item    = listed[(size_t) row];
            const auto  shownAs = describe (item);
            const auto& preset  = shownAs.sound;
            const bool  loaded  = item == currentItem();
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
            if (shownAs.isUser)
            {
                const float userWidth = Fonts::width (Fonts::mono (9.0f * s, 0.06f), "USER") + 14.0f * s;
                right -= userWidth + 6.0f * s;
                paintChip (g, "USER", right, inside.getCentreY(), Palette::cyan);
            }
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
            g.drawText (shownAs.name, inside.removeFromTop (20.0f * s), juce::Justification::centredLeft, true);
            g.setFont (Fonts::sans (11.0f * s));
            g.setColour (Palette::textDim);
            g.drawText (shownAs.detail, inside, juce::Justification::centredLeft, true);
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

    void paintDetail (juce::Graphics& g, const Presets::Preset& preset, const juce::String& name, const juce::String& detail, bool isUser) const
    {
        using namespace Synth2;
        const float s = uiScale;
        auto area = detailArea();

        const float chipWidth = paintChip (g, juce::String (Presets::categoryNames[(size_t) preset.category]).toUpperCase(), area.getX(),
                                           area.getY() + 9.0f * s, Palette::accent);
        if (isUser)
            paintChip (g, "USER", area.getX() + chipWidth + 6.0f * s, area.getY() + 9.0f * s, Palette::cyan);
        area.removeFromTop (26.0f * s);
        g.setFont (Fonts::sans (24.0f * s, true));
        g.setColour (Palette::textStrong);
        g.drawText (name, area.removeFromTop (34.0f * s), juce::Justification::centredLeft, true);
        g.setFont (Fonts::sans (12.0f * s));
        g.setColour (Palette::textDim);
        g.drawFittedText (detail, area.removeFromTop (34.0f * s).getSmallestIntegerContainer(), juce::Justification::topLeft, 2, 1.0f);

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

        // DELETE, for one of the user's: asking first.
        if (const auto removal = deleteBounds(); removal.has_value())
        {
            const auto box = *removal;
            g.setColour (Palette::pink.withAlpha (deleteArmed ? 0.24f : (hover.remove ? 0.16f : 0.08f)));
            g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
            g.setColour (Palette::pink.withAlpha (deleteArmed || hover.remove ? 0.9f : 0.45f));
            g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, juce::jmax (1.0f, s));
            g.setFont (Fonts::sans (10.0f * s, true, 0.14f));
            g.setColour (Palette::pink);
            g.drawText (deleteArmed ? "CLICK AGAIN TO DELETE" : "DELETE", box, juce::Justification::centred, false);
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
    SavePanel savePanel;
    UserPresets* store = nullptr;
    std::function<void (int)> chooseHandler;
    std::function<void (const UserPresets::Entry&)> chooseUserHandler;
    std::function<void()>     closeHandler;
    std::function<bool (const juce::String&, const juce::String&, Presets::Category)> saveHandler;
    std::function<void (const UserPresets::Entry&)> deleteHandler;
    std::vector<Item> listed;
    Hover hover;
    int   shown   = all;
    int   current = 0;
    juce::String currentUser;   // the user's preset loaded, if not a factory one
    bool  deleteArmed = false;  // DELETE clicked once
    float scroll  = 0.0f;   // design pixels down the list
    float uiScale = 1.0f;
};

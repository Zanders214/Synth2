#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

#include "PresetBrowserLook.h"
#include "PresetCatalog.h"
#include "PresetSavePanel.h"
#include "Synth2Theme.h"
#include "UserPresets.h"

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

    SAVE, at the top, opens the save panel (see PresetSavePanel.h): a name, a
    line about it and its category, then the synth as it is now is kept under
    that name.

    A click on a preset loads it and keeps the browser open; a double-click
    loads it and closes it. The up and down arrow keys load the previous and
    next preset in the list, Return closes, and Escape clears the search or,
    with none, closes. Keys it has no use for go on to the editor, so the
    computer's keys still play. The wheel scrolls the list.

    What is listed and loaded is the PresetCatalog's; where things sit and how
    they are painted, PresetBrowserLook's.
*/
class PresetBrowser final : public juce::Component
{
public:
    using Item      = PresetCatalog::Item;
    using SavePanel = PresetSavePanel;
    static constexpr int all  = PresetCatalog::all;
    static constexpr int user = PresetCatalog::user;

    /** What picking, saving, deleting and closing do. */
    struct Handlers
    {
        std::function<void (int)> choose;                                                  // a factory preset, by program number
        std::function<void (const UserPresets::Entry&)> chooseUser;                        // one of the user's
        std::function<bool (const juce::String&, const juce::String&, Presets::Category)> save;   // true if it was saved
        std::function<void (const UserPresets::Entry&)> remove;                            // one of the user's (it has asked first)
        std::function<void()> closed;
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
        search.addKeyListener (&searchKeys);
        addAndMakeVisible (search);

        savePanel.setHandlers ({ [this] (const juce::String& name, const juce::String& detail, Presets::Category category)
                                 {
                                     if (handlers.save == nullptr || ! handlers.save (name, detail, category))
                                         return false;
                                     // Shown among the user's own, loaded.
                                     deleteArmed = false;
                                     catalog.setCurrent (catalog.getCurrent(), name);
                                     catalog.show (user);
                                     search.clear();
                                     scroll = 0.0f;
                                     refilter();
                                     scrollToShow (catalog.currentItem());
                                     return true;
                                 },
                                 [this] (const juce::String& name) { return store != nullptr && store->find (name) != nullptr; },
                                 [this]
                                 {
                                     if (isShowing())
                                         grabKeyboardFocus();
                                 } });
        addChildComponent (savePanel);
        refilter();
    }

    ~PresetBrowser() override { search.removeKeyListener (&searchKeys); }

    /** Where the user's presets are kept; the browser reads them again each time it opens. */
    void setUserPresets (UserPresets& presets)
    {
        store = &presets;
        catalog.setStore (presets);
        refilter();
    }

    void setHandlers (Handlers newHandlers) { handlers = std::move (newHandlers); }

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
        catalog.setCurrent (loaded, loadedUser);
        deleteArmed = false;
        catalog.rescan();
        refilter();
        scrollToShow (catalog.currentItem());
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
        if (handlers.closed != nullptr)
            handlers.closed();
    }

    bool isOpen() const noexcept { return isVisible(); }

    /** The loaded preset changed (stepped to with the picker's arrows, say):
        factory preset `index`, or the user's `userName` if it isn't empty. */
    void setCurrent (int index, const juce::String& userName = {})
    {
        if (! catalog.setCurrent (index, userName))
            return;
        deleteArmed = false;
        scrollToShow (catalog.currentItem());
        repaint();
    }

    /** Shows `category` (all, user, or a Presets::Category), if it has any
        presets (the user's always shows, to say how to save one). */
    void showCategory (int category)
    {
        if (! catalog.show (category))
            return;
        scroll = 0.0f;
        refilter();
        scrollToShow (catalog.currentItem());
    }

    /** Opens the save panel, on the name, line and category of the preset
        loaded if it is one of the user's, otherwise on just its category. */
    void openSavePanel()
    {
        // Another instance may have saved since the browser opened.
        catalog.rescan();
        refilter();
        if (const auto* entry = catalog.currentEntry())
            savePanel.open (entry->name, entry->detail, entry->category);
        else
            savePanel.open ({}, {}, catalog.currentCategory());
    }

    /** What is listed and loaded. */
    const PresetCatalog& getCatalog() const noexcept { return catalog; }
    bool isDeleteArmed() const noexcept    { return deleteArmed; }
    juce::TextEditor& getSearch() noexcept { return search; }
    SavePanel& getSavePanel() noexcept     { return savePanel; }

    /** Where the `row`th preset listed sits (it may be scrolled out of the list). */
    juce::Rectangle<float> rowBounds (int row) const noexcept { return layout().rowBounds (row, scroll); }

    /** Where `category`'s (all, user, or a Presets::Category) row sits. */
    juce::Rectangle<float> categoryBounds (int category) const noexcept { return layout().categoryBounds (category); }

    juce::Rectangle<float> closeBounds() const noexcept { return layout().closeBounds(); }

    /** SAVE, in the top bar, by the close cross. */
    juce::Rectangle<float> saveBounds() const noexcept { return layout().saveBounds(); }

    /** DELETE, under the details of one of the user's presets (none for a factory one). */
    std::optional<juce::Rectangle<float>> deleteBounds() const
    {
        if (catalog.currentEntry() == nullptr)
            return std::nullopt;
        return layout().deleteBounds();
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
            if (const auto next = catalog.next (key == juce::KeyPress::downKey ? 1 : -1))
                choose (*next);
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
        search.setBounds (layout().searchBounds());
        savePanel.setBounds (getLocalBounds());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto l       = layout();
        const auto removal = deleteBounds();
        const Hover now { l.rowAt (e.position, (int) catalog.getListed().size(), scroll), l.categoryAt (e.position),
                          l.closeBounds().contains (e.position), l.saveBounds().contains (e.position),
                          removal.has_value() && removal->contains (e.position) };
        if (now != hover)
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
        const auto l       = layout();
        const auto removal = deleteBounds();
        const int  row     = l.rowAt (e.position, (int) catalog.getListed().size(), scroll);
        if (l.closeBounds().contains (e.position))
            close();
        else if (l.saveBounds().contains (e.position))
            openSavePanel();
        else if (removal.has_value() && removal->contains (e.position))
            deleteCurrent();
        else if (const auto category = l.categoryAt (e.position); category.has_value())
            showCategory (*category);
        else if (row >= 0)
            choose (catalog.getListed()[(size_t) row]);
        else if (deleteArmed)
        {
            deleteArmed = false;
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (layout().rowAt (e.position, (int) catalog.getListed().size(), scroll) >= 0)
            close();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (layout().listArea().contains (e.position))
            scrollTo (scroll - wheel.deltaY * 240.0f);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        namespace Look = PresetBrowserLook;
        const float s    = uiScale;
        const auto  l    = layout();
        const auto  area = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (Palette::panelTop, 0.0f, 0.0f, Palette::panelBottom, 0.0f, area.getHeight() * 0.6f, false));
        g.fillRoundedRectangle (area, 16.0f * s);
        g.setColour (Palette::strongLine);
        g.drawRoundedRectangle (area.reduced (0.5f), 16.0f * s, juce::jmax (1.0f, s));

        // The top bar: the title, the search box (a component), the count, SAVE and the close cross.
        const auto count = (int) catalog.getListed().size();
        g.setFont (Fonts::sans (10.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText ("PRESETS", juce::Rectangle (24.0f * s, 12.0f * s, 160.0f * s, 32.0f * s), juce::Justification::centredLeft, false);
        g.setFont (Fonts::mono (10.0f * s));
        g.drawText (juce::String (count) + (count == 1 ? " PRESET" : " PRESETS"),
                    juce::Rectangle ((float) search.getRight() + 16.0f * s, 12.0f * s, 160.0f * s, 32.0f * s), juce::Justification::centredLeft, false);
        Look::paintSave (g, l.saveBounds(), hover.save, s);
        Look::paintClose (g, l.closeBounds(), hover.close, s);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (16.0f * s, 56.0f * s, area.getWidth() - 32.0f * s, juce::jmax (1.0f, s)));

        Look::paintCategories (g, l, catalog, hover.category);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (l.listArea().getX() - 10.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        Look::paintList (g, l, catalog, scroll, hover.row, search.getText());
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle (l.detailArea().getX() - 14.0f * s, 68.0f * s, juce::jmax (1.0f, s), area.getHeight() - 84.0f * s));
        if (const auto shown = catalog.currentShown())
            Look::paintDetail (g, l.detailArea(), *shown, s);
        if (const auto removal = deleteBounds())
            Look::paintDelete (g, *removal, deleteArmed, hover.remove, s);
    }

private:
    struct Hover
    {
        int row = -1;
        std::optional<int> category;
        bool close  = false;
        bool save   = false;
        bool remove = false;

        friend bool operator!= (const Hover& a, const Hover& b) noexcept
        {
            return a.row != b.row || a.category != b.category || a.close != b.close || a.save != b.save || a.remove != b.remove;
        }
    };

    // The search box sends up, down, Return and Escape here first.
    struct SearchKeys final : juce::KeyListener
    {
        explicit SearchKeys (PresetBrowser& browser) : owner (browser) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return owner.handleKey (key); }
        PresetBrowser& owner;
    };

    PresetBrowserLook::Layout layout() const noexcept { return { uiScale, (float) getWidth(), (float) getHeight() }; }

    void refilter()
    {
        catalog.refilter (search.getText());
        hover.row = -1;
        scrollTo (scroll);
        repaint();
    }

    void scrollTo (float to)
    {
        const float clamped = juce::jlimit (0.0f, layout().maxScroll ((int) catalog.getListed().size()), to);
        if (! juce::exactlyEqual (clamped, scroll))
        {
            scroll = clamped;
            repaint();
        }
    }

    // Scrolls `item` into the list's view, if it is listed.
    void scrollToShow (const Item& item)
    {
        const int row = catalog.rowOf (item);
        if (row < 0)
            return;
        const float top    = (float) row * PresetBrowserLook::rowHeight;
        const float height = layout().listArea().getHeight() / uiScale;
        if (top < scroll)
            scrollTo (top);
        else if (top + PresetBrowserLook::rowHeight > scroll + height)
            scrollTo (top + PresetBrowserLook::rowHeight - height);
    }

    void choose (const Item& item)
    {
        deleteArmed = false;
        catalog.choose (item);
        scrollToShow (item);
        repaint();
        if (const auto* entry = catalog.currentEntry(); item.isUser() && entry != nullptr && handlers.chooseUser != nullptr)
            handlers.chooseUser (*entry);
        else if (! item.isUser() && handlers.choose != nullptr)
            handlers.choose (item.factory);
    }

    // DELETE: the first click asks, the second deletes.
    void deleteCurrent()
    {
        const auto* entry = catalog.currentEntry();
        if (entry == nullptr)
            return;
        if (! deleteArmed)
        {
            deleteArmed = true;
            repaint();
            return;
        }
        deleteArmed = false;
        // (Deleting reads the folder again, so `entry` isn't used after.)
        if (handlers.remove != nullptr)
            handlers.remove (*entry);
        catalog.forgetDeleted();
        refilter();
    }

    juce::TextEditor search;
    SearchKeys searchKeys { *this };
    SavePanel savePanel;
    PresetCatalog catalog;
    UserPresets* store = nullptr;
    Handlers handlers;
    Hover hover;
    bool  deleteArmed = false;  // DELETE clicked once
    float scroll  = 0.0f;       // design pixels down the list
    float uiScale = 1.0f;
};

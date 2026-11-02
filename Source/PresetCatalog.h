#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "Presets.h"
#include "UserPresets.h"

/**
    PresetCatalog — what the preset browser lists, and which preset is loaded.

    The categories: ALL, USER (the presets the user saved) and each of
    Presets::Category. The list: the presets in the category showing that
    match the search, every word, in their name, description or category (or
    USER, for the user's own): by category, the factory's in the order they
    were added, then the user's by name. No painting. Message thread.
*/
class PresetCatalog
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
        friend bool operator== (const Item& a, const Item& b) noexcept { return a.factory == b.factory && a.saved == b.saved; }
    };

    /** What a preset is, to show: its sound, its name and line, and whether it is the user's. */
    struct Shown
    {
        const Presets::Preset& sound;
        juce::String name;
        juce::String detail;
        bool isUser;
    };

    /** Where the user's presets are kept. */
    void setStore (UserPresets& presets) noexcept { store = &presets; }

    /** Reads the user's presets again: another instance may have saved since. */
    void rescan()
    {
        if (store != nullptr)
            store->rescan();
    }

    int userCount() const noexcept { return store != nullptr ? (int) store->all().size() : 0; }

    /** How many presets `category` (all, user, or a Presets::Category) holds. */
    int countIn (int category) const noexcept
    {
        if (category == all)
            return Presets::numPresets + userCount();
        if (category == user)
            return userCount();
        const auto inCategory = [category] (const auto& preset) { return (int) preset.category == category; };
        auto count = (int) std::count_if (Presets::factory.begin(), Presets::factory.end(), inCategory);
        if (store != nullptr)
            count += (int) std::count_if (store->all().begin(), store->all().end(), inCategory);
        return count;
    }

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

    // ---- The preset loaded ---------------------------------------------------------------

    /** Factory preset `index`, or the user's `userName` if it isn't empty;
        false if it was already. */
    bool setCurrent (int index, const juce::String& userName)
    {
        if (index == current && userName == currentUser)
            return false;
        current     = index;
        currentUser = userName;
        return true;
    }

    /** `item` is loaded now. */
    void choose (const Item& item)
    {
        if (item.isUser())
        {
            currentUser = store->all()[(size_t) item.saved].name;
            return;
        }
        current = item.factory;
        currentUser.clear();
    }

    int getCurrent() const noexcept                     { return current; }
    const juce::String& getCurrentUser() const noexcept { return currentUser; }

    /** The user's preset loaded, if one is (and is still saved). */
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

    /** The preset loaded, to show, if there is one. */
    std::optional<Shown> currentShown() const
    {
        if (const auto* entry = currentEntry())
            return Shown { entry->sound, entry->name, entry->detail, true };
        if (! juce::isPositiveAndBelow (current, Presets::numPresets))
            return std::nullopt;
        const auto& preset = Presets::factory[(size_t) current];
        return Shown { preset, preset.name, preset.detail, false };
    }

    /** The loaded preset's category, for saving it. */
    Presets::Category currentCategory() const
    {
        if (const auto* entry = currentEntry())
            return entry->category;
        if (juce::isPositiveAndBelow (current, Presets::numPresets))
            return Presets::factory[(size_t) current].category;
        return Presets::Category::lead;
    }

    /** The user's preset loaded was deleted: back to the factory one, and to
        ALL if that emptied the category showing. */
    void forgetDeleted()
    {
        currentUser.clear();
        rescan();
        if (shown != all && shown != user && countIn (shown) == 0)
            shown = all;
    }

    // ---- What's listed -------------------------------------------------------------------

    /** Shows `category` (all, user, or a Presets::Category), if it has any
        presets (the user's always shows, to say how to save one); false if
        it doesn't or already did. Then refilter. */
    bool show (int category)
    {
        if (category == shown || (category != all && category != user && countIn (category) == 0))
            return false;
        shown = category;
        return true;
    }

    int getShown() const noexcept { return shown; }

    /** Lists the presets in the category showing that match `search`. */
    void refilter (const juce::String& search)
    {
        const auto words = juce::StringArray::fromTokens (search, true);
        listed.clear();
        const auto add = [this, &words] (const Item& item)
        {
            if (matches (item, words))
                listed.push_back (item);
        };
        if (shown == user)
        {
            for (int i = 0; i < userCount(); ++i)
                add ({ -1, i });
            return;
        }
        for (int category = 0; category < Presets::numCategories; ++category)
        {
            if (shown != all && shown != category)
                continue;
            for (int i = 0; i < Presets::numPresets; ++i)
                if ((int) Presets::factory[(size_t) i].category == category)
                    add ({ i, -1 });
            for (int i = 0; i < userCount(); ++i)
                if ((int) store->all()[(size_t) i].category == category)
                    add ({ -1, i });
        }
    }

    const std::vector<Item>& getListed() const noexcept { return listed; }

    /** Where `item` is in the list, or -1 if it isn't listed. */
    int rowOf (const Item& item) const
    {
        const auto at = std::find (listed.begin(), listed.end(), item);
        return at == listed.end() ? -1 : (int) (at - listed.begin());
    }

    /** The preset `direction` (1 down, -1 up) from the loaded one in the
        list, wrapping round; from one not listed, the first or last. */
    std::optional<Item> next (int direction) const
    {
        if (listed.empty())
            return std::nullopt;
        const auto count = (int) listed.size();
        const int  at    = rowOf (currentItem());
        int row = direction > 0 ? 0 : count - 1;
        if (at >= 0)
            row = (at + direction + count) % count;
        return listed[(size_t) row];
    }

private:
    bool matches (const Item& item, const juce::StringArray& words) const
    {
        const auto shownAs = describe (item);
        const juce::String text = shownAs.name + " " + shownAs.detail + " " + Presets::nameOf (shownAs.sound.category)
                                + (shownAs.isUser ? " user" : "");
        return std::all_of (words.begin(), words.end(), [&text] (const juce::String& word)
                            { return word.isEmpty() || text.containsIgnoreCase (word); });
    }

    UserPresets* store = nullptr;
    std::vector<Item> listed;
    int shown   = all;
    int current = 0;
    juce::String currentUser;   // the user's preset loaded, if not a factory one
};

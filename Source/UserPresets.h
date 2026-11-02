#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <optional>
#include <vector>

#include "Presets.h"

/**
    UserPresets — the presets you save yourself: one file each, in a folder
    of their own (~/Music/ZandersSynth2/Presets), so the app and the plug-ins
    in every host share them, and they can be copied, backed up or passed on
    as files.

    Each file holds the name it was saved under, its category, a line of
    description and the whole of the synth as it was then: every parameter,
    the bench as it was laid out and patched, and the effects. The folder is
    read afresh whenever it is asked to (rescan), so presets copied in by
    hand turn up too. A preset deleted goes to the Trash. Message thread.
*/
class UserPresets
{
public:
    static constexpr const char* extension = ".zs2preset";
    static constexpr const char* fileTag   = "ZandersSynth2Preset";
    static constexpr const char* stateTag  = "PARAMETERS";   // the processor's state tree

    struct Entry
    {
        juce::File   file;
        juce::String name;
        juce::String detail;
        Presets::Category category = Presets::Category::lead;
        Presets::Preset   sound { Presets::Category::lead, "", "", {} };   // what it holds, for the browser to show
    };

    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ZandersSynth2").getChildFile ("Presets");
    }

    explicit UserPresets (juce::File folderToUse = defaultFolder()) : folder (std::move (folderToUse)) {}

    /** Keeps them in `newFolder` instead, and, with `trashes` false, deletes
        them outright rather than to the Trash (the tests keep theirs apart). */
    void setFolder (const juce::File& newFolder, bool trashes = true)
    {
        folder      = newFolder;
        usesTrash   = trashes;
        rescan();
    }

    const juce::File& getFolder() const noexcept { return folder; }

    /** Reads the folder again: the presets in it, by name. */
    void rescan()
    {
        entries.clear();
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
            if (auto entry = read (file); entry.has_value())
                entries.push_back (std::move (*entry));
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
    }

    const std::vector<Entry>& all() const noexcept { return entries; }

    /** The one saved as `name` (whatever its case), if any. */
    const Entry* find (const juce::String& name) const
    {
        const auto at = std::find_if (entries.begin(), entries.end(), [&name] (const Entry& e) { return e.name.equalsIgnoreCase (name.trim()); });
        return at != entries.end() ? &*at : nullptr;
    }

    /** Saves `state` as `name`, replacing one already saved under that name;
        returns it as it now reads, or nothing if it couldn't be written. */
    std::optional<Entry> save (const juce::String& name, const juce::String& detail, Presets::Category category, const juce::ValueTree& state)
    {
        const auto trimmed = name.trim();
        if (trimmed.isEmpty() || ! state.isValid() || ! folder.createDirectory().wasOk())
            return std::nullopt;

        auto file = folder.getChildFile (juce::File::createLegalFileName (trimmed) + extension);
        if (const auto* existing = find (trimmed))
            file = existing->file;
        else if (file.existsAsFile())
            file = file.getNonexistentSibling();

        juce::XmlElement xml (fileTag);
        xml.setAttribute ("version", 1);
        xml.setAttribute ("name", trimmed);
        xml.setAttribute ("category", Presets::nameOf (category));
        xml.setAttribute ("detail", detail.trim());
        if (auto stateXml = state.createXml())
            xml.addChildElement (stateXml.release());
        if (! xml.writeTo (file))
            return std::nullopt;

        rescan();
        if (const auto* saved = find (trimmed))
            return *saved;
        return std::nullopt;
    }

    /** Takes `entry` away: to the Trash, or deleted outright (see setFolder). */
    bool remove (const Entry& entry)
    {
        const bool gone = usesTrash ? entry.file.moveToTrash() : entry.file.deleteFile();
        rescan();
        return gone;
    }

    /** The state saved in `file`, or an invalid tree if it holds none. */
    static juce::ValueTree stateIn (const juce::File& file)
    {
        if (const auto xml = juce::parseXMLIfTagMatches (file, fileTag))
            if (const auto* state = xml->getChildByName (stateTag))
                return juce::ValueTree::fromXml (*state);
        return {};
    }

private:
    static std::optional<Entry> read (const juce::File& file)
    {
        const auto xml = juce::parseXMLIfTagMatches (file, fileTag);
        const auto* state = xml != nullptr ? xml->getChildByName (stateTag) : nullptr;
        if (state == nullptr)
            return std::nullopt;

        Entry entry;
        entry.file     = file;
        entry.name     = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        entry.detail   = xml->getStringAttribute ("detail");
        entry.category = categoryNamed (xml->getStringAttribute ("category"));
        entry.sound    = Presets::summaryOf (juce::ValueTree::fromXml (*state));
        entry.sound.category = entry.category;
        return entry;
    }

    // A category by name; one this version doesn't know (or Init) reads as a lead.
    static Presets::Category categoryNamed (const juce::String& name)
    {
        for (int i = 1; i < Presets::numCategories; ++i)
            if (name.equalsIgnoreCase (Presets::categoryNames[(size_t) i]))
                return (Presets::Category) i;
        return Presets::Category::lead;
    }

    juce::File folder;
    bool usesTrash = true;
    std::vector<Entry> entries;
};

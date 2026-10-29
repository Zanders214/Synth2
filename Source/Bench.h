#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "ParamIDs.h"

/**
    Bench

    The workbench the editor shows: an empty space the user fills with modules
    (up to four oscillators, the unison stack and the envelope) and arranges
    freely, and, when sound follows the cables, the cables patched from the
    oscillators to the output.

    Whether a module is on the bench is its parameter (osc1On..osc4On,
    unisonOn, envelopeOn), so the engine, host undo and a restored session all
    agree on it. Where each module sits, and the cables, live in a BENCH child
    of the plugin's state tree, saved and restored with the session:

        BENCH { version }
          MODULE { id: "osc:0" | ... | "osc:3" | "unison" | "envelope", x, y }
          CABLE  { from: "osc:2", to: "out" }

    Positions are in design pixels from the bench area's top left, so they
    scale with the window. A module keeps its last position while it is off
    the bench, and comes back there.
*/
namespace Bench
{
    enum class Kind
    {
        oscillator,
        unison,
        envelope
    };

    inline constexpr int numOscillators = 4;

    /** One module: which kind, and for an oscillator, which (0..3). */
    struct ModuleId
    {
        Kind kind = Kind::oscillator;
        int  slot = 0;

        juce::String toString() const
        {
            switch (kind)
            {
                case Kind::unison:   return "unison";
                case Kind::envelope: return "envelope";
                case Kind::oscillator: break;
            }
            return "osc:" + juce::String (slot);
        }

        static std::optional<ModuleId> fromString (const juce::String& text)
        {
            if (text == "unison")   return ModuleId { Kind::unison, 0 };
            if (text == "envelope") return ModuleId { Kind::envelope, 0 };
            if (text.startsWith ("osc:"))
                if (const int slot = text.fromFirstOccurrenceOf (":", false, false).getIntValue();
                    slot >= 0 && slot < numOscillators && text.length() == 5)
                    return ModuleId { Kind::oscillator, slot };
            return std::nullopt;
        }

        friend bool operator== (const ModuleId& a, const ModuleId& b) noexcept { return a.kind == b.kind && a.slot == b.slot; }
        friend bool operator!= (const ModuleId& a, const ModuleId& b) noexcept { return ! (a == b); }
    };

    /** Every module there can be, in the order the editor stacks them. */
    inline constexpr std::array<ModuleId, 6> allModules {
        ModuleId { Kind::oscillator, 0 }, ModuleId { Kind::oscillator, 1 },
        ModuleId { Kind::oscillator, 2 }, ModuleId { Kind::oscillator, 3 },
        ModuleId { Kind::unison, 0 },     ModuleId { Kind::envelope, 0 } };

    /** The parameter that says whether `id` is on the bench. */
    inline const char* parameterFor (const ModuleId& id) noexcept
    {
        switch (id.kind)
        {
            case Kind::unison:   return ParamID::unisonOn;
            case Kind::envelope: return ParamID::envelopeOn;
            case Kind::oscillator: break;
        }
        return ParamID::oscillators[juce::jlimit (0, numOscillators - 1, id.slot)].on;
    }

    // ---- Geometry (design pixels) ------------------------------------------------

    inline constexpr float moduleWidth  = 236.0f;
    inline constexpr float moduleHeight = 240.0f;
    inline constexpr float areaWidth    = 992.0f;   // four modules and three 16 px gaps
    inline constexpr float areaHeight   = 512.0f;   // two rows, with room to spare
    inline constexpr float gap          = 16.0f;

    inline juce::Rectangle<float> boundsAt (juce::Point<float> topLeft) noexcept
    {
        return { topLeft.x, topLeft.y, moduleWidth, moduleHeight };
    }

    /** `topLeft` moved, if need be, so the whole module is inside the bench. */
    inline juce::Point<float> clampToArea (juce::Point<float> topLeft) noexcept
    {
        return { juce::jlimit (0.0f, areaWidth - moduleWidth, topLeft.x),
                 juce::jlimit (0.0f, areaHeight - moduleHeight, topLeft.y) };
    }

    /** Where a module dropped at `wanted` lands: there (inside the bench) if
        it covers none of `others`, else the nearest spot that is free. If the
        bench has no free spot big enough, it stays where it was dropped. */
    inline juce::Point<float> resolveOverlap (juce::Point<float> wanted, const std::vector<juce::Rectangle<float>>& others)
    {
        wanted = clampToArea (wanted);
        const auto isFree = [&others] (juce::Point<float> topLeft)
        {
            const auto bounds = boundsAt (topLeft);
            return std::none_of (others.begin(), others.end(), [&bounds] (const auto& other) { return bounds.intersects (other); });
        };
        if (isFree (wanted))
            return wanted;

        // Every spot on a fine grid, plus the spots flush against each module
        // (so a module can sit right next to another), nearest first.
        constexpr float step = 8.0f;
        std::vector<float> xs;
        std::vector<float> ys;
        for (float x = 0.0f; x <= areaWidth - moduleWidth; x += step)   xs.push_back (x);
        for (float y = 0.0f; y <= areaHeight - moduleHeight; y += step) ys.push_back (y);
        xs.push_back (areaWidth - moduleWidth);
        ys.push_back (areaHeight - moduleHeight);
        for (const auto& other : others)
        {
            xs.insert (xs.end(), { other.getX() - moduleWidth - gap, other.getRight() + gap, wanted.x });
            ys.insert (ys.end(), { other.getY() - moduleHeight - gap, other.getBottom() + gap, wanted.y });
        }

        std::optional<juce::Point<float>> best;
        float bestDistance = 0.0f;
        for (const float x : xs)
            for (const float y : ys)
            {
                const juce::Point<float> candidate (x, y);
                if (! juce::exactlyEqual (clampToArea (candidate).x, x) || ! juce::exactlyEqual (clampToArea (candidate).y, y))
                    continue;
                const float distance = candidate.getDistanceSquaredFrom (wanted);
                if ((! best.has_value() || distance < bestDistance) && isFree (candidate))
                {
                    best = candidate;
                    bestDistance = distance;
                }
            }
        return best.value_or (wanted);
    }

    /** The order modules at `positions` read in: row by row, top to bottom
        (a module starts a new row once it sits half a module lower than the
        row's first), each row left to right. */
    inline std::vector<size_t> readingOrder (const std::vector<juce::Point<float>>& positions)
    {
        std::vector<size_t> byHeight (positions.size());
        for (size_t i = 0; i < byHeight.size(); ++i)
            byHeight[i] = i;
        std::stable_sort (byHeight.begin(), byHeight.end(),
                          [&positions] (size_t a, size_t b) { return positions[a].y < positions[b].y; });

        std::vector<size_t> order;
        size_t rowStart = 0;
        while (rowStart < byHeight.size())
        {
            const float rowTop = positions[byHeight[rowStart]].y;
            size_t rowEnd = rowStart;
            while (rowEnd < byHeight.size() && positions[byHeight[rowEnd]].y < rowTop + moduleHeight * 0.5f)
                ++rowEnd;
            std::vector<size_t> row (byHeight.begin() + (std::ptrdiff_t) rowStart, byHeight.begin() + (std::ptrdiff_t) rowEnd);
            std::stable_sort (row.begin(), row.end(), [&positions] (size_t a, size_t b) { return positions[a].x < positions[b].x; });
            order.insert (order.end(), row.begin(), row.end());
            rowStart = rowEnd;
        }
        return order;
    }

    /** Where module `id` sits when it has no place of its own yet: the
        oscillators along the top row, unison and the envelope below. */
    inline juce::Point<float> defaultPosition (const ModuleId& id) noexcept
    {
        switch (id.kind)
        {
            case Kind::unison:   return { 0.0f, moduleHeight + gap };
            case Kind::envelope: return { moduleWidth + gap, moduleHeight + gap };
            case Kind::oscillator: break;
        }
        return { (float) id.slot * (moduleWidth + gap), 0.0f };
    }

    // ---- The model ----------------------------------------------------------------

    inline const juce::Identifier benchType  { "BENCH" };
    inline const juce::Identifier moduleType { "MODULE" };
    inline const juce::Identifier cableType  { "CABLE" };
    inline const juce::Identifier idProperty      { "id" };
    inline const juce::Identifier xProperty       { "x" };
    inline const juce::Identifier yProperty       { "y" };
    inline const juce::Identifier fromProperty    { "from" };
    inline const juce::Identifier toProperty      { "to" };
    inline const juce::Identifier versionProperty { "version" };
    inline const juce::String     outputJack      { "out" };

    /** The bench in `state`: its modules, where they sit and the cables
        between them. Message thread only. */
    class Model
    {
    public:
        explicit Model (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse) {}

        /** Whether `root` (a saved state) already has a bench; one from before
            the bench doesn't. */
        static bool hasBench (const juce::ValueTree& root) { return root.getChildWithName (benchType).isValid(); }

        /** Makes sure the state has a bench to keep the layout in. */
        void ensureBench()
        {
            auto bench = state.state.getOrCreateChildWithName (benchType, nullptr);
            bench.setProperty (versionProperty, 1, nullptr);
        }

        /** Sets out a session from before the bench as it looked then: OSC 1
            (and OSC 2, if it was on) above, unison and the envelope below. */
        void adoptClassicLayout()
        {
            ensureBench();
            for (const auto& id : allModules)
                setPosition (id, defaultPosition (id));
            for (const auto& id : { ModuleId { Kind::oscillator, 0 }, ModuleId { Kind::unison, 0 }, ModuleId { Kind::envelope, 0 } })
                setPresent (id, true);
        }

        bool isPresent (const ModuleId& id) const
        {
            return state.getRawParameterValue (parameterFor (id))->load() >= 0.5f;
        }

        /** The modules on the bench, in allModules order. */
        std::vector<ModuleId> present() const
        {
            std::vector<ModuleId> ids;
            for (const auto& id : allModules)
                if (isPresent (id))
                    ids.push_back (id);
            return ids;
        }

        bool isEmpty() const { return present().empty(); }

        juce::Point<float> positionOf (const ModuleId& id) const
        {
            const auto module = findModule (id);
            if (! module.isValid())
                return defaultPosition (id);
            return clampToArea ({ (float) module.getProperty (xProperty), (float) module.getProperty (yProperty) });
        }

        /** Whether another module of `kind` fits: four oscillators, one of each other kind. */
        bool canAdd (Kind kind) const { return freeSlot (kind).has_value(); }

        /** How many more oscillators can be added. */
        int oscillatorsLeft() const
        {
            int left = 0;
            for (int slot = 0; slot < numOscillators; ++slot)
                left += isPresent ({ Kind::oscillator, slot }) ? 0 : 1;
            return left;
        }

        std::optional<ModuleId> freeSlot (Kind kind) const
        {
            for (const auto& id : allModules)
                if (id.kind == kind && ! isPresent (id))
                    return id;
            return std::nullopt;
        }

        /** Puts a new module of `kind` on the bench at `at` (or the nearest
            free spot), switching it on as one gesture. Returns which it was. */
        std::optional<ModuleId> add (Kind kind, juce::Point<float> at)
        {
            const auto id = freeSlot (kind);
            if (! id.has_value())
                return std::nullopt;

            setPosition (*id, resolveOverlap (at, othersThan (*id)));
            setPresent (*id, true);
            return id;
        }

        /** Takes `id` off the bench, and unplugs its cables. */
        void remove (const ModuleId& id)
        {
            disconnect (id);
            setPresent (id, false);
        }

        /** Moves `id` to `to` (or the nearest free spot); returns where it landed. */
        juce::Point<float> move (const ModuleId& id, juce::Point<float> to)
        {
            const auto landed = resolveOverlap (to, othersThan (id));
            setPosition (id, landed);
            return landed;
        }

        /** The modules on the bench in the order sound passes through them
            when it follows the layout. */
        std::vector<ModuleId> readingOrder() const
        {
            const auto ids = present();
            std::vector<juce::Point<float>> positions;
            for (const auto& id : ids)
                positions.push_back (positionOf (id));

            std::vector<ModuleId> ordered;
            for (const auto index : Bench::readingOrder (positions))
                ordered.push_back (ids[index]);
            return ordered;
        }

        // ---- Cables (only oscillators to the output, for now) ---------------------

        bool isPatchedToOutput (const ModuleId& from) const { return findCable (from).isValid(); }

        /** Patches `from` (an oscillator on the bench) to the output. Returns
            false for anything that can't be patched there. */
        bool connect (const ModuleId& from)
        {
            if (from.kind != Kind::oscillator || ! isPresent (from))
                return false;
            if (isPatchedToOutput (from))
                return true;

            ensureBench();
            juce::ValueTree cable (cableType);
            cable.setProperty (fromProperty, from.toString(), nullptr);
            cable.setProperty (toProperty, outputJack, nullptr);
            bench().appendChild (cable, nullptr);
            return true;
        }

        void disconnect (const ModuleId& from)
        {
            if (auto cable = findCable (from); cable.isValid())
                bench().removeChild (cable, nullptr);
        }

        /** The oscillators patched to the output, on the bench or not. */
        std::vector<ModuleId> patchedToOutput() const
        {
            std::vector<ModuleId> ids;
            for (const auto& id : allModules)
                if (isPatchedToOutput (id))
                    ids.push_back (id);
            return ids;
        }

    private:
        juce::ValueTree bench() const { return state.state.getChildWithName (benchType); }

        juce::ValueTree findModule (const ModuleId& id) const
        {
            return bench().getChildWithProperty (idProperty, id.toString());
        }

        juce::ValueTree findCable (const ModuleId& from) const
        {
            for (const auto& child : bench())
                if (child.hasType (cableType) && child[fromProperty].toString() == from.toString()
                    && child[toProperty].toString() == outputJack)
                    return child;
            return {};
        }

        void setPosition (const ModuleId& id, juce::Point<float> topLeft)
        {
            ensureBench();
            auto module = findModule (id);
            if (! module.isValid())
            {
                module = juce::ValueTree (moduleType);
                module.setProperty (idProperty, id.toString(), nullptr);
                bench().appendChild (module, nullptr);
            }
            module.setProperty (xProperty, topLeft.x, nullptr);
            module.setProperty (yProperty, topLeft.y, nullptr);
        }

        void setPresent (const ModuleId& id, bool shouldBePresent)
        {
            auto* parameter = state.getParameter (parameterFor (id));
            if (parameter == nullptr || isPresent (id) == shouldBePresent)
                return;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (shouldBePresent ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }

        std::vector<juce::Rectangle<float>> othersThan (const ModuleId& id) const
        {
            std::vector<juce::Rectangle<float>> others;
            for (const auto& other : present())
                if (other != id)
                    others.push_back (boundsAt (positionOf (other)));
            return others;
        }

        juce::AudioProcessorValueTreeState& state;
    };
}

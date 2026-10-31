#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "ParamIDs.h"

/**
    Bench

    The workbench the editor shows: an empty space the user fills with modules
    (up to four oscillators, each with its own unison stack, up to four
    filters and up to four envelopes) and arranges freely, and, when sound
    follows the cables, the cables patched between them.

    Whether a module is on the bench is its parameter (osc1On..osc4On,
    flt1On..flt4On, env1On..env4On), so the engine, host undo and a restored
    session all agree on it. Where each module sits, and the cables, live in a BENCH child of the
    plugin's state tree, saved and restored with the session:

        BENCH { version: 2, envelopeView: "both", oscillatorView: "full" }
          MODULE { id: "osc:0" .. "osc:3" | "flt:0" .. "flt:3" | "env:0" .. "env:3", x, y }
          CABLE  { from: "osc:2", to: "flt:1" | "env:1" | "out" }
          CABLE  { from: "flt:1", to: "env:1" | "out" }
          CABLE  { from: "env:1", to: "out" }

    Positions are in design pixels from the bench area's top left, so they
    scale with the window. A module keeps its last position while it is off
    the bench, and comes back there.

    Each envelope shapes one oscillator, and each filter filters one, picked
    by how sound flows (see envelopeAssignment and filterAssignment): by
    number, by the layout, or along the cables, where an oscillator's sound
    runs through at most one filter and one envelope, in either order, to
    the output (see chainFrom).

    How the modules show themselves (see View), from the gear menu, is kept
    on the BENCH too, so a set opens looking as it was left. Presets leave it
    alone.
*/
namespace Bench
{
    enum class Kind
    {
        oscillator,
        envelope,
        filter
    };

    /** How sound passes between the modules (the signalFlow parameter's choices). */
    enum class Flow
    {
        fixed,    // ENV n shapes OSC n, FILTER n filters it
        layout,   // each envelope and filter takes an oscillator before it in reading order
        cables    // OSC -> FILTER -> ENV -> OUT (either way round), as patched
    };

    inline constexpr int numOscillators = 4;
    inline constexpr int numEnvelopes   = 4;
    inline constexpr int numFilters     = 4;

    /** One module: which kind, and which of that kind (0..3). */
    struct ModuleId
    {
        Kind kind = Kind::oscillator;
        int  slot = 0;

        juce::String toString() const
        {
            const char* prefix = kind == Kind::envelope ? "env:" : kind == Kind::filter ? "flt:" : "osc:";
            return prefix + juce::String (slot);
        }

        static std::optional<ModuleId> fromString (const juce::String& text)
        {
            if (text == "envelope")   // the one envelope of the first bench
                return ModuleId { Kind::envelope, 0 };
            const auto kind = text.upToFirstOccurrenceOf (":", false, false);
            const int  slot = text.fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (text.length() != 5 || slot < 0 || slot > 3)
                return std::nullopt;
            if (kind == "osc") return ModuleId { Kind::oscillator, slot };
            if (kind == "env") return ModuleId { Kind::envelope, slot };
            if (kind == "flt") return ModuleId { Kind::filter, slot };
            return std::nullopt;
        }

        friend bool operator== (const ModuleId& a, const ModuleId& b) noexcept { return a.kind == b.kind && a.slot == b.slot; }
        friend bool operator!= (const ModuleId& a, const ModuleId& b) noexcept { return ! (a == b); }
    };

    /** Every module there can be, in the order the editor stacks them. */
    inline constexpr std::array<ModuleId, 12> allModules {
        ModuleId { Kind::oscillator, 0 }, ModuleId { Kind::oscillator, 1 },
        ModuleId { Kind::oscillator, 2 }, ModuleId { Kind::oscillator, 3 },
        ModuleId { Kind::envelope, 0 },   ModuleId { Kind::envelope, 1 },
        ModuleId { Kind::envelope, 2 },   ModuleId { Kind::envelope, 3 },
        ModuleId { Kind::filter, 0 },     ModuleId { Kind::filter, 1 },
        ModuleId { Kind::filter, 2 },     ModuleId { Kind::filter, 3 } };

    /** The parameter that says whether `id` is on the bench. */
    inline const char* parameterFor (const ModuleId& id) noexcept
    {
        const auto slot = (size_t) juce::jlimit (0, 3, id.slot);
        switch (id.kind)
        {
            case Kind::envelope: return ParamID::envelopes[slot].on;
            case Kind::filter:   return ParamID::filters[slot].on;
            case Kind::oscillator: break;
        }
        return ParamID::oscillators[slot].on;
    }

    /** Whether a cable can go into a module of `kind` (filters and envelopes have an IN). */
    inline bool takesInput (Kind kind) noexcept { return kind != Kind::oscillator; }

    // ---- Geometry (design pixels) ------------------------------------------------

    inline constexpr float moduleWidth      = 236.0f;
    inline constexpr float oscillatorHeight = 336.0f;   // its wave well over two rows of dials
    inline constexpr float envelopeHeight   = 336.0f;   // its AMP well over two rows of dials, like an oscillator
    inline constexpr float areaWidth        = 992.0f;   // four modules and three 16 px gaps
    inline constexpr float areaHeight       = 688.0f;   // two rows of oscillators
    inline constexpr float gap              = 16.0f;
    inline constexpr float rowStep          = 120.0f;   // how much lower a module starts a new row

    inline constexpr float filterHeight     = 336.0f;   // its response well, a row of dials and its sweep well

    inline float heightOf (Kind kind) noexcept
    {
        switch (kind)
        {
            case Kind::envelope:   return envelopeHeight;
            case Kind::filter:     return filterHeight;
            case Kind::oscillator: break;
        }
        return oscillatorHeight;
    }

    inline juce::Rectangle<float> boundsAt (juce::Point<float> topLeft, Kind kind) noexcept
    {
        return { topLeft.x, topLeft.y, moduleWidth, heightOf (kind) };
    }

    /** `topLeft` moved, if need be, so the whole module is inside the bench. */
    inline juce::Point<float> clampToArea (juce::Point<float> topLeft, Kind kind) noexcept
    {
        return { juce::jlimit (0.0f, areaWidth - moduleWidth, topLeft.x),
                 juce::jlimit (0.0f, areaHeight - heightOf (kind), topLeft.y) };
    }

    /** Where a card `size` big dropped at `wanted` lands on an area `area`
        big (from its top left): there (inside the area) if it covers none of
        `others`, else the nearest spot that is free, `spacing` away from any
        card it lands beside. If the area has no free spot big enough, it
        stays where it was dropped. Shared by the bench and the effects. */
    inline juce::Point<float> resolveOverlapIn (juce::Point<float> wanted, juce::Point<float> size, juce::Point<float> area,
                                                const std::vector<juce::Rectangle<float>>& others, float spacing)
    {
        const auto clamp = [size, area] (juce::Point<float> topLeft)
        {
            return juce::Point (juce::jlimit (0.0f, area.x - size.x, topLeft.x), juce::jlimit (0.0f, area.y - size.y, topLeft.y));
        };
        wanted = clamp (wanted);
        const auto isFree = [&others, size] (juce::Point<float> topLeft)
        {
            const juce::Rectangle<float> bounds (topLeft.x, topLeft.y, size.x, size.y);
            return std::none_of (others.begin(), others.end(), [&bounds] (const auto& other) { return bounds.intersects (other); });
        };
        if (isFree (wanted))
            return wanted;

        // Every spot on a fine grid, plus the spots flush against each card
        // (so a card can sit right next to another), nearest first.
        constexpr float step = 8.0f;
        std::vector<float> xs;
        std::vector<float> ys;
        for (int i = 0; (float) i * step <= area.x - size.x; ++i) xs.push_back ((float) i * step);
        for (int i = 0; (float) i * step <= area.y - size.y; ++i) ys.push_back ((float) i * step);
        xs.push_back (area.x - size.x);
        ys.push_back (area.y - size.y);
        for (const auto& other : others)
        {
            xs.insert (xs.end(), { other.getX() - size.x - spacing, other.getRight() + spacing, other.getX(), wanted.x });
            ys.insert (ys.end(), { other.getY() - size.y - spacing, other.getBottom() + spacing, other.getY(), wanted.y });
        }

        std::optional<juce::Point<float>> best;
        float bestDistance = 0.0f;
        for (const float x : xs)
            for (const float y : ys)
            {
                const juce::Point candidate (x, y);
                if (const auto inside = clamp (candidate); ! juce::exactlyEqual (inside.x, x) || ! juce::exactlyEqual (inside.y, y))
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

    /** Where a module of `kind` dropped at `wanted` lands: there (inside the
        bench) if it covers none of `others`, else the nearest spot that is
        free. If the bench has no free spot big enough, it stays where it was
        dropped. */
    inline juce::Point<float> resolveOverlap (juce::Point<float> wanted, Kind kind, const std::vector<juce::Rectangle<float>>& others)
    {
        return resolveOverlapIn (wanted, { moduleWidth, heightOf (kind) }, { areaWidth, areaHeight }, others, gap);
    }

    /** The order modules at `positions` read in: row by row, top to bottom
        (a module starts a new row once it sits rowStep lower than the row's
        first), each row left to right. */
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
            while (rowEnd < byHeight.size() && positions[byHeight[rowEnd]].y < rowTop + rowStep)
                ++rowEnd;
            std::vector<size_t> row (byHeight.begin() + (std::ptrdiff_t) rowStart, byHeight.begin() + (std::ptrdiff_t) rowEnd);
            std::stable_sort (row.begin(), row.end(), [&positions] (size_t a, size_t b) { return positions[a].x < positions[b].x; });
            order.insert (order.end(), row.begin(), row.end());
            rowStart = rowEnd;
        }
        return order;
    }

    /** Where module `id` sits when it has no place of its own yet: the
        oscillators along the top row, each envelope under its oscillator and
        each filter under that. */
    inline juce::Point<float> defaultPosition (const ModuleId& id) noexcept
    {
        const float x = (float) id.slot * (moduleWidth + gap);
        switch (id.kind)
        {
            case Kind::envelope:   return { x, oscillatorHeight + gap };
            case Kind::filter:     return { x, oscillatorHeight + envelopeHeight + 2.0f * gap };
            case Kind::oscillator: break;
        }
        return { x, 0.0f };
    }

    // ---- How the modules show themselves -----------------------------------------------

    /** What an envelope card shows: its dials, its AMP graph (which can be
        dragged to edit), or both. */
    enum class EnvelopeView { knobs, graph, both };

    /** What an oscillator card shows: its WAVE well over its dials, or just
        a small pill with the shape's icon and name. */
    enum class OscillatorView { full, compact };

    struct View
    {
        EnvelopeView   envelopes   = EnvelopeView::both;
        OscillatorView oscillators = OscillatorView::full;

        friend bool operator== (const View& a, const View& b) noexcept
        {
            return a.envelopes == b.envelopes && a.oscillators == b.oscillators;
        }
        friend bool operator!= (const View& a, const View& b) noexcept { return ! (a == b); }
    };

    inline constexpr std::array<const char*, 3> envelopeViewNames   { "knobs", "graph", "both" };
    inline constexpr std::array<const char*, 2> oscillatorViewNames { "full", "compact" };

    // Card heights in each view (design pixels): a 30 px header, 8 px under
    // it, the well (if shown), two rows of dials (if shown) and 12 px at the
    // bottom. The bench keeps every card's full-size footprint whatever it
    // shows, so switching views never moves anything.
    inline constexpr float dialRowsHeight   = 2.0f * (12.0f + 52.0f + 31.0f);
    inline constexpr float fullWellHeight   = 96.0f;
    inline constexpr float compactWell      = 28.0f;   // the wave pill
    inline constexpr float graphOnlyWell    = fullWellHeight + dialRowsHeight;
    inline constexpr float filterWellHeight = 80.0f;   // the response well; its SWEEP well takes the second row of dials' room

    /** How tall the well of a card of `kind` is under `view` (0 for none).
        A filter's card always shows its response well, a row of dials and
        its sweep well, whatever the view. */
    inline float wellHeightOf (Kind kind, const View& view) noexcept
    {
        if (kind == Kind::filter)
            return filterWellHeight;
        if (kind == Kind::oscillator)
            return view.oscillators == OscillatorView::compact ? compactWell : fullWellHeight;
        switch (view.envelopes)
        {
            case EnvelopeView::knobs: return 0.0f;
            case EnvelopeView::graph: return graphOnlyWell;
            case EnvelopeView::both:  break;
        }
        return fullWellHeight;
    }

    /** Whether a card of `kind` shows its dials under `view`. */
    inline bool showsDials (Kind kind, const View& view) noexcept
    {
        return kind != Kind::envelope || view.envelopes != EnvelopeView::graph;
    }

    /** How tall a card of `kind` is drawn under `view`. */
    inline float cardHeightOf (Kind kind, const View& view) noexcept
    {
        if (kind == Kind::filter)
            return filterHeight;
        return 30.0f + 8.0f + wellHeightOf (kind, view) + (showsDials (kind, view) ? dialRowsHeight : 0.0f) + 12.0f;
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
    inline const juce::Identifier envelopeViewProperty   { "envelopeView" };
    inline const juce::Identifier oscillatorViewProperty { "oscillatorView" };
    inline const juce::String     outputJack      { "out" };
    inline constexpr int          currentVersion = 2;

    /** Where a cable goes: the output, or an envelope's IN. */
    using Destination = std::optional<ModuleId>;   // nullopt = the output

    class Migration;

    /** The bench in `state`: its modules, where they sit and the cables
        between them. Message thread only. */
    class Model
    {
    public:
        explicit Model (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse) {}

        /** Makes sure the state has a bench to keep the layout in. */
        void ensureBench()
        {
            auto bench = state.state.getOrCreateChildWithName (benchType, nullptr);
            if (! bench.hasProperty (versionProperty))
                bench.setProperty (versionProperty, currentVersion, nullptr);
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
            const auto node = findModule (id);
            if (! node.isValid())
                return defaultPosition (id);
            return clampToArea ({ (float) node.getProperty (xProperty), (float) node.getProperty (yProperty) }, id.kind);
        }

        juce::Rectangle<float> boundsOf (const ModuleId& id) const { return boundsAt (positionOf (id), id.kind); }

        /** Whether another module of `kind` fits: four of each. */
        bool canAdd (Kind kind) const { return freeSlot (kind).has_value(); }

        /** How many more modules of `kind` can be added. */
        int left (Kind kind) const
        {
            int count = 0;
            for (const auto& id : allModules)
                count += id.kind == kind && ! isPresent (id) ? 1 : 0;
            return count;
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

            setPosition (*id, resolveOverlap (at, kind, othersThan (*id)));
            setPresent (*id, true);
            return id;
        }

        /** Takes `id` off the bench, and unplugs its cables, in and out. */
        void remove (const ModuleId& id)
        {
            disconnect (id);
            if (takesInput (id.kind))
                disconnectInto (id);
            setPresent (id, false);
        }

        /** Moves `id` to `to` (or the nearest free spot); returns where it landed. */
        juce::Point<float> move (const ModuleId& id, juce::Point<float> to)
        {
            const auto landed = resolveOverlap (to, id.kind, othersThan (id));
            setPosition (id, landed);
            return landed;
        }

        /** Sets the modules on the bench out afresh, as a preset loads: each
            oscillator at the top of its column, with its filter (if there
            is one) under it and its envelope under that, the way sound runs
            through them; and every cable unplugged. */
        void layOut()
        {
            ensureBench();
            for (int slot = 0; slot < numOscillators; ++slot)
            {
                float y = 0.0f;
                for (const auto kind : { Kind::oscillator, Kind::filter, Kind::envelope })
                    if (const ModuleId id { kind, slot }; isPresent (id))
                    {
                        setPosition (id, { (float) slot * (moduleWidth + gap), y });
                        y += heightOf (kind) + gap;
                    }
            }
            for (const auto& [from, to] : cables())
                disconnect (from);
        }

        /** The modules on the bench in reading order. */
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

        // ---- Cables -------------------------------------------------------------

        /** Where `from`'s cable goes, if it has one. */
        std::optional<Destination> destinationOf (const ModuleId& from) const
        {
            const auto cable = findCableFrom (from);
            if (! cable.isValid())
                return std::nullopt;
            return destinationFrom (cable[toProperty].toString());
        }

        bool isPatchedToOutput (const ModuleId& from) const
        {
            const auto to = destinationOf (from);
            return to.has_value() && ! to->has_value();
        }

        /** The module patched into `module`'s IN (a filter's or an envelope's), if any. */
        std::optional<ModuleId> inputOf (const ModuleId& module) const
        {
            for (const auto& child : bench())
                if (child.hasType (cableType) && child[toProperty].toString() == module.toString())
                    return ModuleId::fromString (child[fromProperty].toString());
            return std::nullopt;
        }

        /** Patches `from` to `to` (the output, or a filter's or an
            envelope's IN), replacing the cable `from` had and any other
            cable into `to`. Any module patches to the output; into a filter
            go an oscillator or an envelope, into an envelope an oscillator
            or a filter; only modules on the bench, and never round in a
            loop. Returns false for anything that can't be patched there. */
        bool connect (const ModuleId& from, Destination to = std::nullopt)
        {
            if (! isPresent (from))
                return false;
            if (to.has_value() && (! takesInput (to->kind) || ! isPresent (*to) || ! chainsWithoutRepeats (from, *to)))
                return false;

            disconnect (from);
            if (to.has_value())
                disconnectInto (*to);
            ensureBench();
            juce::ValueTree cable (cableType);
            cable.setProperty (fromProperty, from.toString(), nullptr);
            cable.setProperty (toProperty, to.has_value() ? to->toString() : outputJack, nullptr);
            bench().appendChild (cable, nullptr);
            return true;
        }

        /** Unplugs `from`'s cable. */
        void disconnect (const ModuleId& from)   // NOSONAR(cpp:S5817): it changes the bench, through a ValueTree handle
        {
            if (auto cable = findCableFrom (from); cable.isValid())
                bench().removeChild (cable, nullptr);
        }

        /** Unplugs the cable into `module`'s IN. */
        void disconnectInto (const ModuleId& module)   // NOSONAR(cpp:S5817): it changes the bench, through a ValueTree handle
        {
            for (auto child : bench())
                if (child.hasType (cableType) && child[toProperty].toString() == module.toString())
                {
                    bench().removeChild (child, nullptr);
                    return;
                }
        }

        /** Every cable, in the order they were patched. */
        std::vector<std::pair<ModuleId, Destination>> cables() const
        {
            std::vector<std::pair<ModuleId, Destination>> all;
            for (const auto& child : bench())
            {
                if (! child.hasType (cableType))
                    continue;
                const auto from = ModuleId::fromString (child[fromProperty].toString());
                const auto to   = destinationFrom (child[toProperty].toString());
                if (from.has_value() && to.has_value())
                    all.emplace_back (*from, *to);
            }
            return all;
        }

        /** The modules patched to the output, in the order they were patched. */
        std::vector<ModuleId> patchedToOutput() const
        {
            std::vector<ModuleId> ids;
            for (const auto& [from, to] : cables())
                if (! to.has_value())
                    ids.push_back (from);
            return ids;
        }

        // ---- What sound does ------------------------------------------------------

        /** Where an oscillator's sound runs along the cables: the filter and
            the envelope it goes through (-1 for none), and whether it gets
            to the output. */
        struct Chain
        {
            int  filter   = -1;
            int  envelope = -1;
            bool heard    = false;
        };

        /** Follows OSC `oscillator`'s cable, through at most one filter and
            one envelope, either way round, to the output. */
        Chain chainFrom (int oscillator) const
        {
            Chain chain;
            ModuleId at { Kind::oscillator, oscillator };
            if (! isPresent (at))
                return chain;
            for (int hop = 0; hop < 3; ++hop)
            {
                const auto to = destinationOf (at);
                if (! to.has_value())
                    return chain;
                if (! to->has_value())
                {
                    chain.heard = true;
                    return chain;
                }
                at = **to;
                auto& slot = at.kind == Kind::filter ? chain.filter : chain.envelope;
                if (at.kind == Kind::oscillator || slot >= 0 || ! isPresent (at))
                    return chain;
                slot = at.slot;
            }
            return chain;
        }

        /** Which envelope (0..3, or -1 for none) shapes each oscillator when
            sound flows by `flow`:
              fixed:  ENV n shapes OSC n, when both are on the bench;
              layout: going through the bench in reading order, each envelope
                      takes the first oscillator before it that has none yet;
              cables: an envelope shapes the oscillator whose chain runs
                      through it. */
        std::array<int, numOscillators> envelopeAssignment (Flow flow) const
        {
            return assignment (Kind::envelope, flow);
        }

        /** Which filter (0..3, or -1 for none) filters each oscillator when
            sound flows by `flow`, picked the way envelopes are (see
            envelopeAssignment). */
        std::array<int, numOscillators> filterAssignment (Flow flow) const
        {
            return assignment (Kind::filter, flow);
        }

        /** Whether each oscillator reaches the output along the cables:
            patched to it, or through a filter or an envelope (or both) that
            are. */
        std::array<bool, numOscillators> heardThroughCables() const
        {
            std::array<bool, numOscillators> heard {};
            for (int k = 0; k < numOscillators; ++k)
                heard[(size_t) k] = chainFrom (k).heard;
            return heard;
        }

        // ---- How the modules show themselves ---------------------------------------

        /** The view the gear menu picked (both and full when none was saved). */
        View view() const
        {
            const auto tree  = bench();
            const auto index = [&tree] (const juce::Identifier& property, const auto& names, int fallback)
            {
                const auto name = tree.getProperty (property).toString();
                for (size_t i = 0; i < names.size(); ++i)
                    if (name == names[i])
                        return (int) i;
                return fallback;
            };
            return { (EnvelopeView) index (envelopeViewProperty, envelopeViewNames, (int) EnvelopeView::both),
                     (OscillatorView) index (oscillatorViewProperty, oscillatorViewNames, (int) OscillatorView::full) };
        }

        void setView (const View& newView)
        {
            ensureBench();
            auto tree = bench();
            tree.setProperty (envelopeViewProperty, envelopeViewNames[(size_t) newView.envelopes], nullptr);
            tree.setProperty (oscillatorViewProperty, oscillatorViewNames[(size_t) newView.oscillators], nullptr);
        }

    private:
        juce::ValueTree bench() const { return state.state.getChildWithName (benchType); }

        static std::optional<Destination> destinationFrom (const juce::String& text)
        {
            if (text == outputJack)
                return Destination {};
            if (const auto module = ModuleId::fromString (text); module.has_value() && takesInput (module->kind))
                return Destination { *module };
            return std::nullopt;
        }

        /** Whether patching `from` into `into` keeps every chain to one
            module of each kind: walking back from `from` and on from `into`
            meets no kind twice, which also rules out patching round in a
            loop. */
        bool chainsWithoutRepeats (const ModuleId& from, const ModuleId& into) const
        {
            std::array<bool, 3> seen {};
            const auto meet = [&seen] (const ModuleId& id)
            {
                auto& met = seen[(size_t) id.kind];
                return ! std::exchange (met, true);
            };
            std::optional<ModuleId> back = from;
            for (int hop = 0; back.has_value() && hop < 3; ++hop, back = inputOf (*back))
                if (! meet (*back))
                    return false;
            Destination on = into;
            for (int hop = 0; on.has_value() && hop < 3; ++hop)
            {
                if (! meet (*on))
                    return false;
                const auto next = destinationOf (*on);
                on = next.has_value() ? *next : Destination {};
            }
            return true;
        }

        std::array<int, numOscillators> assignment (Kind kind, Flow flow) const
        {
            std::array<int, numOscillators> takenBy { -1, -1, -1, -1 };
            if (flow == Flow::fixed)
            {
                for (int k = 0; k < numOscillators; ++k)
                    if (isPresent ({ kind, k }) && isPresent ({ Kind::oscillator, k }))
                        takenBy[(size_t) k] = k;
            }
            else if (flow == Flow::layout)
            {
                std::vector<int> waiting;
                for (const auto& id : readingOrder())
                {
                    if (id.kind == Kind::oscillator)
                        waiting.push_back (id.slot);
                    else if (id.kind == kind && ! waiting.empty())
                    {
                        takenBy[(size_t) waiting.front()] = id.slot;
                        waiting.erase (waiting.begin());
                    }
                }
            }
            else
            {
                for (int k = 0; k < numOscillators; ++k)
                {
                    const auto chain = chainFrom (k);
                    takenBy[(size_t) k] = kind == Kind::filter ? chain.filter : chain.envelope;
                }
            }
            return takenBy;
        }

        juce::ValueTree findModule (const ModuleId& id) const
        {
            return bench().getChildWithProperty (idProperty, id.toString());
        }

        juce::ValueTree findCableFrom (const ModuleId& from) const
        {
            for (const auto& child : bench())
                if (child.hasType (cableType) && child[fromProperty].toString() == from.toString())
                    return child;
            return {};
        }

        void setPosition (const ModuleId& id, juce::Point<float> topLeft)
        {
            ensureBench();
            auto node = findModule (id);
            if (! node.isValid())
            {
                node = juce::ValueTree (moduleType);
                node.setProperty (idProperty, id.toString(), nullptr);
                bench().appendChild (node, nullptr);
            }
            node.setProperty (xProperty, topLeft.x, nullptr);
            node.setProperty (yProperty, topLeft.y, nullptr);
        }

        void setPresent (const ModuleId& id, bool shouldBePresent)   // NOSONAR(cpp:S5817): it changes a parameter
        {
            if (isPresent (id) == shouldBePresent)
                return;
            auto* parameter = state.getParameter (parameterFor (id));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (shouldBePresent ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }

        std::vector<juce::Rectangle<float>> othersThan (const ModuleId& id) const
        {
            std::vector<juce::Rectangle<float>> others;
            for (const auto& other : present())
                if (other != id)
                    others.push_back (boundsOf (other));
            return others;
        }

        juce::AudioProcessorValueTreeState& state;

        friend class Migration;   // moves modules and switches them on as a restored session needs
    };

    /** Brings the bench in a restored session up to date (see migrate), or
        sets one out as it looked before there was a bench. Message thread
        only. */
    class Migration
    {
    public:
        explicit Migration (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse), model (stateToUse) {}

        /** Brings a restored session up to date. `saved` is the state as it
            was saved (before the parameters it lacked were filled in): one
            from before the bench is set out as it looked then, and one from
            the first bench (one unison stack and one envelope for every
            oscillator) gets each oscillator its own of both. */
        void migrate (const juce::ValueTree& saved)
        {
            if (const auto savedBench = saved.getChildWithName (benchType); ! savedBench.isValid())
                adoptClassicLayout (saved);
            else if ((int) savedBench.getProperty (versionProperty, 1) < currentVersion)
                adoptFirstBench (saved);
            model.ensureBench();
            model.bench().setProperty (versionProperty, currentVersion, nullptr);
        }

        /** Sets out a session from before the bench as it looked then: OSC 1
            (and the other oscillators that were on) along the top, each under
            its own envelope, and each with the unison stack it shared then.
            OSC 1 and the envelope were always there, so they go on the bench,
            unless `saved` already switched them on or off itself. */
        void adoptClassicLayout (const juce::ValueTree& saved = {})
        {
            model.ensureBench();
            for (const auto& id : allModules)
                model.setPosition (id, defaultPosition (id));

            if (! hasSaved (saved, ParamID::osc1On))
                model.setPresent ({ Kind::oscillator, 0 }, true);
            const bool shaped = savedValue (saved, ParamID::envelopeOn, 1.0f) >= 0.5f;
            if (! hasSaved (saved, ParamID::envelope (0).on))
                model.setPresent ({ Kind::envelope, 0 }, shaped);

            shareUnison (savedValue (saved, ParamID::unisonOn, 1.0f) >= 0.5f);
            if (model.isPresent ({ Kind::envelope, 0 }))
                shareEnvelope();
        }

    private:
        void setValue (const char* id, float value)   // NOSONAR(cpp:S5817): it changes a parameter
        {
            auto* parameter = state.getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }

        float valueOf (const char* id) const { return state.getRawParameterValue (id)->load(); }

        static bool hasSaved (const juce::ValueTree& saved, const char* id)
        {
            return saved.getChildWithProperty (idProperty, juce::String (id)).isValid();
        }

        static float savedValue (const juce::ValueTree& saved, const char* id, float fallback)
        {
            const auto parameter = saved.getChildWithProperty (idProperty, juce::String (id));
            return parameter.isValid() ? (float) parameter.getProperty ("value", fallback) : fallback;
        }

        // The one unison stack every oscillator shared (OSC 1's parameters
        // now) becomes each oscillator's own; without it, each plays one voice.
        void shareUnison (bool stacked)
        {
            const float voices = stacked ? valueOf (ParamID::osc1Voices) : 1.0f;
            for (const auto& ids : ParamID::oscillators)
            {
                setValue (ids.voices, voices);
                setValue (ids.detune, valueOf (ParamID::osc1Detune));
                setValue (ids.width, valueOf (ParamID::osc1Width));
            }
        }

        // The one envelope every oscillator shared (ENV 1 now) goes under each
        // other oscillator on the bench too, as a copy, at the nearest free spot.
        void shareEnvelope()
        {
            for (int k = 1; k < numOscillators; ++k)
            {
                const ModuleId env { Kind::envelope, k };
                if (! model.isPresent ({ Kind::oscillator, k }) || model.isPresent (env))
                    continue;
                const auto& from = ParamID::envelope (0);
                const auto& to   = ParamID::envelope (k);
                for (const auto& [target, source] : { std::pair (to.delay, from.delay), std::pair (to.attack, from.attack),
                                                      std::pair (to.hold, from.hold), std::pair (to.decay, from.decay),
                                                      std::pair (to.sustain, from.sustain), std::pair (to.release, from.release) })
                    setValue (target, valueOf (source));
                model.setPosition (env, resolveOverlap (model.positionOf (env), Kind::envelope, model.othersThan (env)));
                model.setPresent (env, true);
            }
        }

        // The first bench: its unison module goes (each oscillator getting the
        // stack it played), its envelope becomes ENV 1 (and a copy for each
        // other oscillator), oscillators patched to the output go through
        // their envelope, and everything is moved off anything it now covers.
        void adoptFirstBench (const juce::ValueTree& saved)
        {
            auto first = model.bench();
            if (auto unison = first.getChildWithProperty (idProperty, "unison"); unison.isValid())
                first.removeChild (unison, nullptr);
            if (auto envelope = first.getChildWithProperty (idProperty, "envelope"); envelope.isValid())
                envelope.setProperty (idProperty, ModuleId { Kind::envelope, 0 }.toString(), nullptr);

            shareUnison (savedValue (saved, ParamID::unisonOn, 0.0f) >= 0.5f);
            model.setPresent ({ Kind::envelope, 0 }, savedValue (saved, ParamID::envelopeOn, 0.0f) >= 0.5f);

            std::vector<juce::Rectangle<float>> placed;
            for (const auto& id : model.readingOrder())
            {
                const auto landed = resolveOverlap (model.positionOf (id), id.kind, placed);
                model.setPosition (id, landed);
                placed.push_back (boundsAt (landed, id.kind));
            }
            if (model.isPresent ({ Kind::envelope, 0 }))
                shareEnvelope();

            for (const auto& from : model.patchedToOutput())
                if (const ModuleId env { Kind::envelope, from.slot }; from.kind == Kind::oscillator && model.isPresent (env))
                {
                    model.connect (from, env);
                    model.connect (env);
                }
        }

        juce::AudioProcessorValueTreeState& state;
        Model model;
    };
}

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
    (up to four oscillators, each with its own unison stack, and up to four
    envelopes) and arranges freely, and, when sound follows the cables, the
    cables patched between them.

    Whether a module is on the bench is its parameter (osc1On..osc4On,
    env1On..env4On), so the engine, host undo and a restored session all agree
    on it. Where each module sits, and the cables, live in a BENCH child of the
    plugin's state tree, saved and restored with the session:

        BENCH { version: 2, envelopeView: "both", oscillatorView: "full" }
          MODULE { id: "osc:0" .. "osc:3" | "env:0" .. "env:3", x, y }
          CABLE  { from: "osc:2", to: "env:1" | "out" }
          CABLE  { from: "env:1", to: "out" }

    Positions are in design pixels from the bench area's top left, so they
    scale with the window. A module keeps its last position while it is off
    the bench, and comes back there.

    Each envelope shapes one oscillator, picked by how sound flows (see
    envelopeAssignment): by number, by the layout, or along the cables.

    How the modules show themselves (see View), from the gear menu, is kept
    on the BENCH too, so a set opens looking as it was left. Presets leave it
    alone.
*/
namespace Bench
{
    enum class Kind
    {
        oscillator,
        envelope
    };

    /** How sound passes between the modules (the signalFlow parameter's choices). */
    enum class Flow
    {
        fixed,    // ENV n shapes OSC n
        layout,   // each envelope shapes an oscillator before it in reading order
        cables    // OSC -> ENV -> OUT, as patched
    };

    inline constexpr int numOscillators = 4;
    inline constexpr int numEnvelopes   = 4;

    /** One module: which kind, and which of that kind (0..3). */
    struct ModuleId
    {
        Kind kind = Kind::oscillator;
        int  slot = 0;

        juce::String toString() const { return (kind == Kind::envelope ? "env:" : "osc:") + juce::String (slot); }

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
            return std::nullopt;
        }

        friend bool operator== (const ModuleId& a, const ModuleId& b) noexcept { return a.kind == b.kind && a.slot == b.slot; }
        friend bool operator!= (const ModuleId& a, const ModuleId& b) noexcept { return ! (a == b); }
    };

    /** Every module there can be, in the order the editor stacks them. */
    inline constexpr std::array<ModuleId, 8> allModules {
        ModuleId { Kind::oscillator, 0 }, ModuleId { Kind::oscillator, 1 },
        ModuleId { Kind::oscillator, 2 }, ModuleId { Kind::oscillator, 3 },
        ModuleId { Kind::envelope, 0 },   ModuleId { Kind::envelope, 1 },
        ModuleId { Kind::envelope, 2 },   ModuleId { Kind::envelope, 3 } };

    /** The parameter that says whether `id` is on the bench. */
    inline const char* parameterFor (const ModuleId& id) noexcept
    {
        const auto slot = (size_t) juce::jlimit (0, 3, id.slot);
        return id.kind == Kind::envelope ? ParamID::envelopes[slot].on : ParamID::oscillators[slot].on;
    }

    // ---- Geometry (design pixels) ------------------------------------------------

    inline constexpr float moduleWidth      = 236.0f;
    inline constexpr float oscillatorHeight = 336.0f;   // its wave well over two rows of dials
    inline constexpr float envelopeHeight   = 336.0f;   // its AMP well over two rows of dials, like an oscillator
    inline constexpr float areaWidth        = 992.0f;   // four modules and three 16 px gaps
    inline constexpr float areaHeight       = 688.0f;   // two rows of oscillators
    inline constexpr float gap              = 16.0f;
    inline constexpr float rowStep          = 120.0f;   // how much lower a module starts a new row

    inline float heightOf (Kind kind) noexcept { return kind == Kind::envelope ? envelopeHeight : oscillatorHeight; }

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

    /** Where a module of `kind` dropped at `wanted` lands: there (inside the
        bench) if it covers none of `others`, else the nearest spot that is
        free. If the bench has no free spot big enough, it stays where it was
        dropped. */
    inline juce::Point<float> resolveOverlap (juce::Point<float> wanted, Kind kind, const std::vector<juce::Rectangle<float>>& others)
    {
        wanted = clampToArea (wanted, kind);
        const float height = heightOf (kind);
        const auto isFree = [&others, kind] (juce::Point<float> topLeft)
        {
            const auto bounds = boundsAt (topLeft, kind);
            return std::none_of (others.begin(), others.end(), [&bounds] (const auto& other) { return bounds.intersects (other); });
        };
        if (isFree (wanted))
            return wanted;

        // Every spot on a fine grid, plus the spots flush against each module
        // (so a module can sit right next to another), nearest first.
        constexpr float step = 8.0f;
        std::vector<float> xs;
        std::vector<float> ys;
        for (float x = 0.0f; x <= areaWidth - moduleWidth; x += step) xs.push_back (x);
        for (float y = 0.0f; y <= areaHeight - height; y += step)     ys.push_back (y);
        xs.push_back (areaWidth - moduleWidth);
        ys.push_back (areaHeight - height);
        for (const auto& other : others)
        {
            xs.insert (xs.end(), { other.getX() - moduleWidth - gap, other.getRight() + gap, other.getX(), wanted.x });
            ys.insert (ys.end(), { other.getY() - height - gap, other.getBottom() + gap, other.getY(), wanted.y });
        }

        std::optional<juce::Point<float>> best;
        float bestDistance = 0.0f;
        for (const float x : xs)
            for (const float y : ys)
            {
                const juce::Point<float> candidate (x, y);
                const auto inside = clampToArea (candidate, kind);
                if (! juce::exactlyEqual (inside.x, x) || ! juce::exactlyEqual (inside.y, y))
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
        oscillators along the top row, each envelope under its oscillator. */
    inline juce::Point<float> defaultPosition (const ModuleId& id) noexcept
    {
        const float x = (float) id.slot * (moduleWidth + gap);
        return { x, id.kind == Kind::envelope ? oscillatorHeight + gap : 0.0f };
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

    /** How tall the well of a card of `kind` is under `view` (0 for none). */
    inline float wellHeightOf (Kind kind, const View& view) noexcept
    {
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
        return kind == Kind::oscillator || view.envelopes != EnvelopeView::graph;
    }

    /** How tall a card of `kind` is drawn under `view`. */
    inline float cardHeightOf (Kind kind, const View& view) noexcept
    {
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

        /** Brings a restored session up to date. `saved` is the state as it
            was saved (before the parameters it lacked were filled in): one
            from before the bench is set out as it looked then, and one from
            the first bench (one unison stack and one envelope for every
            oscillator) gets each oscillator its own of both. */
        void migrate (const juce::ValueTree& saved)
        {
            const auto savedBench = saved.getChildWithName (benchType);
            if (! savedBench.isValid())
                adoptClassicLayout (saved);
            else if ((int) savedBench.getProperty (versionProperty, 1) < currentVersion)
                adoptFirstBench (saved);
            ensureBench();
            bench().setProperty (versionProperty, currentVersion, nullptr);
        }

        /** Sets out a session from before the bench as it looked then: OSC 1
            (and the other oscillators that were on) along the top, each under
            its own envelope, and each with the unison stack it shared then.
            OSC 1 and the envelope were always there, so they go on the bench,
            unless `saved` already switched them on or off itself. */
        void adoptClassicLayout (const juce::ValueTree& saved = {})
        {
            ensureBench();
            for (const auto& id : allModules)
                setPosition (id, defaultPosition (id));

            if (! hasSaved (saved, ParamID::osc1On))
                setPresent ({ Kind::oscillator, 0 }, true);
            const bool shaped = savedValue (saved, ParamID::envelopeOn, 1.0f) >= 0.5f;
            if (! hasSaved (saved, ParamID::envelopes[0].on))
                setPresent ({ Kind::envelope, 0 }, shaped);

            shareUnison (savedValue (saved, ParamID::unisonOn, 1.0f) >= 0.5f);
            if (isPresent ({ Kind::envelope, 0 }))
                shareEnvelope();
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
            return clampToArea ({ (float) module.getProperty (xProperty), (float) module.getProperty (yProperty) }, id.kind);
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
            if (id.kind == Kind::envelope)
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

        /** Sets the modules on the bench out afresh, as a preset loads: the
            oscillators along the top row, each envelope under its
            oscillator, and every cable unplugged. */
        void layOut()
        {
            ensureBench();
            for (const auto& id : present())
                setPosition (id, defaultPosition (id));
            for (const auto& cable : cables())
                disconnect (cable.first);
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

        /** The oscillator patched into envelope `env`'s IN, if any. */
        std::optional<ModuleId> inputOf (const ModuleId& env) const
        {
            for (const auto& child : bench())
                if (child.hasType (cableType) && child[toProperty].toString() == env.toString())
                    return ModuleId::fromString (child[fromProperty].toString());
            return std::nullopt;
        }

        /** Patches `from` to `to` (the output, or an envelope's IN), replacing
            the cable `from` had and any other cable into that envelope. An
            oscillator patches to the output or an envelope, an envelope to the
            output, and only modules on the bench. Returns false for anything
            that can't be patched there. */
        bool connect (const ModuleId& from, Destination to = std::nullopt)
        {
            if (! isPresent (from) || (to.has_value() && (from.kind != Kind::oscillator || to->kind != Kind::envelope || ! isPresent (*to))))
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
        void disconnect (const ModuleId& from)
        {
            if (auto cable = findCableFrom (from); cable.isValid())
                bench().removeChild (cable, nullptr);
        }

        /** Unplugs the cable into envelope `env`'s IN. */
        void disconnectInto (const ModuleId& env)
        {
            for (auto child : bench())
                if (child.hasType (cableType) && child[toProperty].toString() == env.toString())
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
                if (child.hasType (cableType))
                    if (const auto from = ModuleId::fromString (child[fromProperty].toString()))
                        if (const auto to = destinationFrom (child[toProperty].toString()))
                            all.emplace_back (*from, *to);
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

        /** Which envelope (0..3, or -1 for none) shapes each oscillator when
            sound flows by `flow`:
              fixed:  ENV n shapes OSC n, when both are on the bench;
              layout: going through the bench in reading order, each envelope
                      takes the first oscillator before it that has none yet;
              cables: an envelope shapes the oscillator patched into it. */
        std::array<int, numOscillators> envelopeAssignment (Flow flow) const
        {
            std::array<int, numOscillators> shapedBy { -1, -1, -1, -1 };
            if (flow == Flow::fixed)
            {
                for (int k = 0; k < numOscillators; ++k)
                    if (isPresent ({ Kind::envelope, k }) && isPresent ({ Kind::oscillator, k }))
                        shapedBy[(size_t) k] = k;
            }
            else if (flow == Flow::layout)
            {
                std::vector<int> waiting;
                for (const auto& id : readingOrder())
                {
                    if (id.kind == Kind::oscillator)
                        waiting.push_back (id.slot);
                    else if (! waiting.empty())
                    {
                        shapedBy[(size_t) waiting.front()] = id.slot;
                        waiting.erase (waiting.begin());
                    }
                }
            }
            else
            {
                for (int e = 0; e < numEnvelopes; ++e)
                    if (const auto input = inputOf ({ Kind::envelope, e }); input.has_value() && isPresent ({ Kind::envelope, e }))
                        shapedBy[(size_t) input->slot] = e;
            }
            return shapedBy;
        }

        /** Whether each oscillator reaches the output along the cables:
            patched to it, or to an envelope that is. */
        std::array<bool, numOscillators> heardThroughCables() const
        {
            std::array<bool, numOscillators> heard {};
            for (int k = 0; k < numOscillators; ++k)
            {
                const ModuleId osc { Kind::oscillator, k };
                const auto to = destinationOf (osc);
                if (! to.has_value() || ! isPresent (osc))
                    continue;
                heard[(size_t) k] = ! to->has_value() || isPatchedToOutput (**to);
            }
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
            if (const auto env = ModuleId::fromString (text); env.has_value() && env->kind == Kind::envelope)
                return Destination { *env };
            return std::nullopt;
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
            if (isPresent (id) == shouldBePresent)
                return;
            auto* parameter = state.getParameter (parameterFor (id));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (shouldBePresent ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }

        void setValue (const char* id, float value)
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
            const float voices = stacked ? valueOf (ParamID::voices) : 1.0f;
            for (int k = 0; k < numOscillators; ++k)
            {
                const auto& ids = ParamID::oscillators[k];
                setValue (ids.voices, voices);
                setValue (ids.detune, valueOf (ParamID::detune));
                setValue (ids.width, valueOf (ParamID::width));
            }
        }

        // The one envelope every oscillator shared (ENV 1 now) goes under each
        // other oscillator on the bench too, as a copy, at the nearest free spot.
        void shareEnvelope()
        {
            for (int k = 1; k < numOscillators; ++k)
            {
                const ModuleId env { Kind::envelope, k };
                if (! isPresent ({ Kind::oscillator, k }) || isPresent (env))
                    continue;
                const auto& from = ParamID::envelopes[0];
                const auto& to   = ParamID::envelopes[k];
                for (const auto& [target, source] : { std::pair (to.delay, from.delay), std::pair (to.attack, from.attack),
                                                      std::pair (to.hold, from.hold), std::pair (to.decay, from.decay),
                                                      std::pair (to.sustain, from.sustain), std::pair (to.release, from.release) })
                    setValue (target, valueOf (source));
                setPosition (env, resolveOverlap (positionOf (env), Kind::envelope, othersThan (env)));
                setPresent (env, true);
            }
        }

        // The first bench: its unison module goes (each oscillator getting the
        // stack it played), its envelope becomes ENV 1 (and a copy for each
        // other oscillator), oscillators patched to the output go through
        // their envelope, and everything is moved off anything it now covers.
        void adoptFirstBench (const juce::ValueTree& saved)
        {
            auto first = bench();
            if (auto unison = first.getChildWithProperty (idProperty, "unison"); unison.isValid())
                first.removeChild (unison, nullptr);
            if (auto envelope = first.getChildWithProperty (idProperty, "envelope"); envelope.isValid())
                envelope.setProperty (idProperty, ModuleId { Kind::envelope, 0 }.toString(), nullptr);

            shareUnison (savedValue (saved, ParamID::unisonOn, 0.0f) >= 0.5f);
            setPresent ({ Kind::envelope, 0 }, savedValue (saved, ParamID::envelopeOn, 0.0f) >= 0.5f);

            std::vector<juce::Rectangle<float>> placed;
            for (const auto& id : readingOrder())
            {
                const auto landed = resolveOverlap (positionOf (id), id.kind, placed);
                setPosition (id, landed);
                placed.push_back (boundsAt (landed, id.kind));
            }
            if (isPresent ({ Kind::envelope, 0 }))
                shareEnvelope();

            for (const auto& from : patchedToOutput())
                if (const ModuleId env { Kind::envelope, from.slot }; from.kind == Kind::oscillator && isPresent (env))
                {
                    connect (from, env);
                    connect (env);
                }
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
    };
}

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "Bench.h"
#include "EffectSettings.h"
#include "ParamIDs.h"

/**
    Rack

    The EFFECTS tab, laid out like the bench: an empty space the user puts
    effects on (a drive, a delay and a reverb, one of each) and arranges
    freely. Every note goes through the effects in the chain in the order
    they read in, row by row and left to right, so moving a card moves its
    effect along the chain; dropping a card on another swaps the two.

    Whether an effect is in the chain is its parameter (driveInChain,
    delayInChain, reverbInChain), so the engine, host undo and a restored
    session all agree on it; its power switch (driveOn, delayOn, reverbOn)
    bypasses it without taking it out. Where each card sits lives in a RACK
    child of the plugin's state tree, saved and restored with the session:

        RACK { version: 1 }
          EFFECT { id: "drive" | "delay" | "reverb", x, y }

    Positions are in design pixels from the rack area's top left (under the
    line spelling out the chain), so they scale with the window. An effect
    keeps its last position while it is out of the chain, and comes back
    there.
*/
namespace Rack
{
    using Effects::Kind;

    /** Every effect there can be, in the order a new chain plays them. */
    inline constexpr std::array<Kind, (size_t) Effects::numKinds> allEffects = Effects::defaultOrder;

    /** The name `kind` is saved under. */
    inline const char* idOf (Kind kind) noexcept
    {
        switch (kind)
        {
            case Kind::delay:  return "delay";
            case Kind::reverb: return "reverb";
            case Kind::drive:  break;
        }
        return "drive";
    }

    inline std::optional<Kind> kindFromId (const juce::String& text)
    {
        for (const auto kind : allEffects)
            if (text == idOf (kind))
                return kind;
        return std::nullopt;
    }

    /** The parameter that says whether `kind` is in the chain. */
    inline const char* inChainParameter (Kind kind) noexcept
    {
        switch (kind)
        {
            case Kind::delay:  return ParamID::delayInChain;
            case Kind::reverb: return ParamID::reverbInChain;
            case Kind::drive:  break;
        }
        return ParamID::driveInChain;
    }

    /** And the one that is its power switch. */
    inline const char* powerParameter (Kind kind) noexcept
    {
        switch (kind)
        {
            case Kind::delay:  return ParamID::delayOn;
            case Kind::reverb: return ParamID::reverbOn;
            case Kind::drive:  break;
        }
        return ParamID::driveOn;
    }

    // ---- Geometry (design pixels) ------------------------------------------------

    inline constexpr float cardWidth   = 292.0f;
    inline constexpr float cardHeight  = 448.0f;
    inline constexpr float chainHeight = 40.0f;                                // the line spelling out the chain, over the area
    inline constexpr float areaWidth   = Bench::areaWidth;
    inline constexpr float areaHeight  = Bench::areaHeight - chainHeight;
    inline constexpr float gap         = 29.0f;                                // room for an arrow between two cards

    inline juce::Rectangle<float> boundsAt (juce::Point<float> topLeft) noexcept
    {
        return { topLeft.x, topLeft.y, cardWidth, cardHeight };
    }

    /** Where the card in place `place` (0..2) of a chain set out afresh sits:
        side by side, across the middle of the area. */
    inline juce::Point<float> defaultPosition (int place) noexcept
    {
        return { gap + (float) juce::jlimit (0, 2, place) * (cardWidth + gap), 0.5f * (areaHeight - cardHeight) };
    }

    inline juce::Point<float> defaultPosition (Kind kind) noexcept { return defaultPosition ((int) kind); }

    /** Where a card dropped at `wanted` lands: there, or the nearest free spot. */
    inline juce::Point<float> resolveOverlap (juce::Point<float> wanted, const std::vector<juce::Rectangle<float>>& others)
    {
        return Bench::resolveOverlapIn (wanted, { cardWidth, cardHeight }, { areaWidth, areaHeight }, others, gap);
    }

    // ---- The model ----------------------------------------------------------------

    inline const juce::Identifier rackType   { "RACK" };
    inline const juce::Identifier effectType { "EFFECT" };
    inline const juce::Identifier idProperty      { "id" };
    inline const juce::Identifier xProperty       { "x" };
    inline const juce::Identifier yProperty       { "y" };
    inline const juce::Identifier versionProperty { "version" };
    inline constexpr int          currentVersion = 1;

    /** The effects in `state`: which are in the chain, where their cards
        sit, and so the order sound goes through them. Message thread only. */
    class Model
    {
    public:
        explicit Model (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse) {}

        /** Makes sure the state has a rack to keep the layout in. */
        void ensureRack()
        {
            auto rack = state.state.getOrCreateChildWithName (rackType, nullptr);
            if (! rack.hasProperty (versionProperty))
                rack.setProperty (versionProperty, currentVersion, nullptr);
        }

        /** Brings a restored session up to date. `saved` is the state as it
            was saved: one from before the rack had all three effects on the
            tab, side by side in the order they played (the drive, then the
            delay and the reverb in either order), so it opens as it was. */
        void migrate (const juce::ValueTree& saved)
        {
            if (! saved.getChildWithName (rackType).isValid())
            {
                ensureRack();
                const auto order = savedValue (saved, ParamID::effectsOrder, 0.0f) >= 0.5f
                                 ? Effects::Order { Kind::drive, Kind::reverb, Kind::delay }
                                 : Effects::defaultOrder;
                for (size_t place = 0; place < order.size(); ++place)
                {
                    setPosition (order[place], defaultPosition ((int) place));
                    setParameter (inChainParameter (order[place]), true);
                }
            }
            ensureRack();
        }

        bool isInChain (Kind kind) const { return state.getRawParameterValue (inChainParameter (kind))->load() >= 0.5f; }

        /** The effects in the chain, in allEffects order. */
        std::vector<Kind> inChain() const
        {
            std::vector<Kind> kinds;
            for (const auto kind : allEffects)
                if (isInChain (kind))
                    kinds.push_back (kind);
            return kinds;
        }

        bool isEmpty() const { return inChain().empty(); }

        juce::Point<float> positionOf (Kind kind) const
        {
            const auto effect = findEffect (kind);
            if (! effect.isValid())
                return defaultPosition (kind);
            return { juce::jlimit (0.0f, areaWidth - cardWidth, (float) effect.getProperty (xProperty)),
                     juce::jlimit (0.0f, areaHeight - cardHeight, (float) effect.getProperty (yProperty)) };
        }

        juce::Rectangle<float> boundsOf (Kind kind) const { return boundsAt (positionOf (kind)); }

        /** Puts `kind` in the chain with its card at `at` (or the nearest
            free spot), switched on. False if it was already there. */
        bool add (Kind kind, juce::Point<float> at)
        {
            if (isInChain (kind))
                return false;
            setPosition (kind, resolveOverlap (at, othersThan (kind)));
            setParameter (powerParameter (kind), true);
            setParameter (inChainParameter (kind), true);
            return true;
        }

        /** Takes `kind` out of the chain (its settings, its switch and its
            place are kept for when it comes back). */
        void remove (Kind kind) { setParameter (inChainParameter (kind), false); }

        /** Moves `kind`'s card to `to` (or the nearest free spot); returns
            where it landed. Dropped with its middle on another card, it
            trades places with that card instead, so even a full row (three
            cards fill one) is reordered by dropping one card on another. */
        juce::Point<float> move (Kind kind, juce::Point<float> to)
        {
            if (const auto under = cardUnder (kind, boundsAt (to).getCentre()))
            {
                const auto from  = positionOf (kind);
                const auto there = positionOf (*under);
                setPosition (*under, from);
                setPosition (kind, there);
                return there;
            }
            const auto landed = resolveOverlap (to, othersThan (kind));
            setPosition (kind, landed);
            return landed;
        }

        /** Sets the effects in the chain out afresh, as a preset loads: side
            by side, the drive, then the delay, then the reverb. */
        void layOut()
        {
            ensureRack();
            int place = 0;
            for (const auto kind : inChain())
                setPosition (kind, defaultPosition (place++));
        }

        /** The effects in the chain in reading order: the order sound goes through them. */
        std::vector<Kind> readingOrder() const
        {
            const auto kinds = inChain();
            std::vector<juce::Point<float>> positions;
            for (const auto kind : kinds)
                positions.push_back (positionOf (kind));

            std::vector<Kind> ordered;
            for (const auto index : Bench::readingOrder (positions))
                ordered.push_back (kinds[index]);
            return ordered;
        }

        /** The order for the engine: the chain in reading order, then the
            effects out of it (silent anyway), in allEffects order. */
        Effects::Order chain() const
        {
            auto ordered = readingOrder();
            for (const auto kind : allEffects)
                if (std::find (ordered.begin(), ordered.end(), kind) == ordered.end())
                    ordered.push_back (kind);
            Effects::Order order {};
            std::copy_n (ordered.begin(), order.size(), order.begin());
            return order;
        }

    private:
        juce::ValueTree rack() const { return state.state.getChildWithName (rackType); }

        juce::ValueTree findEffect (Kind kind) const { return rack().getChildWithProperty (idProperty, juce::String (idOf (kind))); }

        void setPosition (Kind kind, juce::Point<float> topLeft)
        {
            ensureRack();
            auto effect = findEffect (kind);
            if (! effect.isValid())
            {
                effect = juce::ValueTree (effectType);
                effect.setProperty (idProperty, juce::String (idOf (kind)), nullptr);
                rack().appendChild (effect, nullptr);
            }
            effect.setProperty (xProperty, topLeft.x, nullptr);
            effect.setProperty (yProperty, topLeft.y, nullptr);
        }

        void setParameter (const char* id, bool on)   // NOSONAR(cpp:S5817): it changes a parameter
        {
            auto* parameter = state.getParameter (id);
            if ((parameter->getValue() >= 0.5f) == on)
                return;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (on ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }

        static float savedValue (const juce::ValueTree& saved, const char* id, float fallback)
        {
            const auto parameter = saved.getChildWithProperty (idProperty, juce::String (id));
            return parameter.isValid() ? (float) parameter.getProperty ("value", fallback) : fallback;
        }

        // The card in the chain, other than `kind`, under `point` (if any).
        std::optional<Kind> cardUnder (Kind kind, juce::Point<float> point) const
        {
            for (const auto other : inChain())
                if (other != kind && boundsOf (other).contains (point))
                    return other;
            return std::nullopt;
        }

        std::vector<juce::Rectangle<float>> othersThan (Kind kind) const
        {
            std::vector<juce::Rectangle<float>> others;
            for (const auto other : inChain())
                if (other != kind)
                    others.push_back (boundsOf (other));
            return others;
        }

        juce::AudioProcessorValueTreeState& state;
    };
}

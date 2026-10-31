#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <memory>
#include <utility>

#include "Bench.h"
#include "BenchModules.h"
#include "Synth2Theme.h"

/**
    BenchView — the workbench: a dark area, faintly dotted, where the modules
    sit wherever they were put. Empty, it invites a click; a click on any
    empty spot asks the editor to offer modules to add there.

    Every module there can be is made up front and simply shown while it is
    on the bench (see Bench::Model). Dragging a module by its header moves it;
    letting go lands it there, or on the nearest free spot if it would cover
    another. A module that arrives fades in; one that is taken off fades out.
*/
class BenchView final : public juce::Component
{
public:
    BenchView (juce::AudioProcessorValueTreeState& stateToUse, Synth2LookAndFeel& looks)
        : state (stateToUse), model (stateToUse)
    {
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
            oscillators[(size_t) slot] = std::make_unique<OscillatorModule> (slot, looks, state);
        for (int slot = 0; slot < Bench::numEnvelopes; ++slot)
            envelopes[(size_t) slot] = std::make_unique<EnvelopeModule> (slot, looks, state);
        for (int slot = 0; slot < Bench::numFilters; ++slot)
            filters[(size_t) slot] = std::make_unique<FilterModule> (slot, looks, state);

        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            addChildComponent (card);
            card.onRemove ([this, id] { remove (id); });
            card.onMoved ([this, id] (juce::Point<int> topLeft) { land (id, topLeft); });
        }
        setRepaintsOnMouseActivity (false);
    }

    /** A click on an empty spot: where, in design pixels on the bench, and in this view. */
    void onEmptyClick (std::function<void (juce::Point<float>, juce::Point<int>)> handler) { emptyClickHandler = std::move (handler); }

    BenchModule& getModule (const Bench::ModuleId& id)
    {
        const auto slot = (size_t) juce::jlimit (0, 3, id.slot);
        switch (id.kind)
        {
            case Bench::Kind::envelope:   return *envelopes[slot];
            case Bench::Kind::filter:     return *filters[slot];
            case Bench::Kind::oscillator: break;
        }
        return *oscillators[slot];
    }

    OscillatorModule& getOscillator (int slot) { return *oscillators[(size_t) slot]; }
    EnvelopeModule&   getEnvelope (int slot)   { return *envelopes[(size_t) slot]; }
    FilterModule&     getFilter (int slot)     { return *filters[(size_t) slot]; }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            juce::Desktop::getInstance().getAnimator().cancelAnimation (&card, true);
            card.setUiScale (uiScale);
            card.setBounds (boundsFor (id));
        }
        repaint();
    }

    /** Where module `id` sits in this view, as tall as its card is drawn. */
    juce::Rectangle<int> boundsFor (const Bench::ModuleId& id)
    {
        const auto bounds = model.boundsOf (id);
        return (bounds * uiScale).getSmallestIntegerContainer().withSize (juce::roundToInt (Bench::moduleWidth * uiScale),
                                                                           juce::roundToInt (getModule (id).cardHeight() * uiScale));
    }

    /** Shows every card as `view` says. */
    void setView (const Bench::View& view)
    {
        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            card.setView (view);
            if (! juce::Desktop::getInstance().getAnimator().isAnimating (&card))
                card.setBounds (boundsFor (id));
        }
    }

    /** Adds a module of `kind` at `at` (design pixels), fading it in. */
    void add (Bench::Kind kind, juce::Point<float> at)
    {
        if (const auto id = model.add (kind, at))
            show (*id, true);
    }

    /** Takes `id` off the bench, fading it out. */
    void remove (const Bench::ModuleId& id)
    {
        model.remove (id);
        hide (id);
    }

    /** Catches up with the bench: modules that came or went (from the host,
        undo or a restored session) and where they sit. A module being
        dragged, or gliding or fading, is left to finish first. */
    void sync()
    {
        const auto& animator = juce::Desktop::getInstance().getAnimator();
        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            const bool present = model.isPresent (id);
            if (present && ! card.isVisible())
                show (id, true);
            else if (! present && card.isVisible())
                hide (id);
            else if (present && ! card.isBeingDragged() && ! animator.isAnimating (&card))
                if (const auto bounds = boundsFor (id); card.getBounds() != bounds)
                    card.setBounds (bounds);
        }

        const bool empty = model.isEmpty();
        if (empty != wasEmpty)
        {
            wasEmpty = empty;
            repaint();
        }
    }

    bool isShowingHint() const { return model.isEmpty(); }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  area = getLocalBounds().toFloat();

        BenchStyle::paintArea (g, area, s);

        if (model.isEmpty())
        {
            const auto hint = area.withSizeKeepingCentre (420.0f * s, 60.0f * s);
            g.setFont (Fonts::sans (15.0f * s, true));
            g.setColour (Palette::textMid);
            g.drawText ("Click anywhere to add a module", hint.withHeight (24.0f * s), juce::Justification::centred, false);
            g.setFont (Fonts::sans (11.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("Start with an oscillator, then drag things where you like them",
                        hint.withTrimmedTop (30.0f * s), juce::Justification::centredTop, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (emptyClickHandler == nullptr)
            return;
        const auto design = e.position / uiScale;
        const auto topLeft = juce::Point<float> (design.x - Bench::moduleWidth * 0.5f, design.y - 15.0f);
        emptyClickHandler (topLeft, e.getPosition());
    }

    void resized() override { setUiScale (uiScale); }

private:
    void show (const Bench::ModuleId& id, bool fadeIn)
    {
        auto& card = getModule (id);
        card.setBounds (boundsFor (id));
        card.toFront (false);
        if (fadeIn)
            juce::Desktop::getInstance().getAnimator().fadeIn (&card, 150);   // --dur-base
        else
            card.setVisible (true);
        repaint();
    }

    void hide (const Bench::ModuleId& id)
    {
        if (auto& card = getModule (id); card.isVisible())
            juce::Desktop::getInstance().getAnimator().fadeOut (&card, 150);
        repaint();
    }

    void land (const Bench::ModuleId& id, juce::Point<int> topLeft)
    {
        const auto landed = model.move (id, topLeft.toFloat() / uiScale);
        auto& card = getModule (id);
        if (const auto target = boundsFor (id); landed != topLeft.toFloat() / uiScale && card.getBounds() != target)
            juce::Desktop::getInstance().getAnimator().animateComponent (&card, target, 1.0f, 150, false, 0.4, 0.0);
        else
            card.setBounds (target);
    }

    juce::AudioProcessorValueTreeState& state;
    Bench::Model model;

    std::array<std::unique_ptr<OscillatorModule>, Bench::numOscillators> oscillators;
    std::array<std::unique_ptr<EnvelopeModule>, Bench::numEnvelopes> envelopes;
    std::array<std::unique_ptr<FilterModule>, Bench::numFilters> filters;

    std::function<void (juce::Point<float>, juce::Point<int>)> emptyClickHandler;
    float uiScale  = 1.0f;
    bool  wasEmpty = true;
};

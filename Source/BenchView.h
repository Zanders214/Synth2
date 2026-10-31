#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

#include "Bench.h"
#include "BenchModules.h"
#include "Synth2Theme.h"

/**
    ZoomPill — the small pill on the bench's bottom edge, at its right: minus,
    how far the bench is zoomed, plus, and Fit. It belongs to the bench but
    sits in the editor, half below the bench's box, so it never covers the
    dials of a card in the bench's bottom corner.
*/
class ZoomPill final : public juce::Component
{
public:
    enum class Part { zoomOut, zoomIn, fit };

    static constexpr float pillWidth  = 148.0f;   // design pixels
    static constexpr float pillHeight = 22.0f;

    ZoomPill() { setRepaintsOnMouseActivity (true); }

    /** A part was clicked. */
    void onPress (std::function<void (Part)> handler) { pressHandler = std::move (handler); }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    void setZoom (float newZoom)
    {
        if (! juce::exactlyEqual (zoom, newZoom))
        {
            zoom = newZoom;
            repaint();
        }
    }

    juce::Rectangle<float> partBounds (Part part) const noexcept
    {
        const auto  pill  = getLocalBounds().toFloat();
        const float s     = uiScale;
        const float glyph = 28.0f * s;
        switch (part)
        {
            case Part::zoomOut: return pill.withWidth (glyph);
            case Part::zoomIn:  return pill.withX (pill.getX() + 28.0f * s + 52.0f * s).withWidth (glyph);
            case Part::fit:     break;
        }
        return pill.withTrimmedLeft (2.0f * glyph + 52.0f * s);
    }

    juce::String zoomText() const { return juce::String (juce::roundToInt (zoom * 100.0f)) + "%"; }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s    = uiScale;
        const auto  pill = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Palette::panelBottom.withAlpha (0.92f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Palette::idleFill);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Palette::hairline);
        g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, juce::jmax (1.0f, s));

        const auto mouse = getMouseXYRelative().toFloat();
        const bool over  = isMouseOver();
        const auto tint  = [&] (Part part, bool enabled)
        {
            if (! enabled)
                return Palette::textFaint;
            return over && partBounds (part).contains (mouse) ? Palette::textStrong : Palette::textMid;
        };
        const auto stroke = juce::PathStrokeType (juce::jmax (1.0f, 1.6f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const float arm   = 4.0f * s;

        juce::Path minus;
        const auto outAt = partBounds (Part::zoomOut).getCentre().translated (2.0f * s, 0.0f);
        minus.startNewSubPath (outAt.translated (-arm, 0.0f));
        minus.lineTo (outAt.translated (arm, 0.0f));
        g.setColour (tint (Part::zoomOut, zoom > Bench::fitZoom + 0.001f));
        g.strokePath (minus, stroke);

        juce::Path plus;
        const auto inAt = partBounds (Part::zoomIn).getCentre().translated (-2.0f * s, 0.0f);
        plus.startNewSubPath (inAt.translated (-arm, 0.0f));
        plus.lineTo (inAt.translated (arm, 0.0f));
        plus.startNewSubPath (inAt.translated (0.0f, -arm));
        plus.lineTo (inAt.translated (0.0f, arm));
        g.setColour (tint (Part::zoomIn, zoom < Bench::maxZoom - 0.001f));
        g.strokePath (plus, stroke);

        g.setFont (Fonts::mono (10.0f * s));
        g.setColour (Palette::textMid);
        g.drawText (zoomText(), pill.withX (partBounds (Part::zoomOut).getRight()).withWidth (52.0f * s), juce::Justification::centred, false);

        const auto fit = partBounds (Part::fit);
        g.setColour (Palette::hairline);
        g.fillRect (juce::Rectangle<float> (fit.getX(), fit.getY() + 6.0f * s, juce::jmax (1.0f, s), fit.getHeight() - 12.0f * s));
        g.setFont (Fonts::sans (9.0f * s, true, 0.14f));
        g.setColour (tint (Part::fit, true));
        g.drawText ("FIT", fit.withTrimmedRight (4.0f * s), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (const auto part : { Part::zoomOut, Part::zoomIn, Part::fit })
            if (partBounds (part).contains (e.position))
            {
                if (pressHandler != nullptr)
                    pressHandler (part);
                return;
            }
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }

private:
    std::function<void (Part)> pressHandler;
    float uiScale = 1.0f;
    float zoom    = 1.0f;
};

/**
    BenchView — the workbench: a dark box, faintly dotted, looking onto a
    canvas (Bench::areaWidth x areaHeight) where the modules sit wherever they
    were put. Empty, it invites a click; a click on any empty spot asks the
    editor to offer modules to add there.

    Every module there can be is made up front and simply shown while it is
    on the bench (see Bench::Model). Dragging a module by its header moves it;
    letting go lands it there, or on the nearest free spot if it would cover
    another. A module that arrives fades in; one that is taken off fades out.

    The box zooms (Cmd / Ctrl + wheel, a pinch, or its ZoomPill, which the
    editor puts on the box's bottom edge) and pans (the wheel, two fingers, or dragging an empty spot) over
    the canvas; how far is kept on the bench (see Bench::Viewport). Each card
    is drawn at the zoom, so it stays crisp.
*/
class BenchView final : public juce::Component
{
public:
    BenchView (juce::AudioProcessorValueTreeState& stateToUse, Synth2LookAndFeel& looks)
        : state (stateToUse), model (stateToUse), viewport (model.viewport())
    {
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
            oscillators[(size_t) slot] = std::make_unique<OscillatorModule> (slot, looks, state);
        for (int slot = 0; slot < Bench::numEnvelopes; ++slot)
            envelopes[(size_t) slot] = std::make_unique<EnvelopeModule> (slot, looks, state);

        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            addChildComponent (card);
            card.onRemove ([this, id] { remove (id); });
            card.onMoved ([this, id] (juce::Point<int> topLeft) { land (id, topLeft); });
        }
        pill.setZoom (viewport.zoom);
        pill.onPress ([this] (ZoomPill::Part part)
        {
            if (part == ZoomPill::Part::fit)
                fit();
            else
                stepZoom (part == ZoomPill::Part::zoomIn ? 1 : -1);
        });
        setRepaintsOnMouseActivity (false);
    }

    /** A click on an empty spot: where, in design pixels on the canvas, and in this view. */
    void onEmptyClick (std::function<void (juce::Point<float>, juce::Point<int>)> handler) { emptyClickHandler = std::move (handler); }
    /** The box zoomed or panned (and so the cards moved). */
    void onViewportChange (std::function<void()> handler) { viewportHandler = std::move (handler); }

    BenchModule& getModule (const Bench::ModuleId& id)
    {
        const auto slot = (size_t) juce::jlimit (0, 3, id.slot);
        if (id.kind == Bench::Kind::envelope)
            return *envelopes[slot];
        return *oscillators[slot];
    }

    OscillatorModule& getOscillator (int slot) { return *oscillators[(size_t) slot]; }
    EnvelopeModule&   getEnvelope (int slot)   { return *envelopes[(size_t) slot]; }
    ZoomPill&         getZoomPill() noexcept   { return pill; }

    void setUiScale (float newScale)
    {
        uiScale = newScale;
        pill.setUiScale (uiScale);
        placeCards (true);
        repaint();
    }

    // ---- Zoom and pan ---------------------------------------------------------------

    const Bench::Viewport& getViewport() const noexcept { return viewport; }

    /** Looks onto the canvas as `newViewport` says (kept to what the bench
        can show), and keeps that on the bench. */
    void setViewport (const Bench::Viewport& newViewport)
    {
        lookAt (Bench::limitViewport (newViewport));
        model.setViewport (viewport);
    }

    /** Zooms to `newZoom`, keeping the canvas point under `viewAt` (in this view) there. */
    void zoomAround (juce::Point<float> viewAt, float newZoom)
    {
        const auto anchor = toDesign (viewAt);
        newZoom = juce::jlimit (Bench::fitZoom, Bench::maxZoom, newZoom);
        setViewport ({ newZoom, anchor - viewAt / (uiScale * newZoom) });
    }

    /** One step in (`direction` 1) or out (-1) through the pill's levels, round the middle of the box. */
    void stepZoom (int direction)
    {
        static constexpr std::array<float, 5> levels { Bench::fitZoom, 0.8f, 1.0f, 1.25f, Bench::maxZoom };
        float next = direction > 0 ? Bench::maxZoom : Bench::fitZoom;
        if (direction > 0)
        {
            for (auto level = levels.rbegin(); level != levels.rend(); ++level)
                if (*level > viewport.zoom + 0.001f)
                    next = *level;
        }
        else
        {
            for (const float level : levels)
                if (level < viewport.zoom - 0.001f)
                    next = level;
        }
        zoomAround (getLocalBounds().toFloat().getCentre(), next);
    }

    /** Frames the modules on the bench, or shows the whole canvas when there are none. */
    void fit() { setViewport (Bench::framing (model.extent())); }

    /** A point on the canvas (design pixels) in this view, and back. */
    juce::Point<float> toView (juce::Point<float> design) const noexcept { return (design - viewport.pan) * scale(); }
    juce::Point<float> toDesign (juce::Point<float> viewAt) const noexcept { return viewAt / scale() + viewport.pan; }
    juce::Rectangle<float> toView (juce::Rectangle<float> design) const noexcept
    {
        return { toView (design.getPosition()), toView (design.getBottomRight()) };
    }

    /** Where module `id` sits in this view, as tall as its card is drawn. */
    juce::Rectangle<int> boundsFor (const Bench::ModuleId& id)
    {
        // A hair's grace, so a card dropped at a pixel lands on that pixel
        // whatever the zoom made of it.
        const auto snap    = [] (float v) { return (int) std::floor (v + 0.01f); };
        const auto topLeft = toView (model.positionOf (id));
        return { snap (topLeft.x), snap (topLeft.y), juce::roundToInt (Bench::moduleWidth * scale()),
                 juce::roundToInt (getModule (id).cardHeight() * scale()) };
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

    /** Adds a module of `kind` at `at` (design pixels), fading it in. It
        lands where the box shows it, if it fits there. */
    void add (Bench::Kind kind, juce::Point<float> at)
    {
        const auto seen = viewport.visibleArea().getIntersection ({ Bench::areaWidth, Bench::areaHeight });
        at = { juce::jlimit (seen.getX(), juce::jmax (seen.getX(), seen.getRight() - Bench::moduleWidth), at.x),
               juce::jlimit (seen.getY(), juce::jmax (seen.getY(), seen.getBottom() - Bench::heightOf (kind)), at.y) };
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
        undo or a restored session), where they sit, and how far the box is
        zoomed and panned. A module being dragged, or gliding or fading, is
        left to finish first. */
    void sync()
    {
        if (const auto saved = model.viewport(); saved != viewport)
            lookAt (saved);

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
        const float s      = uiScale;
        const float z      = scale();
        const auto  box    = getLocalBounds().toFloat();
        const auto  canvas = toView (juce::Rectangle<float> (Bench::areaWidth, Bench::areaHeight));

        g.setColour (Palette::well.withAlpha (0.55f));
        g.fillRoundedRectangle (box, 16.0f * s);

        // The dots lie on the canvas, so they move and grow with it.
        {
            const juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (canvas.getIntersection (box.reduced (10.0f * s)).toNearestInt());
            g.setColour (Palette::gridLine.withMultipliedAlpha (1.6f));
            static constexpr float spacing = 24.0f;   // static, so the lambdas below can use it without a capture (MSVC)
            const float dot  = juce::jmax (1.5f, 2.0f * z);
            const auto  seen = viewport.visibleArea();
            const auto  first = [] (float from) { return juce::jmax (1, (int) std::ceil (from / spacing)); };
            const auto  last  = [] (float to, float extent) { return (int) std::floor (juce::jmin (to, extent - spacing) / spacing); };
            for (int row = first (seen.getY()); row <= last (seen.getBottom(), Bench::areaHeight); ++row)
                for (int column = first (seen.getX()); column <= last (seen.getRight(), Bench::areaWidth); ++column)
                {
                    const auto at = toView (juce::Point<float> ((float) column * spacing, (float) row * spacing));
                    g.fillEllipse (at.x - dot * 0.5f, at.y - dot * 0.5f, dot, dot);
                }
        }

        // A faint line where the canvas ends inside the box.
        g.setColour (Palette::hairline.withMultipliedAlpha (0.7f));
        const float line = juce::jmax (1.0f, s);
        const auto  edge = [&g, &box] (juce::Rectangle<float> strip)
        {
            if (box.reduced (2.0f).intersects (strip))
                g.fillRect (strip.getIntersection (box));
        };
        edge ({ canvas.getX() - line, canvas.getY(), line, canvas.getHeight() });
        edge ({ canvas.getRight(), canvas.getY(), line, canvas.getHeight() });
        edge ({ canvas.getX(), canvas.getY() - line, canvas.getWidth(), line });
        edge ({ canvas.getX(), canvas.getBottom(), canvas.getWidth(), line });

        g.setColour (Palette::hairline);
        g.drawRoundedRectangle (box.reduced (0.5f), 16.0f * s, juce::jmax (1.0f, s));

        if (model.isEmpty())
        {
            const auto hint = box.withSizeKeepingCentre (420.0f * z, 60.0f * z);
            g.setFont (Fonts::sans (15.0f * z, true));
            g.setColour (Palette::textMid);
            g.drawText ("Click anywhere to add a module", hint.withHeight (24.0f * z), juce::Justification::centred, false);
            g.setFont (Fonts::sans (11.0f * z));
            g.setColour (Palette::textDim);
            g.drawText ("Start with an oscillator, then drag things where you like them",
                        hint.withTrimmedTop (30.0f * z), juce::Justification::centredTop, false);
        }
    }

    // A press on an empty spot is a click (to add a module there) unless it
    // drags more than a few pixels, which pans instead.
    void mouseDown (const juce::MouseEvent& e) override
    {
        press = Press { e.position, viewport.pan, false, true };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! press.down)
            return;
        if (! press.panning && e.position.getDistanceFrom (press.at) > 4.0f * uiScale)
        {
            press.panning = true;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
        if (press.panning)
            setViewport ({ viewport.zoom, press.pan - (e.position - press.at) / scale() });
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const auto was = std::exchange (press, Press {});
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (! was.down || was.panning || emptyClickHandler == nullptr)
            return;
        const auto design  = toDesign (was.at);
        const auto topLeft = juce::Point<float> (design.x - Bench::moduleWidth * 0.5f, design.y - 15.0f);
        emptyClickHandler (topLeft, was.at.roundToInt());
    }

    // Cmd / Ctrl + wheel zooms round the pointer; the wheel (or two fingers)
    // pans. A dial takes the wheel over itself; the rest of a card passes it here.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCommandDown())
        {
            zoomAround (e.position, viewport.zoom * std::exp2 (3.0f * wheel.deltaY));
            return;
        }
        constexpr float pixelsPerDelta = 512.0f;   // a trackpad's pixel, as JUCE gives it
        setViewport ({ viewport.zoom, viewport.pan - juce::Point<float> (wheel.deltaX, wheel.deltaY) * (pixelsPerDelta / scale()) });
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        zoomAround (e.position, viewport.zoom * scaleFactor);
    }

    void resized() override { setUiScale (uiScale); }

private:
    struct Press
    {
        juce::Point<float> at;    // where it went down, in this view
        juce::Point<float> pan;   // the pan then
        bool panning = false;
        bool down    = false;
    };

    /** Design pixels to this view's: the window's scale times the zoom. */
    float scale() const noexcept { return uiScale * viewport.zoom; }

    /** Looks onto the canvas as `newViewport` (already limited) says. */
    void lookAt (const Bench::Viewport& newViewport)
    {
        const bool rescale = ! juce::exactlyEqual (newViewport.zoom, viewport.zoom);
        viewport = newViewport;
        pill.setZoom (viewport.zoom);
        placeCards (rescale);
        repaint();
        if (viewportHandler != nullptr)
            viewportHandler();
    }

    /** Puts every card where the zoom and pan show it (drawn at the zoom when
        `rescale`), finishing any glide or fade, and keeps their drags to the
        canvas in the box. */
    void placeCards (bool rescale)
    {
        const auto canvas = toView (juce::Rectangle<float> (Bench::areaWidth, Bench::areaHeight)).getSmallestIntegerContainer();
        for (const auto& id : Bench::allModules)
        {
            auto& card = getModule (id);
            card.setDragArea (canvas.getIntersection (getLocalBounds()));
            if (rescale)
                card.setUiScale (scale());
            if (card.isBeingDragged())
                continue;
            juce::Desktop::getInstance().getAnimator().cancelAnimation (&card, true);
            card.setBounds (boundsFor (id));
        }
    }

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
        const auto wanted = toDesign (topLeft.toFloat());
        const auto landed = model.move (id, wanted);
        auto& card = getModule (id);
        if (const auto target = boundsFor (id); landed != wanted && card.getBounds() != target)
            juce::Desktop::getInstance().getAnimator().animateComponent (&card, target, 1.0f, 150, false, 0.4, 0.0);
        else
            card.setBounds (target);
    }

    juce::AudioProcessorValueTreeState& state;
    Bench::Model model;

    std::array<std::unique_ptr<OscillatorModule>, Bench::numOscillators> oscillators;
    std::array<std::unique_ptr<EnvelopeModule>, Bench::numEnvelopes> envelopes;
    ZoomPill pill;

    std::function<void (juce::Point<float>, juce::Point<int>)> emptyClickHandler;
    std::function<void()> viewportHandler;
    Bench::Viewport viewport;   // as on the bench (see Bench::Model::viewport)
    Press  press;
    float  uiScale  = 1.0f;
    bool   wasEmpty = true;
};

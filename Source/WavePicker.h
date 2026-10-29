#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>

#include "Synth2Theme.h"
#include "Synth2VisualModel.h"

namespace WaveGlyph
{
    inline constexpr int numWaves = 4;
    inline constexpr std::array<const char*, numWaves> shortNames { "SINE", "TRI", "SAW", "SQUARE" };
    inline constexpr std::array<const char*, numWaves> longNames  { "SINE", "TRIANGLE", "SAW", "SQUARE" };

    /** `cycles` of `wave` drawn across `area`, peak to peak its full height. */
    inline juce::Path path (Waveform wave, juce::Rectangle<float> area, float cycles)
    {
        juce::Path shape;
        const int points = juce::jmax (32, (int) area.getWidth());
        for (int p = 0; p <= points; ++p)
        {
            const float x01 = (float) p / (float) points;
            const float v   = Oscillator::naive (wave, (double) (x01 * cycles));
            const juce::Point pt (area.getX() + x01 * area.getWidth(), area.getCentreY() - v * area.getHeight() * 0.5f);
            if (p == 0) shape.startNewSubPath (pt);
            else        shape.lineTo (pt);
        }
        return shape;
    }

    /** The spectrum, cyan to amber, laid across `area`. */
    inline juce::ColourGradient spectrum (juce::Rectangle<float> area)
    {
        juce::ColourGradient ramp (Synth2::Palette::cyan, area.getX(), 0.0f, Synth2::Palette::amber, area.getRight(), 0.0f, false);
        ramp.addColour (1.0 / 3.0, Synth2::Palette::violet);
        ramp.addColour (2.0 / 3.0, Synth2::Palette::pink);
        return ramp;
    }
}

/**
    WavePicker — picks the WAVE, laid over the WAVE well.

    The well's top right holds a pill with the chosen shape: its arrows step
    to the previous and next shape (wrapping round), and the scroll wheel
    anywhere over the well flips through them. A click anywhere else on the
    well opens the wave menu (onOpenMenu). Hovering lights the well's border
    in the accent, the design system's mark of a well you can act on.

    It is tied to the WAVE choice parameter directly (ParameterAttachment), so
    host automation and undo see each pick as one gesture.
*/
class WavePicker final : public juce::Component
{
public:
    static constexpr int numWaves = WaveGlyph::numWaves;

    /** A click on the well (not on an arrow): open, or close, the menu. */
    std::function<void()> onOpenMenu;

    explicit WavePicker (juce::RangedAudioParameter& waveParameter)
        : attachment (waveParameter, [this] (float value) { setSelected (juce::roundToInt (value)); })
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }
    void setMenuOpen (bool open)     { menuOpen = open; repaint(); }

    int getSelected() const noexcept { return selected; }

    /** Picks `index` (0..3), as one gesture. */
    void choose (int index) { attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, numWaves - 1, index)); }

    /** Picks the shape `delta` steps on, wrapping round. */
    void step (int delta) { choose (Synth2VisualModel::stepWave (selected, delta)); }

    juce::Rectangle<float> pillBounds() const noexcept
    {
        const float s = uiScale;
        return { (float) getWidth() - (8.0f + 116.0f) * s, 6.0f * s, 116.0f * s, 20.0f * s };
    }

    juce::Rectangle<float> previousArrow() const noexcept { return pillBounds().withWidth (24.0f * uiScale); }
    juce::Rectangle<float> nextArrow() const noexcept
    {
        const auto pill = pillBounds();
        return pill.withTrimmedLeft (pill.getWidth() - 24.0f * uiScale);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (previousArrow().contains (e.position))
            step (-1);
        else if (nextArrow().contains (e.position))
            step (1);
        else if (onOpenMenu != nullptr)
            onOpenMenu();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        // A trackpad sends many small deltas, a wheel a few big ones: gather
        // them, and step once each time enough has built up. Down (or right)
        // is the next shape.
        wheelTravel += std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (wheel.isReversed)
            wheelTravel = -wheelTravel;

        constexpr float perStep = 0.12f;
        if (std::abs (wheelTravel) >= perStep)
        {
            step (wheelTravel < 0.0f ? 1 : -1);
            wheelTravel = 0.0f;
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s      = uiScale;
        const auto  mouse  = getMouseXYRelative().toFloat();
        const bool  over   = isMouseOver (true);

        // The well's border lights up in the accent: faint on hover, firm while
        // its menu is open.
        if (over || menuOpen)
        {
            g.setColour (Palette::accent.withAlpha (menuOpen ? 0.60f : 0.30f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f * s, juce::jmax (1.0f, s));
        }

        // The pill: arrows either side of the chosen shape and its name.
        const auto pill = pillBounds();
        g.setColour (over ? Palette::idleFill.withMultipliedAlpha (1.6f) : Palette::idleFill);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Palette::strongLine);
        g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, juce::jmax (1.0f, s));

        const auto arrow = [&] (juce::Rectangle<float> area, bool pointsRight)
        {
            const bool hot = over && area.contains (mouse);
            const auto c   = area.getCentre();
            const float w  = 2.5f * s;
            const float h  = 4.0f * s;
            juce::Path chevron;
            chevron.startNewSubPath (c.x + (pointsRight ? -w : w) * 0.5f, c.y - h);
            chevron.lineTo (c.x + (pointsRight ? w : -w) * 0.5f, c.y);
            chevron.lineTo (c.x + (pointsRight ? -w : w) * 0.5f, c.y + h);
            g.setColour (hot ? Palette::textStrong : Palette::textDim);
            g.strokePath (chevron, juce::PathStrokeType (1.5f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        };
        arrow (previousArrow(), false);
        arrow (nextArrow(), true);

        const auto middle = pill.reduced (24.0f * s, 0.0f);
        const auto glyph  = middle.withWidth (16.0f * s).withSizeKeepingCentre (16.0f * s, 8.0f * s);
        g.setGradientFill (WaveGlyph::spectrum (glyph));
        g.strokePath (WaveGlyph::path ((Waveform) selected, glyph, 1.0f),
                      juce::PathStrokeType (1.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (Fonts::sans (10.0f * s, true, 0.14f));
        g.setColour (Palette::textStrong);
        g.drawText (WaveGlyph::shortNames[(size_t) selected], middle.withTrimmedLeft (glyph.getWidth() + 6.0f * s),
                    juce::Justification::centredLeft, false);
    }

private:
    void setSelected (int index)
    {
        selected = juce::jlimit (0, numWaves - 1, index);
        repaint();
    }

    juce::ParameterAttachment attachment;
    int   selected    = 0;
    float uiScale     = 1.0f;
    float wheelTravel = 0.0f;
    bool  menuOpen    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavePicker)
};

/**
    WaveMenu — the tray the WAVE well drops open: the four shapes as big tiles,
    each drawn glowing in the spectrum, the chosen one ringed in the accent.

    It covers the whole editor while open, dimming everything but the WAVE well
    (which previews the hovered shape, via onHover). Click a tile to pick it;
    click anywhere else, or press Escape, to close. The arrow keys move along
    the tiles and Return picks.
*/
class WaveMenu final : public juce::Component
{
public:
    static constexpr int numWaves = WaveGlyph::numWaves;

    std::function<void (int)> onChoose;   // a tile was picked
    std::function<void (int)> onHover;    // the tile under the mouse (or keys), -1 for none
    std::function<void()>     onClose;

    WaveMenu()
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    /** Where the WAVE well sits: the tray drops from under it. */
    void setAnchor (juce::Rectangle<int> wellBounds) { anchor = wellBounds; repaint(); }

    bool isOpen() const noexcept { return isVisible(); }
    int  getHighlighted() const noexcept { return highlighted; }

    void open (int selectedWave)
    {
        selected    = selectedWave;
        highlighted = -1;
        juce::Desktop::getInstance().getAnimator().fadeIn (this, 80);   // --dur-fast
        toFront (true);
    }

    void close()
    {
        if (! isVisible())
            return;
        setVisible (false);
        setHighlighted (-1);
        if (onClose != nullptr)
            onClose();
    }

    juce::Rectangle<float> trayBounds() const noexcept
    {
        const auto well = anchor.toFloat();
        return { well.getX(), well.getBottom() + 8.0f * uiScale, well.getWidth(), 92.0f * uiScale };
    }

    juce::Rectangle<float> tileBounds (int index) const noexcept
    {
        const float s     = uiScale;
        const auto  inner = trayBounds().reduced (8.0f * s);
        const float gap   = 6.0f * s;
        const float width = (inner.getWidth() - gap * (float) (numWaves - 1)) / (float) numWaves;
        return { inner.getX() + (float) index * (width + gap), inner.getY(), width, inner.getHeight() };
    }

    int tileAt (juce::Point<float> position) const noexcept
    {
        for (int i = 0; i < numWaves; ++i)
            if (tileBounds (i).contains (position))
                return i;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override { setHighlighted (tileAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override   { setHighlighted (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int tile = tileAt (e.position); tile >= 0)
            pick (tile);
        else
            close();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
            close();
        else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            setHighlighted (Synth2VisualModel::stepWave (highlighted >= 0 ? highlighted : selected,
                                                         key == juce::KeyPress::rightKey ? 1 : -1));
        else if (key == juce::KeyPress::returnKey)
            pick (highlighted >= 0 ? highlighted : selected);
        else
            return false;
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Synth2;
        const float s = uiScale;

        // Dim everything but the WAVE well (its rounded shape cut out), which
        // shows the previews.
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        dim.addRoundedRectangle (anchor.toFloat(), 12.0f * s);
        dim.setUsingNonZeroWinding (false);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillPath (dim);

        // The tray: an inset well of its own, floating on a deep drop.
        const auto tray = trayBounds();
        juce::Path trayShape;
        trayShape.addRoundedRectangle (tray, 12.0f * s);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (24.0f * s), { 0, (int) (10.0f * s) }).drawForPath (g, trayShape);
        g.setColour (Palette::panelTop);
        g.fillPath (trayShape);
        g.setColour (Palette::strongLine);
        g.strokePath (trayShape, juce::PathStrokeType (juce::jmax (1.0f, s)));

        for (int i = 0; i < numWaves; ++i)
        {
            const auto tile   = tileBounds (i);
            const bool chosen = i == selected;
            const bool lit    = i == highlighted;
            const float radius = 8.0f * s;

            juce::Path tileShape;
            tileShape.addRoundedRectangle (tile, radius);
            if (chosen)
            {
                juce::DropShadow (Palette::accent.withAlpha (0.45f), (int) (16.0f * s), {}).drawForPath (g, tileShape);
                g.setColour (Palette::well);
                g.fillPath (tileShape);
                g.setColour (Palette::accentSoft);
                g.fillPath (tileShape);
                g.setColour (Palette::accent.withAlpha (0.8f));
                g.strokePath (tileShape, juce::PathStrokeType (juce::jmax (1.0f, 1.5f * s)));
            }
            else
            {
                g.setColour (Palette::well);
                g.fillPath (tileShape);
                g.setColour (lit ? Palette::idleFill.withMultipliedAlpha (1.8f) : Palette::idleFill.withMultipliedAlpha (0.5f));
                g.fillPath (tileShape);
                g.setColour (lit ? Palette::strongLine.withMultipliedAlpha (2.0f) : Palette::strongLine);
                g.strokePath (tileShape, juce::PathStrokeType (juce::jmax (1.0f, s)));
            }

            // The shape, glowing when it is lit or chosen, then its name.
            auto content = tile.reduced (12.0f * s, 10.0f * s);
            const auto label = content.removeFromBottom (12.0f * s);
            const auto glyph = content.withTrimmedBottom (6.0f * s).reduced (0.0f, 4.0f * s);
            const auto shape = WaveGlyph::path ((Waveform) i, glyph, 1.5f);
            const bool bright = chosen || lit;

            g.setGradientFill (WaveGlyph::spectrum (glyph));
            if (bright)
            {
                g.setOpacity (0.25f);
                g.strokePath (shape, juce::PathStrokeType (7.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setGradientFill (WaveGlyph::spectrum (glyph));
            }
            g.setOpacity (bright ? 1.0f : 0.55f);
            g.strokePath (shape, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setOpacity (1.0f);

            g.setFont (Fonts::sans (10.0f * s, true, 0.14f));
            g.setColour (bright ? Palette::textStrong : Palette::textDim);
            g.drawText (WaveGlyph::longNames[(size_t) i], label, juce::Justification::centred, false);
        }
    }

private:
    void setHighlighted (int index)
    {
        if (index == highlighted)
            return;
        highlighted = index;
        if (onHover != nullptr)
            onHover (index);
        repaint();
    }

    void pick (int index)
    {
        if (onChoose != nullptr)
            onChoose (index);
        close();
    }

    juce::Rectangle<int> anchor;
    int   selected    = 0;
    int   highlighted = -1;
    float uiScale     = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveMenu)
};

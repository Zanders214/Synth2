#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <optional>

#include "PresetCatalog.h"
#include "Synth2Theme.h"
#include "WavePicker.h"

/**
    PresetBrowserLook — where the preset browser's parts sit and how they are
    painted: the categories down the left, the list in the middle, the loaded
    preset on the right, and the buttons. See PresetBrowser.h.
*/
namespace PresetBrowserLook
{
    inline constexpr float categoryHeight = 34.0f;
    inline constexpr float rowHeight      = 54.0f;

    /** Where everything sits, for a browser `width` by `height` pixels at
        `s` pixels to the design pixel. */
    struct Layout
    {
        float s      = 1.0f;
        float width  = 0.0f;
        float height = 0.0f;

        juce::Rectangle<float> categoryArea() const noexcept { return { 16.0f * s, 68.0f * s, 176.0f * s, height - 84.0f * s }; }
        juce::Rectangle<float> listArea() const noexcept     { return { 212.0f * s, 68.0f * s, width - 212.0f * s - 312.0f * s, height - 84.0f * s }; }
        juce::Rectangle<float> detailArea() const noexcept   { return { width - 284.0f * s, 68.0f * s, 260.0f * s, height - 92.0f * s }; }
        juce::Rectangle<float> closeBounds() const noexcept  { return { width - 48.0f * s, 12.0f * s, 32.0f * s, 32.0f * s }; }
        juce::Rectangle<float> saveBounds() const noexcept   { return { width - (48.0f + 10.0f + 84.0f) * s, 14.0f * s, 84.0f * s, 28.0f * s }; }

        juce::Rectangle<int> searchBounds() const noexcept
        {
            return listArea().getSmallestIntegerContainer().withY (juce::roundToInt (12.0f * s))
                             .withHeight (juce::roundToInt (32.0f * s)).withWidth (juce::roundToInt (300.0f * s));
        }

        /** DELETE, under the details. */
        juce::Rectangle<float> deleteBounds() const noexcept
        {
            const auto detail = detailArea();
            return { detail.getX(), detail.getBottom() - (32.0f + 12.0f + 30.0f) * s, 176.0f * s, 30.0f * s };
        }

        /** Where the `row`th preset listed sits, the list `scroll` design pixels down. */
        juce::Rectangle<float> rowBounds (int row, float scroll) const noexcept
        {
            const auto list = listArea();
            return { list.getX(), list.getY() + ((float) row * rowHeight - scroll) * s, list.getWidth() - 10.0f * s, rowHeight * s };
        }

        /** Where `category`'s (all, user, or a Presets::Category) row sits. */
        juce::Rectangle<float> categoryBounds (int category) const noexcept
        {
            int place = category + 2;
            if (category == PresetCatalog::all)
                place = 0;
            else if (category == PresetCatalog::user)
                place = 1;
            return categoryArea().withTrimmedTop ((float) place * categoryHeight * s).withHeight (categoryHeight * s);
        }

        std::optional<int> categoryAt (juce::Point<float> position) const noexcept
        {
            for (int category = PresetCatalog::user; category < Presets::numCategories; ++category)
                if (categoryBounds (category).contains (position))
                    return category;
            return std::nullopt;
        }

        /** The row of a list of `count` under `position`, or -1. */
        int rowAt (juce::Point<float> position, int count, float scroll) const noexcept
        {
            if (! listArea().contains (position))
                return -1;
            for (int row = 0; row < count; ++row)
                if (rowBounds (row, scroll).contains (position))
                    return row;
            return -1;
        }

        /** How far a list of `count` scrolls, in design pixels. */
        float maxScroll (int count) const noexcept
        {
            return juce::jmax (0.0f, (float) count * rowHeight - listArea().getHeight() / s);
        }
    };

    inline juce::Font chipFont (float s) { return Synth2::Fonts::mono (9.0f * s, 0.06f); }

    inline float chipWidth (const juce::String& text, float s) { return Synth2::Fonts::width (chipFont (s), text) + 14.0f * s; }

    /** A small chip: `text` in `colour` on a faint fill of it, its left at
        `x`; returns its width. */
    inline float paintChip (juce::Graphics& g, const juce::String& text, float x, float centreY, juce::Colour colour, float s)
    {
        const float w = chipWidth (text, s);
        const juce::Rectangle chip (x, centreY - 9.0f * s, w, 18.0f * s);
        g.setColour (colour.withAlpha (0.14f));
        g.fillRoundedRectangle (chip, 9.0f * s);
        g.setFont (chipFont (s));
        g.setColour (colour);
        g.drawText (text, chip, juce::Justification::centred, false);
        return w;
    }

    /** An oscillator's shape, small, in its own colour of the spectrum. */
    inline void paintGlyph (juce::Graphics& g, Waveform wave, juce::Rectangle<float> box, int slot, bool withWell, float s)
    {
        using namespace Synth2;
        if (withWell)
        {
            g.setColour (Palette::well);
            g.fillRoundedRectangle (box, 6.0f * s);
            box = box.reduced (6.0f * s, 5.0f * s);
        }
        g.setColour (ColourMath::spectrum ((float) slot / 3.0f));
        g.strokePath (WaveGlyph::path (wave, box, 1.5f),
                      juce::PathStrokeType (juce::jmax (1.0f, 1.5f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** A section's label, under a little space, moving `area`'s top past it. */
    inline void paintLabel (juce::Graphics& g, juce::Rectangle<float>& area, const juce::String& label, float s)
    {
        using namespace Synth2;
        area.removeFromTop (14.0f * s);
        g.setFont (Fonts::sans (9.0f * s, true, 0.22f));
        g.setColour (Palette::textFaint);
        g.drawText (label, area.removeFromTop (18.0f * s), juce::Justification::centredLeft, false);
    }

    /** SAVE: an accent pill, lit under the pointer. */
    inline void paintSave (juce::Graphics& g, juce::Rectangle<float> box, bool hovered, float s)
    {
        using namespace Synth2;
        g.setColour (Palette::accentSoft.withMultipliedAlpha (hovered ? 2.4f : 1.4f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (Palette::accent.withAlpha (hovered ? 0.9f : 0.5f));
        g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, juce::jmax (1.0f, s));
        g.setFont (Fonts::sans (10.0f * s, true, 0.18f));
        g.setColour (Palette::accent);
        g.drawText ("SAVE", box, juce::Justification::centred, false);
    }

    /** The close cross, on a disc under the pointer. */
    inline void paintClose (juce::Graphics& g, juce::Rectangle<float> box, bool hovered, float s)
    {
        using namespace Synth2;
        if (hovered)
        {
            g.setColour (Palette::idleFill.withMultipliedAlpha (1.6f));
            g.fillEllipse (box);
        }
        const auto c = box.getCentre();
        const float r = 5.0f * s;
        juce::Path cross;
        cross.startNewSubPath (c.x - r, c.y - r);
        cross.lineTo (c.x + r, c.y + r);
        cross.startNewSubPath (c.x + r, c.y - r);
        cross.lineTo (c.x - r, c.y + r);
        g.setColour (hovered ? Palette::textStrong : Palette::textDim);
        g.strokePath (cross, juce::PathStrokeType (juce::jmax (1.0f, 1.6f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** DELETE, in pink; `armed` once clicked, asking for a second. */
    inline void paintDelete (juce::Graphics& g, juce::Rectangle<float> box, bool armed, bool hovered, float s)
    {
        using namespace Synth2;
        float fill = hovered ? 0.16f : 0.08f;
        if (armed)
            fill = 0.24f;
        g.setColour (Palette::pink.withAlpha (fill));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (Palette::pink.withAlpha (armed || hovered ? 0.9f : 0.45f));
        g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, juce::jmax (1.0f, s));
        g.setFont (Fonts::sans (10.0f * s, true, 0.14f));
        g.setColour (Palette::pink);
        g.drawText (armed ? "CLICK AGAIN TO DELETE" : "DELETE", box, juce::Justification::centred, false);
    }

    inline juce::String categoryName (int category)
    {
        if (category == PresetCatalog::all)
            return "All";
        if (category == PresetCatalog::user)
            return "User";
        return Presets::categoryNames[(size_t) category];
    }

    /** The categories, each with how many presets it holds: the one showing
        lit, one with none greyed out, and USER set apart under ALL. */
    inline void paintCategories (juce::Graphics& g, const Layout& layout, const PresetCatalog& catalog, std::optional<int> hovered)
    {
        using namespace Synth2;
        const float s = layout.s;
        for (int category = PresetCatalog::user; category < Presets::numCategories; ++category)
        {
            const auto row     = layout.categoryBounds (category);
            const int  count   = catalog.countIn (category);
            const bool showing = category == catalog.getShown();
            const bool live    = count > 0 || category == PresetCatalog::user;
            if (category == PresetCatalog::user)
            {
                g.setColour (Palette::hairline);
                g.fillRect (row.withY (row.getBottom() - 0.5f * s).withHeight (juce::jmax (1.0f, s)).reduced (8.0f * s, 0.0f));
            }
            if (showing || (hovered == category && live))
            {
                g.setColour (showing ? Palette::accentSoft : Palette::idleFill.withMultipliedAlpha (1.6f));
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f * s), 8.0f * s);
            }

            auto tint      = showing ? Palette::accent : Palette::textStrong;
            auto countTint = showing ? Palette::accent : Palette::textDim;
            if (! live)
                tint = Palette::textFaint;
            if (count == 0)
                countTint = Palette::textFaint;
            auto text = row.reduced (12.0f * s, 0.0f);
            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (countTint);
            g.drawText (juce::String (count), text.removeFromRight (24.0f * s), juce::Justification::centredRight, false);
            g.setFont (Fonts::sans (13.0f * s, true));
            g.setColour (tint);
            g.drawText (categoryName (category), text, juce::Justification::centredLeft, true);
        }
    }

    /** One preset in the list: its name and line, and at the right its
        oscillators' shapes, MONO if it is, USER if it is the user's, and its
        category; lit if `loaded`, or under the pointer. */
    inline void paintRow (juce::Graphics& g, juce::Rectangle<float> box, const PresetCatalog::Shown& shown, bool loaded, bool hovered, float s)
    {
        using namespace Synth2;
        const auto& preset = shown.sound;
        if (loaded || hovered)
        {
            g.setColour (loaded ? Palette::accentSoft : Palette::idleFill.withMultipliedAlpha (1.6f));
            g.fillRoundedRectangle (box.reduced (0.0f, 2.0f * s), 10.0f * s);
        }
        if (loaded)
        {
            g.setColour (Palette::accent);
            g.fillRoundedRectangle (box.reduced (0.0f, 14.0f * s).withWidth (3.0f * s), 1.5f * s);
        }

        auto inside = box.reduced (16.0f * s, 8.0f * s);
        const float centreY = inside.getCentreY();
        float right = inside.getRight();
        const auto chipAtRight = [&g, &right, centreY, s] (const juce::String& text, juce::Colour colour, float gap)
        {
            right -= chipWidth (text, s) + gap;
            paintChip (g, text, right, centreY, colour, s);
        };
        chipAtRight (juce::String (Presets::nameOf (preset.category)).toUpperCase(), Palette::textDim, 0.0f);
        if (shown.isUser)
            chipAtRight ("USER", Palette::cyan, 6.0f * s);
        if (preset.mono)
            chipAtRight ("MONO", Palette::violet, 6.0f * s);
        if (preset.arp.has_value() && preset.category != Presets::Category::arp)   // (an arp preset says so already)
            chipAtRight ("ARP", Palette::amber, 6.0f * s);
        for (int slot = Bench::numOscillators - 1; slot >= 0; --slot)
            if (const auto& osc = preset.oscillators[(size_t) slot]; osc.has_value())
            {
                right -= 26.0f * s;
                paintGlyph (g, (Waveform) osc->wave, juce::Rectangle (right, centreY - 6.0f * s, 20.0f * s, 12.0f * s), slot, false, s);
            }

        inside.setRight (right - 12.0f * s);
        g.setFont (Fonts::sans (14.0f * s, true));
        g.setColour (Palette::textStrong);
        g.drawText (shown.name, inside.removeFromTop (20.0f * s), juce::Justification::centredLeft, true);
        g.setFont (Fonts::sans (11.0f * s));
        g.setColour (Palette::textDim);
        g.drawText (shown.detail, inside, juce::Justification::centredLeft, true);
    }

    /** The list: the presets listed, the loaded one lit, scrolled `scroll`
        design pixels down, with a thin scroll bar if it is longer than its
        box; or, with none, why. */
    inline void paintList (juce::Graphics& g, const Layout& layout, const PresetCatalog& catalog, float scroll, int hoveredRow,
                           const juce::String& search)
    {
        using namespace Synth2;
        const float s      = layout.s;
        const auto  list   = layout.listArea();
        const auto& listed = catalog.getListed();
        const juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (list.getSmallestIntegerContainer());

        if (listed.empty())
        {
            g.setFont (Fonts::sans (13.0f * s));
            g.setColour (Palette::textDim);
            if (catalog.getShown() == PresetCatalog::user && catalog.userCount() == 0)
                g.drawFittedText ("No presets of your own yet.\nSAVE keeps the sound as it is now, to come back to.",
                                  list.withHeight (60.0f * s).getSmallestIntegerContainer(), juce::Justification::centred, 2, 1.0f);
            else
                g.drawText ("No presets match \"" + search + "\"", list.withHeight (60.0f * s), juce::Justification::centred, true);
            return;
        }

        const auto loaded = catalog.currentItem();
        for (int row = 0; row < (int) listed.size(); ++row)
            if (const auto box = layout.rowBounds (row, scroll); box.intersects (list))
                paintRow (g, box, catalog.describe (listed[(size_t) row]), listed[(size_t) row] == loaded, row == hoveredRow, s);

        if (const float most = layout.maxScroll ((int) listed.size()); most > 0.0f)
        {
            const float total = (float) listed.size() * rowHeight * s;
            const float thumb = list.getHeight() * list.getHeight() / total;
            const float y     = list.getY() + (list.getHeight() - thumb) * (scroll / most);
            g.setColour (Palette::strongLine);
            g.fillRoundedRectangle (juce::Rectangle (list.getRight() - 4.0f * s, y, 3.0f * s, thumb), 1.5f * s);
        }
    }

    inline juce::String envelopeText (const Presets::Envelope& env)
    {
        using namespace Synth2;
        return "A " + Format::seconds (env.attackSeconds) + "  D " + Format::seconds (env.decaySeconds)
             + "  S " + Format::percent (env.sustainLevel) + "  R " + Format::seconds (env.releaseSeconds);
    }

    /** What shapes an oscillator: its filter, or else its envelope. */
    inline juce::String shapingText (const Presets::Oscillator& osc)
    {
        using namespace Synth2;
        static constexpr std::array<const char*, 6> filterNames { "LOW 24", "LOW 12", "BAND", "HIGH 12", "HIGH 24", "NOTCH" };
        if (osc.filter.has_value())
            return juce::String (filterNames[(size_t) juce::jlimit (0, 5, osc.filter->type)]) + "  " + Format::hertz (osc.filter->cutoffHz)
                 + "  RES " + Format::percent (osc.filter->resonance);
        if (osc.envelope.has_value())
            return envelopeText (*osc.envelope);
        return "No envelope";
    }

    /** OSCILLATORS: each one's shape in a well, then its wave, pitch and
        stack, and its filter or envelope. */
    inline void paintOscillators (juce::Graphics& g, juce::Rectangle<float>& area, const Presets::Preset& preset, float s)
    {
        using namespace Synth2;
        paintLabel (g, area, "OSCILLATORS", s);
        bool any = false;
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
        {
            const auto& osc = preset.oscillators[(size_t) slot];
            if (! osc.has_value())
                continue;
            any = true;
            auto row = area.removeFromTop (44.0f * s);
            paintGlyph (g, (Waveform) osc->wave, row.removeFromLeft (48.0f * s).withSizeKeepingCentre (48.0f * s, 30.0f * s), slot, true, s);
            row.removeFromLeft (10.0f * s);
            const int voices = osc->unison.voices;
            const int wave   = juce::jlimit (0, WaveGlyph::numWaves - 1, osc->wave);
            auto line = juce::String (WaveGlyph::longNames[(size_t) wave]);
            if (wave != (int) Waveform::noise)   // which has no pitch or stack
                line << "  " << Format::semitones (osc->semitones) << "  " << voices << (voices == 1 ? " voice" : " voices");
            g.setFont (Fonts::sans (12.0f * s, true));
            g.setColour (Palette::textMid);
            g.drawText (line,
                        row.removeFromTop (22.0f * s).withTrimmedTop (4.0f * s), juce::Justification::centredLeft, true);
            g.setFont (Fonts::mono (10.0f * s));
            g.setColour (Palette::textDim);
            g.drawText (shapingText (*osc), row.removeFromTop (16.0f * s), juce::Justification::centredLeft, true);
        }
        if (! any)
        {
            g.setFont (Fonts::sans (12.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("Nothing on the bench yet", area.removeFromTop (24.0f * s), juce::Justification::centredLeft, false);
        }
    }

    /** PLAYING (POLY or MONO, GLIDE and OUTPUT), then EFFECTS (a chip for each). */
    inline void paintPlayingAndEffects (juce::Graphics& g, juce::Rectangle<float>& area, const Presets::Preset& preset, float s)
    {
        using namespace Synth2;
        paintLabel (g, area, "PLAYING", s);
        g.setFont (Fonts::sans (12.0f * s, true));
        g.setColour (Palette::textMid);
        juce::String playing = preset.mono ? "MONO" : "POLY";
        if (preset.glideSeconds > 0.0f)
            playing += "  GLIDE " + Format::seconds (preset.glideSeconds);
        if (preset.arp.has_value())
        {
            playing += "  ARP " + juce::String (Arp::rates[(size_t) juce::jlimit (0, (int) Arp::rates.size() - 1, preset.arp->rate)].name);
            if (preset.arp->octaves > 1)
                playing += " " + juce::String (preset.arp->octaves) + " OCT";
        }
        playing += "  OUTPUT " + Format::decibels (preset.outputDb);
        g.drawText (playing, area.removeFromTop (22.0f * s), juce::Justification::centredLeft, true);

        paintLabel (g, area, "EFFECTS", s);
        const auto chips = area.removeFromTop (22.0f * s);
        float x = chips.getX();
        if (preset.drive.has_value())
            x += paintChip (g, "DRIVE", x, chips.getCentreY(), Palette::amber, s) + 6.0f * s;
        if (preset.eq.has_value())
            x += paintChip (g, "EQ", x, chips.getCentreY(), Palette::pink, s) + 6.0f * s;
        if (preset.delay.has_value())
            x += paintChip (g, "DELAY " + juce::String (Effects::divisions[(size_t) preset.delay->division].name).toUpperCase(), x,
                            chips.getCentreY(), Palette::cyan, s) + 6.0f * s;
        if (preset.reverb.has_value())
            x += paintChip (g, "REVERB", x, chips.getCentreY(), Palette::violet, s) + 6.0f * s;
        if (juce::exactlyEqual (x, chips.getX()))
        {
            g.setFont (Fonts::sans (12.0f * s));
            g.setColour (Palette::textDim);
            g.drawText ("None: dry", chips, juce::Justification::centredLeft, false);
        }
    }

    /** The loaded preset, in `area`: its category (and USER, if it is the
        user's), name and line, its oscillators, how it plays and its
        effects; and at the foot, how to get about. */
    inline void paintDetail (juce::Graphics& g, juce::Rectangle<float> area, const PresetCatalog::Shown& shown, float s)
    {
        using namespace Synth2;
        const auto whole = area;
        const float categoryWidth = paintChip (g, juce::String (Presets::nameOf (shown.sound.category)).toUpperCase(), area.getX(),
                                               area.getY() + 9.0f * s, Palette::accent, s);
        if (shown.isUser)
            paintChip (g, "USER", area.getX() + categoryWidth + 6.0f * s, area.getY() + 9.0f * s, Palette::cyan, s);
        area.removeFromTop (26.0f * s);
        g.setFont (Fonts::sans (24.0f * s, true));
        g.setColour (Palette::textStrong);
        g.drawText (shown.name, area.removeFromTop (34.0f * s), juce::Justification::centredLeft, true);
        g.setFont (Fonts::sans (12.0f * s));
        g.setColour (Palette::textDim);
        g.drawFittedText (shown.detail, area.removeFromTop (34.0f * s).getSmallestIntegerContainer(), juce::Justification::topLeft, 2, 1.0f);

        paintOscillators (g, area, shown.sound, s);
        paintPlayingAndEffects (g, area, shown.sound, s);

        g.setFont (Fonts::sans (10.0f * s));
        g.setColour (Palette::textFaint);
        g.drawFittedText ("Click a preset to hear it; double-click to load it and close.\nUp and down step through the list; Esc closes.",
                          whole.withTrimmedTop (whole.getHeight() - 32.0f * s).getSmallestIntegerContainer(), juce::Justification::bottomLeft, 2, 1.0f);
    }
}

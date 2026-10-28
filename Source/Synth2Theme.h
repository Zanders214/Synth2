#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

/**
    Shared visual language for the Synth2 editor, from the Neon Plugins design
    system: the colour palette, font helpers, small colour-mixing utilities, and
    the numeric value formatters. Kept in one place so the LookAndFeel, the
    displays and the keyboard all speak the same tokens.
*/
namespace Synth2
{
    namespace Palette
    {
        // Backgrounds / surfaces
        const juce::Colour panelTop    { 0xff1a2030 };
        const juce::Colour panelBottom { 0xff0a0b12 };
        const juce::Colour well        { 0xff070a0e };   // inset display well
        const juce::Colour dialSeat    = juce::Colour::fromFloatRGBA (0.031f, 0.043f, 0.071f, 0.6f);
        const juce::Colour chipSurface = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.05f);
        const juce::Colour idleFill    = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.06f);
        const juce::Colour ringTrack   = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.12f);
        const juce::Colour gridLine    = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.08f);
        const juce::Colour hairline    = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.06f);
        const juce::Colour strongLine  = juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.12f);

        // Text
        const juce::Colour textStrong  { 0xffe8ecf3 };
        const juce::Colour textMid     { 0xffcfd4dc };
        const juce::Colour textUnit    { 0xff9aa3b3 };
        const juce::Colour textDim     { 0xff8a93a3 };
        const juce::Colour textDimmer  { 0xff7e8794 };
        const juce::Colour textFaint   { 0xff5d6473 };

        // The spectrum, low to high, and the one accent
        const juce::Colour cyan   { 0xff34d8ff };
        const juce::Colour violet { 0xff8b7bff };  // Synth2's own colour (the wordmark)
        const juce::Colour pink   { 0xffff5fa8 };
        const juce::Colour amber  { 0xffffc24b };
        const juce::Colour accent { 0xff5e93ff };  // badge, selection
        const juce::Colour accentSoft = juce::Colour::fromFloatRGBA (0.369f, 0.576f, 1.0f, 0.12f);

        // Keys
        const juce::Colour keyWhite      { 0xfff8f5ef };
        const juce::Colour keyWhiteDown  { 0xffe4dcff };
        const juce::Colour keyBlack      { 0xff1a1714 };
        const juce::Colour keyBlackDown  { 0xff3b3358 };
    }

    /**
        Font helpers. The design system calls for Space Grotesk (UI) + JetBrains
        Mono (numbers). We use them when the host machine has them installed and
        fall back to the platform's default sans / monospaced face otherwise.
        Bundling the TTFs as BinaryData later is a localized swap inside these
        two functions.
    */
    namespace Fonts
    {
        inline juce::Font sans (float height, bool bold = false, float letterSpacingEm = 0.0f)
        {
            static const bool hasSpaceGrotesk = juce::Font::findAllTypefaceNames().contains ("Space Grotesk");

            auto options = juce::FontOptions().withHeight (height);
            if (hasSpaceGrotesk)
                options = options.withName ("Space Grotesk");
            if (bold)
                options = options.withStyle ("Bold");

            juce::Font f (options);
            if (std::abs (letterSpacingEm) > 1.0e-6f)
                f.setExtraKerningFactor (letterSpacingEm);
            return f;
        }

        inline juce::Font mono (float height, float letterSpacingEm = 0.0f)
        {
            static const juce::String name = juce::Font::findAllTypefaceNames().contains ("JetBrains Mono")
                                               ? juce::String ("JetBrains Mono")
                                               : juce::Font::getDefaultMonospacedFontName();

            juce::Font f (juce::FontOptions().withName (name).withHeight (height));
            if (std::abs (letterSpacingEm) > 1.0e-6f)
                f.setExtraKerningFactor (letterSpacingEm);
            return f;
        }

        /** How wide `text` sets in `font`. */
        inline float width (const juce::Font& font, const juce::String& text)
        {
            juce::GlyphArrangement arrangement;
            arrangement.addLineOfText (font, text, 0.0f, 0.0f);
            return arrangement.getBoundingBox (0, -1, true).getWidth();
        }
    }

    namespace ColourMath
    {
        inline juce::Colour lerp (juce::Colour a, juce::Colour b, float t) noexcept
        {
            t = juce::jlimit (0.0f, 1.0f, t);
            return juce::Colour::fromFloatRGBA (a.getFloatRed()   + (b.getFloatRed()   - a.getFloatRed())   * t,
                                                a.getFloatGreen() + (b.getFloatGreen() - a.getFloatGreen()) * t,
                                                a.getFloatBlue()  + (b.getFloatBlue()  - a.getFloatBlue())  * t,
                                                a.getFloatAlpha() + (b.getFloatAlpha() - a.getFloatAlpha()) * t);
        }

        /** The four-stop spectrum sampled at f in 0..1
            (cyan -> violet -> pink -> amber, evenly spaced). */
        inline juce::Colour spectrum (float f) noexcept
        {
            using namespace Palette;
            if (f < 1.0f / 3.0f) return lerp (cyan,   violet, f * 3.0f);
            if (f < 2.0f / 3.0f) return lerp (violet, pink,  (f - 1.0f / 3.0f) * 3.0f);
            return                       lerp (pink,   amber, (f - 2.0f / 3.0f) * 3.0f);
        }
    }

    namespace Format
    {
        /** 0..1 as an integer percentage, e.g. "73%". */
        inline juce::String percent (float x)
        {
            return juce::String (juce::roundToInt (x * 100.0f)) + "%";
        }

        /** A time in seconds: "NNN ms" below a second, "X.XX s" above. */
        inline juce::String seconds (float s)
        {
            if (s < 1.0f)
                return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
            return juce::String (s, 2) + " s";
        }

        /** The unison detune, as the outer voices' offset: "±12.5 ct". */
        inline juce::String cents (float c)
        {
            return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (c, 1) + " ct";
        }

        /** A level in dB with its sign, e.g. "-6.0 dB" / "0.0 dB" / "+3.0 dB". */
        inline juce::String decibels (float db)
        {
            return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
        }
    }
}

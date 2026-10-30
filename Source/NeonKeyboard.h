#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <array>

#include "Synth2Theme.h"

/**
    NeonKeyboard — the on-screen keyboard along the bottom of the panel, after
    the design system's Keyboard: ivory keys with rounded bottoms and a soft
    shadow at their tips, near-black sharps, and a pressed key that tints violet
    and glows, whether it was clicked or played from the host.

    Three octaves, C3 to C6, always fitted to the component's width.

    It can also be played from the computer's keys (the standalone app does,
    see typedKeyPressed): the middle row from A to ; is a white-key octave and
    a third from the typing octave's C, the row above it (W E T Y U O P) the
    black keys, and Z and X shift the octave down and up. A small letter on
    the key A plays shows where that is. Each held key remembers its note, so
    shifting the octave while playing leaves nothing hanging.
*/
class NeonKeyboard final : public juce::MidiKeyboardComponent
{
public:
    static constexpr int lowestNote  = 48;   // C3
    static constexpr int highestNote = 84;   // C6
    static constexpr int numWhiteKeys = 22;

    explicit NeonKeyboard (juce::MidiKeyboardState& state)
        : MidiKeyboardComponent (state, horizontalKeyboard),
          notes (state)
    {
        setAvailableRange (lowestNote, highestNote);
        setScrollButtonsVisible (false);
        setVelocity (velocity, false);
        setBlackNoteLengthProportion (0.61f);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        clearKeyMappings();   // typed notes are played by typedKeysChanged instead

        // The background (drawn by JUCE, in the white-key colour) shows in the
        // gaps between the keys: make it the panel's, with no shadow over it.
        setColour (whiteNoteColourId, Synth2::Palette::panelBottom);
        setColour (shadowColourId, juce::Colours::transparentBlack);
    }

    void setUiScale (float newScale) { uiScale = newScale; repaint(); }

    // ---- Playing from the computer's keys --------------------------------------------

    /** The keys that play notes, a semitone apart from the typing octave's C. */
    static constexpr const char* typingKeys = "awsedftgyhujkolp;";
    static constexpr int numTypingKeys = 17;
    static constexpr int lowestTypingC  = 24;    // C1
    static constexpr int highestTypingC = 96;    // C7

    /** Shows (or hides) the letter on the key A plays. */
    void setShowsTypingKeys (bool shouldShow)
    {
        if (showsTypingKeys != shouldShow)
        {
            showsTypingKeys = shouldShow;
            repaint();
        }
    }

    /** The note the A key plays. */
    int getTypingC() const noexcept { return typingC; }

    /** A key went down: Z and X shift the typing octave. True if `key` is one
        this keyboard plays (or shifts with), so it goes no further. */
    bool typedKeyPressed (const juce::KeyPress& key)
    {
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
            return false;
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (c == 'z' || c == 'x')
        {
            typingC = juce::jlimit (lowestTypingC, highestTypingC, typingC + (c == 'z' ? -12 : 12));
            repaint();
            return true;
        }
        return c != 0 && juce::String (typingKeys).containsChar (c);
    }

    /** Plays and releases the typed notes after the keys that are down
        (`isDown` asks about one of typingKeys); with a command key held none
        are, so shortcuts play nothing. True if any note started or stopped. */
    template <typename IsDown>
    bool typedKeysChanged (IsDown&& isDown)
    {
        const bool shortcut = juce::ModifierKeys::currentModifiers.isCommandDown()
                           || juce::ModifierKeys::currentModifiers.isCtrlDown();
        bool changed = false;
        for (size_t i = 0; i < (size_t) numTypingKeys; ++i)
        {
            const bool down = ! shortcut && isDown (typingKeys[i]);
            if (down && typedNotes[i] < 0)
            {
                typedNotes[i] = typingC + (int) i;
                notes.noteOn (typingChannel, typedNotes[i], velocity);
                changed = true;
            }
            else if (! down && typedNotes[i] >= 0)
            {
                notes.noteOff (typingChannel, typedNotes[i], 0.0f);
                typedNotes[i] = -1;
                changed = true;
            }
        }
        return changed;
    }

    /** The same, asking the system which keys are down. */
    bool typedKeysChanged()
    {
        return typedKeysChanged ([] (char c) { return juce::KeyPress::isKeyCurrentlyDown (c); });
    }

    /** Whether any typed note is sounding. */
    bool holdsTypedNotes() const noexcept
    {
        return std::any_of (typedNotes.begin(), typedNotes.end(), [] (int note) { return note >= 0; });
    }

    void resized() override
    {
        setKeyWidth ((float) getWidth() / (float) numWhiteKeys);
        setLowestVisibleKey (lowestNote);
        MidiKeyboardComponent::resized();
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override
    {
        using namespace Synth2;
        const float s = uiScale;
        auto key = area.reduced (0.5f * s, 0.0f);
        if (isDown)
            key = key.withTrimmedTop (2.0f * s);   // the 2 px press

        juce::Path shape;
        shape.addRoundedRectangle (key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                   4.0f * s, 4.0f * s, false, false, true, true);

        auto colour = isOver ? Palette::keyWhite.darker (0.04f) : Palette::keyWhite;
        if (isDown)
            colour = Palette::keyWhiteDown;
        g.setColour (colour);
        g.fillPath (shape);

        // inset 0 -7px 9px -7px rgba(0,0,0,.22): a soft shade at the key's tip.
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, key.getBottom() - 8.0f * s,
                                                 juce::Colours::black.withAlpha (0.22f), 0.0f, key.getBottom(), false));
        g.fillPath (shape);

        if (isDown)
        {
            g.setColour (Palette::violet.withAlpha (0.55f));
            g.strokePath (shape, juce::PathStrokeType (2.0f * s));
        }

        if (showsTypingKeys && note == typingC)
        {
            g.setFont (Fonts::mono (9.0f * s));
            g.setColour (Palette::violet.withAlpha (0.8f));
            g.drawText ("A", key.withTrimmedBottom (8.0f * s).removeFromBottom (12.0f * s), juce::Justification::centred, false);
        }
    }

    void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override
    {
        using namespace Synth2;
        const float s = uiScale;
        auto key = area.reduced (1.0f * s, 0.0f);
        if (isDown)
            key = key.withTrimmedTop (2.0f * s);

        juce::Path shape;
        shape.addRoundedRectangle (key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                   3.0f * s, 3.0f * s, false, false, true, true);

        if (isDown)
            juce::DropShadow (Palette::violet.withAlpha (0.8f), (int) (10.0f * s), {}).drawForPath (g, shape);
        else
            juce::DropShadow (juce::Colours::black.withAlpha (0.4f), (int) (4.0f * s), { 0, (int) (3.0f * s) }).drawForPath (g, shape);

        auto colour = isOver ? Palette::keyBlack.brighter (0.15f) : Palette::keyBlack;
        if (isDown)
            colour = Palette::keyBlackDown;
        g.setColour (colour);
        g.fillPath (shape);

        // inset 0 -3px 5px rgba(255,255,255,.07): a faint lip at the tip.
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, 0.0f, key.getBottom() - 5.0f * s,
                                                 juce::Colours::white.withAlpha (0.07f), 0.0f, key.getBottom(), false));
        g.fillPath (shape);
    }

private:
    static constexpr int   typingChannel = 1;
    static constexpr float velocity      = 0.8f;

    juce::MidiKeyboardState& notes;
    float uiScale = 1.0f;
    int   typingC = 60;   // C4
    bool  showsTypingKeys = false;
    std::array<int, numTypingKeys> typedNotes = make (-1);

    static std::array<int, numTypingKeys> make (int value)
    {
        std::array<int, numTypingKeys> notes {};
        notes.fill (value);
        return notes;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonKeyboard)
};

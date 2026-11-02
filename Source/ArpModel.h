#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Arp.h"

/**
    Arp::Model

    The arp's pattern (see Arp.h), kept in an ARP child of the plugin's
    state tree, beside the bench and the rack, so it is saved and restored
    with the session and in a user preset:

        ARP { version: 1, length: 1..16 }
          STEP { rows: 0..255, velocity: 0..1, gate: 0..3 }   (sixteen of them)

    `rows` has a bit for each row the step plays (bit 0: the lowest key
    held), `gate` is its Arp::Length. The steps past `length` are kept, so
    shortening a pattern and lengthening it again gets them back. Whether
    the arp plays, RATE, SWING and LATCH are parameters (see ParamIDs.h).
    Message thread only.
*/
namespace Arp
{
    inline const juce::Identifier arpType          { "ARP" };
    inline const juce::Identifier stepType         { "STEP" };
    inline const juce::Identifier lengthProperty   { "length" };
    inline const juce::Identifier rowsProperty     { "rows" };
    inline const juce::Identifier velocityProperty { "velocity" };
    inline const juce::Identifier gateProperty     { "gate" };
    inline const juce::Identifier arpVersionProperty { "version" };
    inline constexpr int          currentVersion = 1;

    /** The pattern in `arp` (an ARP tree), or the one to start with if it isn't one. */
    inline Pattern patternIn (const juce::ValueTree& arp)
    {
        if (! arp.hasType (arpType))
            return defaultPattern();
        Pattern pattern;
        pattern.length = juce::jlimit (1, maxSteps, (int) arp.getProperty (lengthProperty, 8));
        for (int i = 0; i < maxSteps && i < arp.getNumChildren(); ++i)
        {
            const auto step = arp.getChild (i);
            pattern[i] = { (std::uint8_t) juce::jlimit (0, 255, (int) step.getProperty (rowsProperty, 0)),
                           juce::jlimit (0.0f, 1.0f, (float) step.getProperty (velocityProperty, 0.8f)),
                           (Length) juce::jlimit (0, numLengths - 1, (int) step.getProperty (gateProperty, (int) Length::half)) };
        }
        return pattern;
    }

    class Model
    {
    public:
        explicit Model (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse) {}

        /** Makes sure the state has a pattern: the one to start with if not. */
        void ensureArp()
        {
            if (! arp().isValid())
                setPattern (defaultPattern());
        }

        Pattern pattern() const { return patternIn (arp()); }

        /** Sets the whole pattern (a preset's, or one a button made). */
        void setPattern (const Pattern& pattern)
        {
            auto tree = state.state.getOrCreateChildWithName (arpType, nullptr);
            tree.setProperty (arpVersionProperty, currentVersion, nullptr);
            tree.setProperty (lengthProperty, juce::jlimit (1, maxSteps, pattern.length), nullptr);
            while (tree.getNumChildren() < maxSteps)
                tree.appendChild (juce::ValueTree (stepType), nullptr);
            for (int i = 0; i < maxSteps; ++i)
                write (tree.getChild (i), pattern[i]);
        }

        void setStep (int index, const Step& step)
        {
            if (juce::isPositiveAndBelow (index, maxSteps))
            {
                ensureArp();
                write (arp().getChild (index), step);
            }
        }

        void setLength (int length)
        {
            ensureArp();
            arp().setProperty (lengthProperty, juce::jlimit (1, maxSteps, length), nullptr);
        }

    private:
        juce::ValueTree arp() const { return state.state.getChildWithName (arpType); }

        static void write (juce::ValueTree step, const Step& value)
        {
            step.setProperty (rowsProperty, (int) value.rows, nullptr);
            step.setProperty (velocityProperty, (double) juce::roundToInt (value.velocity * 127.0f) / 127.0, nullptr);
            step.setProperty (gateProperty, (int) value.length, nullptr);
        }

        juce::AudioProcessorValueTreeState& state;
    };
}

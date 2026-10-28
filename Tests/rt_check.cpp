// RealtimeSanitizer driver for Synth2.
//
// Exercises the real audio callback under clang's -fsanitize=realtime so any
// allocation / lock / syscall on the audio thread becomes a hard failure. It
// plays the worst case: sixteen-note chords of sixteen detuned voices, with
// notes stolen, the sustain pedal, the on-screen keyboard's notes, and the
// settings moving underneath (VOICES, WAVE, the envelope) while they sound.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    Synth2AudioProcessor proc;
    const double sr = 48000.0;
    const int    bs = 512;
    proc.prepareToPlay (sr, bs);

    const auto set = [&proc] (const char* id, float value)
    {
        auto* p = proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamID::voices, 16.0f);
    set (ParamID::detune, 30.0f);
    set (ParamID::width, 1.0f);

    juce::AudioBuffer<float> buffer (2, bs);
    const int blocks = (int) (sr / bs) * 2;          // ~2 s of callbacks

    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;

        // A new chord every 8 blocks: 20 notes, so four are always stolen.
        if (b % 8 == 0)
            for (int n = 0; n < 20; ++n)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (b / 8 + n * 5) % 60, 0.8f), n * 7);
        if (b % 8 == 6)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 100);
        if (b % 16 == 3)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        if (b % 16 == 11)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        if (b % 10 == 5)
            proc.getKeyboardState().noteOn (1, 72, 0.7f);
        if (b % 10 == 9)
            proc.getKeyboardState().noteOff (1, 72, 0.0f);

        // Settings move while notes sound, between callbacks, as the editor does.
        if (b % 12 == 0)
        {
            set (ParamID::wave,    (float) ((b / 12) % 4));
            set (ParamID::voices,  (float) (1 + (b / 12) % 16));
            set (ParamID::attack,  0.001f + 0.01f * (float) ((b / 12) % 5));
            set (ParamID::release, 0.05f + 0.1f * (float) ((b / 12) % 5));
        }

        proc.processBlock (buffer, midi);           // exercises the annotated DSP
    }

    std::printf ("PASS: %d blocks of 16-voice unison chords (stolen notes, sustain pedal,\n"
                 "      on-screen keys, settings changing mid-note), no RT-safety violations\n", blocks);
    return 0;
}

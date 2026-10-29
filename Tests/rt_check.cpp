// RealtimeSanitizer driver for Synth2.
//
// Exercises the real audio callback (all of processBlock) under clang's
// -fsanitize=realtime so any allocation / lock / syscall on the audio thread
// becomes a hard failure. It
// plays the worst case: sixteen-note chords of sixteen detuned voices, both
// oscillators on, with notes stolen, the sustain pedal, the on-screen
// keyboard's notes, and the settings moving underneath (VOICES, both WAVEs,
// OSC 2 switching on and off, the tuning, the envelope) while they sound.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include <cstdio>

// The whole audio callback, not just the engine, runs as a real-time context:
// the parameter reads, the on-screen keys' hand-over and publishing the held
// notes are checked along with the DSP.
static void audioCallback (Synth2AudioProcessor& proc, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) SYNTH2_RT_NONBLOCKING
{
    proc.processBlock (buffer, midi);
}

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
    set (ParamID::osc2On, 1.0f);

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
            set (ParamID::osc2Wave,  (float) ((b / 12 + 1) % 4));
            set (ParamID::osc2Pitch, (float) ((b / 12) % 25 - 12));
            set (ParamID::osc1Fine,  (float) ((b / 12) % 11) * 5.0f - 25.0f);
        }
        if (b % 40 == 20)
            set (ParamID::osc2On, 0.0f);
        if (b % 40 == 30)
            set (ParamID::osc2On, 1.0f);

        // Modules come and go from the bench, and the patch is rewired.
        if (b % 24 == 4)
        {
            set (ParamID::osc3On,     (float) ((b / 24) % 2));
            set (ParamID::osc4On,     (float) ((b / 48) % 2));
            set (ParamID::osc4Wave,   (float) ((b / 24) % 4));
            set (ParamID::unisonOn,   (float) ((b / 24 + 1) % 2));
            set (ParamID::envelopeOn, (float) ((b / 72 + 1) % 2));
            set (ParamID::signalFlow, (float) ((b / 24) % 3));
            proc.setRoutedToOutput ((b / 24) % 4, (b / 24) % 3 != 0);
        }

        audioCallback (proc, buffer, midi);         // the full callback, under RTSan
    }

    std::printf ("PASS: %d blocks of 16-voice unison chords on up to four oscillators (stolen notes,\n"
                 "      sustain pedal, on-screen keys, settings, modules and routing changing mid-note),\n"
                 "      no RT-safety violations\n", blocks);
    return 0;
}

// RealtimeSanitizer driver for Synth2.
//
// Exercises the real audio callback (all of processBlock) under clang's
// -fsanitize=realtime so any allocation / lock / syscall on the audio thread
// becomes a hard failure. It
// plays the worst case: sixteen-note chords of sixteen detuned voices, both
// oscillators on, with notes stolen, the sustain pedal, the on-screen
// keyboard's notes, and the settings moving underneath (each oscillator's
// VOICES, both WAVEs, OSC 2 switching on and off, the tuning, every stage of
// the envelopes and which oscillator each shapes) while they sound.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Bench.h"
#include "ParamIDs.h"
#include "Rack.h"
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
    set (ParamID::osc1Voices, 16.0f);
    set (ParamID::osc1Detune, 30.0f);
    set (ParamID::osc1Width, 1.0f);
    set (ParamID::osc1On, 1.0f);   // a new instance opens on an empty bench
    set (ParamID::osc2On, 1.0f);
    set (ParamID::osc2Voices, 16.0f);
    set (ParamID::env1On, 1.0f);
    Bench::Model bench (proc.getState());
    Rack::Model rack (proc.getState());
    for (const auto kind : Rack::allEffects)   // and an empty EFFECTS tab
        set (Rack::inChainParameter (kind), 1.0f);

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
            set (ParamID::osc1Wave,    (float) ((b / 12) % 4));
            set (ParamID::osc1Voices,  (float) (1 + (b / 12) % 16));
            set (ParamID::env1Attack,  0.001f + 0.01f * (float) ((b / 12) % 5));
            set (ParamID::env1Release, 0.05f + 0.1f * (float) ((b / 12) % 5));
            set (ParamID::env1Delay,   0.005f * (float) ((b / 12) % 3));
            set (ParamID::env1Hold,    0.01f * (float) ((b / 12 + 1) % 3));
            set (ParamID::env1Decay,   0.02f + 0.05f * (float) ((b / 12) % 4));
            set (ParamID::env1Sustain, 0.25f * (float) ((b / 12) % 5));
            set (ParamID::osc2Voices, (float) (16 - (b / 12) % 16));
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
            set (ParamID::osc4Voices, (float) (1 + (b / 24) % 16));
            set (ParamID::env2On,     (float) ((b / 24 + 1) % 2));
            set (ParamID::env4On,     (float) ((b / 72 + 1) % 2));
            set (ParamID::osc1Active, (float) ((b / 24) % 3 != 1));   // switched off and on mid-note
            set (ParamID::env1Active, (float) ((b / 24) % 4 != 2));
            set (ParamID::signalFlow, (float) ((b / 24) % 3));
            if ((b / 24) % 2 == 0)
            {
                bench.connect ({ Bench::Kind::oscillator, 1 }, Bench::ModuleId { Bench::Kind::envelope, 1 });
                bench.connect ({ Bench::Kind::envelope, 1 });
            }
            else
            {
                bench.disconnect ({ Bench::Kind::oscillator, 1 });
            }
            proc.setRoutedToOutput ((b / 24) % 4, (b / 24) % 3 != 0);
        }

        // The effects switch on and off, swap order and change time mid-note.
        if (b % 24 == 12)
        {
            // A new order every 72 blocks: every card out, then back in, rotated.
            if (b % 72 == 12)
            {
                for (const auto kind : Rack::allEffects)
                    rack.remove (kind);
                for (int place = 0; place < Effects::numKinds; ++place)
                    rack.add (Rack::allEffects[(size_t) ((place + b / 72) % Effects::numKinds)], Rack::defaultPosition (place));
            }
            set (ParamID::driveOn,       (float) ((b / 24 + 2) % 3 != 0));
            set (ParamID::driveAmount,   0.25f * (float) ((b / 24) % 5));
            set (ParamID::driveShape,    (float) ((b / 24) % 3));
            set (ParamID::driveTone,     0.2f * (float) ((b / 24) % 6));
            set (ParamID::driveMix,      0.5f + 0.25f * (float) ((b / 24) % 3));
            set (ParamID::delayOn,       (float) ((b / 24) % 3 != 0));
            set (ParamID::reverbOn,      (float) ((b / 24 + 1) % 3 != 0));
            set (ParamID::delaySync,     (float) ((b / 24) % 2));
            set (ParamID::delayTime,     50.0f + 150.0f * (float) ((b / 24) % 7));
            set (ParamID::delayNote,     (float) ((b / 24) % 12));
            set (ParamID::delayFeedback, 0.95f);
            set (ParamID::delayPingPong, (float) ((b / 48) % 2));
            set (ParamID::reverbSize,    0.2f * (float) ((b / 24) % 6));
            set (ParamID::reverbPreDelay, 40.0f * (float) ((b / 24) % 7));
            set (ParamID::reverbDamping, 0.25f * (float) ((b / 24) % 5));
        }

        audioCallback (proc, buffer, midi);         // the full callback, under RTSan
    }

    std::printf ("PASS: %d blocks of 16-voice unison chords on up to four oscillators (stolen notes,\n"
                 "      sustain pedal, on-screen keys, settings, modules, envelopes, routing and effects changing mid-note),\n"
                 "      no RT-safety violations\n", blocks);
    return 0;
}

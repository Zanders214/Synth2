/*
    Synth2 plugin tests.

    Drives the real Synth2AudioProcessor and its editor headlessly: parameter
    layout, bus support, MIDI in and audio out, state save/restore, and
    offscreen renders of the whole editor (two sizes) so every paint / layout
    path runs exactly as it does in a host. The DSP maths has its own tests in
    Synth2EngineTests.cpp; this file covers the host-facing wrapper and the UI.

    Same plain check() + int main() shape as the other tests, so it plugs into
    CTest without a framework. On Linux CTest runs it under xvfb-run.
*/

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Bench.h"
#include "ParamIDs.h"
#include "Presets.h"
#include "Rack.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const std::string& message)
{
    std::printf ("  [%s] %s\n", condition ? "PASS" : "FAIL", message.c_str());
    if (! condition)
        ++failures;
}

bool approx (float a, float b, float tolerance) { return std::abs (a - b) <= tolerance; }

float param (const Synth2AudioProcessor& p, const char* id)
{
    return p.getState().getRawParameterValue (id)->load();
}

void setParam (Synth2AudioProcessor& p, const char* id, float value)
{
    auto* prm = p.getState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// Paints a component (and its visible children) into an offscreen software
// image, so the render is identical on every platform and needs no window.
juce::Image render (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

// True when a render drew real content rather than a flat fill.
bool hasDetail (const juce::Image& image)
{
    const auto reference = image.getPixelAt (0, 0);
    int differing = 0;
    for (int y = 0; y < image.getHeight(); y += 4)
        for (int x = 0; x < image.getWidth(); x += 4)
            if (image.getPixelAt (x, y) != reference)
                ++differing;
    return differing > 100;
}

// How many sampled pixels differ between two same-sized renders.
int pixelsChanged (const juce::Image& a, const juce::Image& b)
{
    int changed = 0;
    for (int y = 0; y < a.getHeight(); y += 2)
        for (int x = 0; x < a.getWidth(); x += 2)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                ++changed;
    return changed;
}

// Plays `blocks` blocks through the processor's real audio callback, with
// `midi` in the first, and returns the RMS of the last block's left channel.
float playAndMeasure (Synth2AudioProcessor& p, juce::MidiBuffer midi, int blocks, int block = 512)
{
    juce::AudioBuffer<float> buffer (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer events;
        if (b == 0)
            events.swapWith (midi);
        p.processBlock (buffer, events);
    }
    return buffer.getRMSLevel (0, 0, block);
}

// Puts OSC 1 and ENV 1 on the bench, as a session from before the bench
// opens, so a processor has something to play.
void fillBench (Synth2AudioProcessor& p)
{
    Bench::Migration (p.getState()).adoptClassicLayout();
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

// Runs the message loop until `done` holds, for at most `milliseconds`: the
// editor's displays catch up on its next timer tick, which a slow CI machine
// may not reach inside a fixed wait. Returns whether `done` came to hold.
template <typename Condition>
bool pumpUntil (Condition done, int milliseconds = 2000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
    while (! done())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        pump (10);
    }
    return true;
}

// ---- The processor as a host sees it ------------------------------------------
void testProcessorBasics()
{
    std::printf ("Processor basics:\n");

    Synth2AudioProcessor p;
    check (p.getName() == "ZandersSynth2", "reports its name");
    check (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "takes MIDI in, makes none");
    check (p.hasEditor(), "has an editor");

    for (const auto* id : { ParamID::osc1Wave, ParamID::osc1Voices, ParamID::osc1Detune, ParamID::osc1Width,
                            ParamID::env1Attack, ParamID::env1Release, ParamID::output,
                            ParamID::osc1Pitch, ParamID::osc1Fine, ParamID::osc1Level,
                            ParamID::osc2On, ParamID::osc2Wave, ParamID::osc2Pitch, ParamID::osc2Fine, ParamID::osc2Level,
                            ParamID::osc4Voices, ParamID::osc4Detune, ParamID::osc4Width,
                            ParamID::env1On, ParamID::env1Delay, ParamID::env1Hold, ParamID::env1Decay, ParamID::env1Sustain,
                            ParamID::env4On, ParamID::env4Delay, ParamID::env4Attack, ParamID::env4Hold, ParamID::env4Decay,
                            ParamID::env4Sustain, ParamID::env4Release })
        check (p.getState().getParameter (id) != nullptr, std::string ("has the '") + id + "' parameter");
    check (p.getState().getParameter (ParamID::unisonOn) == nullptr && p.getState().getParameter (ParamID::envelopeOn) == nullptr,
           "the first bench's shared unison and envelope switches are gone");

    check (approx (param (p, ParamID::osc1Wave), (float) Waveform::saw, 1e-6f), "starts on the saw");
    check (approx (param (p, ParamID::osc1Voices), 1.0f, 1e-6f), "starts with one voice");
    check (approx (param (p, ParamID::osc1Detune), 15.0f, 1e-4f) && approx (param (p, ParamID::osc1Width), 0.5f, 1e-6f),
           "DETUNE and WIDTH start at 15 ct and 50%, ready for when VOICES goes up");
    check (approx (param (p, ParamID::env2Delay), 0.0f, 1e-6f) && approx (param (p, ParamID::env2Hold), 0.0f, 1e-6f)
               && approx (param (p, ParamID::env2Sustain), 1.0f, 1e-6f) && approx (param (p, ParamID::env2Decay), 0.3f, 1e-4f),
           "an envelope starts with no delay or hold, sustaining at the top (so older sessions sound as they did)");
    auto* sustain = p.getState().getParameter (ParamID::env2Sustain);
    auto* hold    = p.getState().getParameter (ParamID::env2Hold);
    check (sustain->getText (sustain->convertTo0to1 (0.4f), 32) == "40 %" && hold->getText (hold->convertTo0to1 (0.25f), 32) == "250 ms",
           "SUSTAIN reads as a level, HOLD as a time");

    setParam (p, ParamID::env1Release, 2.0f);
    setParam (p, ParamID::osc1On, 1.0f);
    check (approx ((float) p.getTailLengthSeconds(), Synth2Engine::gateReleaseSeconds, 1e-4f), "without an envelope, the tail is a few ms");
    setParam (p, ParamID::env1On, 1.0f);
    check (approx ((float) p.getTailLengthSeconds(), 2.0f, 0.01f), "with ENV 1 shaping OSC 1, the tail is its release time");

    const auto settings = p.currentSettings();
    check (approx (settings.envelopes[0].releaseSeconds, 2.0f, 0.01f) && settings.oscillators[0].wave == Waveform::saw
               && settings.oscillators[0].envelope == 0,
           "the engine's settings come from the parameters");
    setParam (p, ParamID::env1On, 0.0f);
    setParam (p, ParamID::osc1On, 0.0f);

    // The effects: off and out of the chain to start with, like the bench.
    for (const auto* id : { ParamID::delayOn, ParamID::delaySync, ParamID::delayTime, ParamID::delayNote, ParamID::delayFeedback,
                            ParamID::delayTone, ParamID::delayMix, ParamID::delayPingPong, ParamID::reverbOn, ParamID::reverbSize,
                            ParamID::reverbDamping, ParamID::reverbWidth, ParamID::reverbMix, ParamID::driveInChain,
                            ParamID::delayInChain, ParamID::reverbInChain })
        check (p.getState().getParameter (id) != nullptr, std::string ("has the '") + id + "' parameter");
    check (p.getState().getParameter (ParamID::effectsOrder) == nullptr, "the order is where the cards sit, not a parameter");
    check (param (p, ParamID::delayOn) < 0.5f && param (p, ParamID::reverbOn) < 0.5f, "both effects start off");
    check (Rack::Model (p.getState()).isEmpty(), "and none is in the chain");
    for (const auto kind : Rack::allEffects)
        check (! p.getState().getParameter (Rack::inChainParameter (kind))->isAutomatable(), "being in the chain isn't automated");
    auto* note = p.getState().getParameter (ParamID::delayNote);
    auto* time = p.getState().getParameter (ParamID::delayTime);
    check (note->getCurrentValueAsText() == "1/8D" && time->getCurrentValueAsText() == "375 ms",
           "the delay starts on a dotted eighth, 375 ms");

    setParam (p, ParamID::osc1On, 1.0f);
    setParam (p, ParamID::delayOn, 1.0f);
    setParam (p, ParamID::delayTime, 500.0f);
    setParam (p, ParamID::delayFeedback, 0.5f);
    check (! p.currentSettings().effects.delay.on && approx ((float) p.getTailLengthSeconds(), Synth2Engine::gateReleaseSeconds, 1e-4f),
           "switched on but out of the chain, the delay plays nothing");
    setParam (p, ParamID::delayInChain, 1.0f);
    setParam (p, ParamID::reverbInChain, 1.0f);
    check (p.currentSettings().effects.delay.on, "in the chain and switched on, it plays");
    const double delayTail = 0.5 * (1.0 + std::log (0.001) / std::log (0.5));
    check (approx ((float) p.getTailLengthSeconds(), (float) (Synth2Engine::gateReleaseSeconds + delayTail), 0.01f),
           "the delay's repeats add to the tail");
    setParam (p, ParamID::reverbOn, 1.0f);
    check (p.getTailLengthSeconds() > Synth2Engine::gateReleaseSeconds + delayTail + 1.0, "and so does the reverb's ring");
    check (p.currentSettings().effects.order == Effects::Order { Rack::Kind::delay, Rack::Kind::reverb, Rack::Kind::drive },
           "the delay, then the reverb, as their cards sit (the drive, out of the chain, last)");
    Rack::Model rack (p.getState());
    rack.move (Rack::Kind::reverb, { 0.0f, Rack::defaultPosition (0).y });
    check (p.currentSettings().effects.order == Effects::Order { Rack::Kind::reverb, Rack::Kind::delay, Rack::Kind::drive }
               && approx (p.currentSettings().effects.delay.feedback, 0.5f, 1e-4f),
           "the engine gets the effects' settings, and the order the cards read in");
    rack.move (Rack::Kind::reverb, Rack::defaultPosition (Rack::Kind::reverb));

    // A synced delay follows the host's tempo.
    struct Tempo final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (100.0);
            return info;
        }
    } tempo;
    p.prepareToPlay (48000.0, 512);
    p.setPlayHead (&tempo);
    playAndMeasure (p, {}, 1);
    p.setPlayHead (nullptr);
    check (approx ((float) p.currentSettings().effects.bpm, 100.0f, 1e-4f), "the host's tempo reaches the delay");
    for (const auto* id : { ParamID::delayOn, ParamID::reverbOn, ParamID::delayInChain, ParamID::reverbInChain, ParamID::osc1On })
        setParam (p, id, 0.0f);
    setParam (p, ParamID::delayTime, 375.0f);
    setParam (p, ParamID::delayFeedback, 0.35f);

    auto* detune = p.getState().getParameter (ParamID::osc1Detune);
    check (detune->getText (detune->convertTo0to1 (12.5f), 32) == "12.5 ct", "DETUNE reads in cents");
    auto* attack = p.getState().getParameter (ParamID::env1Attack);
    check (attack->getText (attack->convertTo0to1 (0.25f), 32) == "250 ms"
               && attack->getText (attack->convertTo0to1 (1.5f), 32) == "1.50 s",
           "times read in ms below a second, s above");

    // The bench starts empty: no oscillator, no envelope. Each oscillator has
    // its own sound ready for when it's added: OSC 1 at the note at full
    // level, OSC 2 a square an octave down.
    bool empty = true;
    for (const auto& osc : ParamID::oscillators)
        empty = empty && param (p, osc.on) < 0.5f;
    for (const auto& env : ParamID::envelopes)
        empty = empty && param (p, env.on) < 0.5f;
    check (empty, "the bench starts empty");
    check (approx (param (p, ParamID::signalFlow), (float) Synth2AudioProcessor::SignalFlow::fixed, 1e-6f), "with sound in a fixed order");
    check (! p.getState().getParameter (ParamID::osc1On)->isAutomatable() && ! p.getState().getParameter (ParamID::signalFlow)->isAutomatable(),
           "what is on the bench, and the flow, aren't automatable");
    check (approx (param (p, ParamID::osc1Pitch), 0.0f, 1e-6f) && approx (param (p, ParamID::osc1Fine), 0.0f, 1e-6f)
               && approx (param (p, ParamID::osc1Level), 1.0f, 1e-6f),
           "OSC 1 is set to the note, full level");
    check (approx (param (p, ParamID::osc2Wave), (float) Waveform::square, 1e-6f) && approx (param (p, ParamID::osc2Pitch), -12.0f, 1e-6f),
           "set up as a square an octave down");

    auto* pitch = p.getState().getParameter (ParamID::osc2Pitch);
    auto* fine  = p.getState().getParameter (ParamID::osc1Fine);
    check (pitch->getText (pitch->convertTo0to1 (7.0f), 32) == "+7 st" && pitch->getText (pitch->convertTo0to1 (-12.0f), 32) == "-12 st",
           "PITCH reads in signed semitones");
    check (fine->getText (fine->convertTo0to1 (3.5f), 32) == "+3.5 ct", "FINE reads in signed cents");

    setParam (p, ParamID::osc2On, 1.0f);
    setParam (p, ParamID::osc2Pitch, 7.0f);
    setParam (p, ParamID::osc1Level, 0.5f);
    const auto oscillators = p.currentSettings();
    check (oscillators.oscillators[1].on && oscillators.oscillators[1].semitones == 7 && approx (oscillators.oscillators[0].level, 0.5f, 1e-6f),
           "the oscillators' settings reach the engine");

    setParam (p, ParamID::osc4On, 1.0f);
    setParam (p, ParamID::osc4Wave, (float) Waveform::sine);
    setParam (p, ParamID::osc4Voices, 5.0f);
    setParam (p, ParamID::osc4Width, 0.25f);
    setParam (p, ParamID::env4Attack, 0.5f);
    setParam (p, ParamID::env4Delay, 0.1f);
    setParam (p, ParamID::env4Hold, 0.2f);
    setParam (p, ParamID::env4Decay, 0.7f);
    setParam (p, ParamID::env4Sustain, 0.4f);
    const auto modules = p.currentSettings();
    check (modules.oscillators[3].on && modules.oscillators[3].wave == Waveform::sine && modules.oscillators[3].voices == 5
               && approx (modules.oscillators[3].width, 0.25f, 1e-6f) && modules.oscillators[0].voices == 1
               && approx (modules.envelopes[3].attackSeconds, 0.5f, 0.001f) && approx (modules.envelopes[3].delaySeconds, 0.1f, 0.001f)
               && approx (modules.envelopes[3].holdSeconds, 0.2f, 0.001f) && approx (modules.envelopes[3].decaySeconds, 0.7f, 0.001f)
               && approx (modules.envelopes[3].sustainLevel, 0.4f, 1e-4f),
           "OSC 4's own unison stack and all of ENV 4's stages reach the engine");

    // Which envelope shapes which oscillator follows the flow: by number...
    Bench::Model bench (p.getState());
    check (p.currentSettings().oscillators[3].envelope == -1, "with no envelope on the bench, OSC 4 has none");
    bench.add (Bench::Kind::envelope, { 0.0f, 400.0f });   // ENV 1, under OSC 1's column
    check (p.currentSettings().oscillators[0].envelope == -1, "nor OSC 1 while it is off the bench");
    bench.remove ({ Bench::Kind::envelope, 0 });
    setParam (p, ParamID::osc1On, 1.0f);
    bench.add (Bench::Kind::envelope, { 0.0f, 400.0f });   // ENV 1, under OSC 1's column
    check (p.currentSettings().oscillators[0].envelope == 0 && p.currentSettings().oscillators[3].envelope == -1,
           "in a fixed order, ENV 1 shapes OSC 1, not OSC 4");
    setParam (p, ParamID::osc1On, 0.0f);

    // ...by the layout: each envelope the first free oscillator before it...
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::layout);
    check (p.currentSettings().oscillators[1].envelope == 0, "following the layout, ENV 1 below the oscillators shapes the first, OSC 2");
    bench.move ({ Bench::Kind::oscillator, 3 }, { 0.0f, 0.0f });
    bench.move ({ Bench::Kind::oscillator, 1 }, { 504.0f, 0.0f });
    check (p.currentSettings().oscillators[3].envelope == 0 && p.currentSettings().oscillators[1].envelope == -1,
           "moving OSC 4 first hands ENV 1 to it");

    // ...or along the cables, where only what reaches the output is heard.
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    const Bench::ModuleId osc2 { Bench::Kind::oscillator, 1 }, osc4 { Bench::Kind::oscillator, 3 }, env1 { Bench::Kind::envelope, 0 };
    check (! p.currentSettings().oscillators[1].routed && p.currentSettings().oscillators[3].envelope == -1,
           "with patch cables, an unpatched oscillator isn't routed or shaped");
    check (bench.connect (osc2) && p.currentSettings().oscillators[1].routed && ! p.currentSettings().oscillators[3].routed,
           "patching OSC 2 to the output routes it");
    check (bench.connect (osc4, env1) && ! p.currentSettings().oscillators[3].routed, "OSC 4 into ENV 1 isn't heard yet");
    check (bench.connect (env1) && p.currentSettings().oscillators[3].routed && p.currentSettings().oscillators[3].envelope == 0,
           "until ENV 1 goes to the output: then OSC 4 is heard, shaped by ENV 1");
    check (bench.connect (osc2, env1) && p.currentSettings().oscillators[1].envelope == 0 && p.currentSettings().oscillators[3].envelope == -1
               && ! p.currentSettings().oscillators[3].routed,
           "an envelope takes one oscillator: patching OSC 2 in unplugs OSC 4");
    check (! bench.connect ({ Bench::Kind::oscillator, 2 }) && ! bench.connect (env1, Bench::ModuleId { Bench::Kind::envelope, 0 })
               && ! bench.connect (osc4, Bench::ModuleId { Bench::Kind::envelope, 2 }),
           "nothing patches from or to a module that isn't on the bench, nor an envelope into an envelope");
    bench.remove (env1);
    check (! bench.destinationOf (osc2).has_value() && bench.cables().empty(), "taking ENV 1 off unplugs its cables, in and out");
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::fixed);
    check (p.currentSettings().oscillators[3].routed, "in a fixed order, every oscillator is heard");
}

void testBusLayouts()
{
    std::printf ("Bus layouts:\n");

    Synth2AudioProcessor p;
    check (p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2, "an instrument: no input, stereo out");

    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    check (p.checkBusesLayoutSupported (layout), "stereo out is supported");

    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    check (p.checkBusesLayoutSupported (layout), "mono out is supported");

    layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    check (! p.checkBusesLayoutSupported (layout), "surround is not");
}

// ---- MIDI in, sound out --------------------------------------------------------
void testPlaying()
{
    std::printf ("Playing:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);

    juce::MidiBuffer first;
    first.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    check (playAndMeasure (p, first, 8) == 0.0f, "an empty bench makes no sound");
    juce::MidiBuffer stop;
    stop.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    playAndMeasure (p, stop, 4);

    fillBench (p);
    check (playAndMeasure (p, {}, 4) == 0.0f, "silent with no notes");

    juce::MidiBuffer chord;
    for (const int note : { 60, 64, 67 })
        chord.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
    check (playAndMeasure (p, chord, 8) > 0.05f, "a chord sounds");
    check (p.getNumActiveNotes() == 3, "three notes are playing");

    juce::MidiBuffer release;
    for (const int note : { 60, 64, 67 })
        release.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    playAndMeasure (p, release, 100);   // ~1 s, past the 300 ms release
    check (p.getNumActiveNotes() == 0, "and stops after the release");

    // The on-screen keyboard plays through the processor too.
    p.getKeyboardState().noteOn (1, 69, 0.8f);
    check (playAndMeasure (p, {}, 8) > 0.05f, "a key held on the on-screen keyboard sounds");
    p.getKeyboardState().noteOff (1, 69, 0.0f);
    playAndMeasure (p, {}, 100);
    check (p.getNumActiveNotes() == 0, "and stops when it is let go");

    // More voices: the stack stays at about the same level.
    juce::MidiBuffer note;
    note.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
    const float oneVoice = playAndMeasure (p, note, 20);
    setParam (p, ParamID::osc1Voices, 9.0f);
    const float nineVoices = playAndMeasure (p, {}, 20);
    check (nineVoices > 0.5f * oneVoice && nineVoices < 2.0f * oneVoice,
           "turning VOICES up mid-note keeps the level steady");

    // The host's MIDI doesn't leak out of an instrument.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 10);
    p.processBlock (buffer, midi);
    check (midi.isEmpty(), "no MIDI comes back out");

    // A mono bus gets sound too.
    Synth2AudioProcessor mono;
    fillBench (mono);
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    mono.setBusesLayout (layout);
    mono.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> monoBuffer (1, 512);
    juce::MidiBuffer monoMidi;
    monoMidi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    for (int b = 0; b < 8; ++b)
    {
        mono.processBlock (monoBuffer, monoMidi);
        monoMidi.clear();
    }
    check (monoBuffer.getRMSLevel (0, 0, 512) > 0.05f, "a mono bus plays");
}

// ---- State ----------------------------------------------------------------------
void testStateRoundTrip()
{
    std::printf ("State round trip:\n");

    Synth2AudioProcessor a;
    setParam (a, ParamID::osc1Wave, (float) Waveform::square);
    setParam (a, ParamID::osc1Voices, 7.0f);
    setParam (a, ParamID::osc1Detune, 33.0f);
    setParam (a, ParamID::osc1Width, 0.8f);
    setParam (a, ParamID::env1Attack, 0.5f);
    setParam (a, ParamID::env1Release, 3.0f);
    setParam (a, ParamID::output, -4.0f);

    juce::MemoryBlock saved;
    a.getStateInformation (saved);

    Synth2AudioProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    check (approx (param (b, ParamID::osc1Wave), (float) Waveform::square, 1e-6f), "WAVE comes back");
    check (approx (param (b, ParamID::osc1Voices), 7.0f, 1e-6f), "VOICES comes back");
    check (approx (param (b, ParamID::osc1Detune), 33.0f, 0.01f), "DETUNE comes back");
    check (approx (param (b, ParamID::osc1Width), 0.8f, 0.001f), "WIDTH comes back");
    check (approx (param (b, ParamID::env1Attack), 0.5f, 0.001f), "ATTACK comes back");
    check (approx (param (b, ParamID::env1Release), 3.0f, 0.001f), "RELEASE comes back");
    check (approx (param (b, ParamID::output), -4.0f, 0.01f), "OUTPUT comes back");

    // The bench comes back as it was left: what is on it, where, and the cables.
    const Bench::ModuleId osc1 { Bench::Kind::oscillator, 0 }, osc2 { Bench::Kind::oscillator, 1 },
                          env1 { Bench::Kind::envelope, 0 },   env2 { Bench::Kind::envelope, 1 };
    Bench::Model benchA (a.getState());
    benchA.add (Bench::Kind::oscillator, { 300.0f, 20.0f });
    benchA.add (Bench::Kind::oscillator, { 600.0f, 260.0f });
    benchA.add (Bench::Kind::envelope, { 0.0f, 0.0f });
    benchA.connect (osc1);
    benchA.connect (osc2, env1);
    benchA.connect (env1);
    setParam (a, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    a.getStateInformation (saved);
    Synth2AudioProcessor c;
    c.setStateInformation (saved.getData(), (int) saved.getSize());
    const Bench::Model benchC (c.getState());
    check (benchC.isPresent (osc2) && benchC.isPresent (env1) && benchC.present().size() == 3, "the modules on the bench come back");
    check (benchC.positionOf (osc1) == juce::Point<float> (300.0f, 20.0f) && benchC.positionOf (osc2) == juce::Point<float> (600.0f, 260.0f),
           "where they were");
    check (benchC.isPatchedToOutput (osc1) && benchC.inputOf (env1) == std::optional (osc2) && benchC.isPatchedToOutput (env1),
           "with their cables");
    check (c.currentSettings().oscillators[1].routed && c.currentSettings().oscillators[1].envelope == 0
               && c.currentSettings().oscillators[0].envelope == -1,
           "which the sound follows");

    Synth2AudioProcessor fresh;
    fresh.getStateInformation (saved);
    Synth2AudioProcessor d;
    d.setStateInformation (saved.getData(), (int) saved.getSize());
    check (Bench::Model (d.getState()).isEmpty(), "an empty bench comes back empty");

    // Older sessions, made from this one: without the bench's tree and the
    // parameters that came after them, and with any they had that are gone.
    const auto olderSession = [&a] (const juce::ValueTree& bench, std::initializer_list<std::pair<const char*, float>> values)
    {
        auto older = a.getState().copyState().createCopy();
        older.removeChild (older.getChildWithName (Bench::benchType), nullptr);
        std::vector<juce::String> newer { ParamID::osc1On, ParamID::signalFlow };
        for (const auto& osc : { ParamID::oscillator (1), ParamID::oscillator (2), ParamID::oscillator (3) })
            newer.insert (newer.end(), { osc.voices, osc.detune, osc.width });
        for (const auto& env : ParamID::envelopes)
            newer.insert (newer.end(), { env.on, env.attack, env.release });
        for (const auto& id : newer)
            if (id != ParamID::env1Attack && id != ParamID::env1Release)
                older.removeChild (older.getChildWithProperty ("id", id), nullptr);
        for (const auto& [id, value] : values)
        {
            auto parameter = older.getChildWithProperty ("id", id);
            if (! parameter.isValid())
            {
                parameter = juce::ValueTree ("PARAM");
                parameter.setProperty ("id", id, nullptr);
                older.appendChild (parameter, nullptr);
            }
            parameter.setProperty ("value", value, nullptr);
        }
        if (bench.isValid())
            older.appendChild (bench.createCopy(), nullptr);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*older.createXml(), block);
        auto restored = std::make_unique<Synth2AudioProcessor>();
        restored->setStateInformation (block.getData(), (int) block.getSize());
        return restored;
    };

    // One from before the bench opens as it looked then: OSC 1 (and OSC 2,
    // which was on) along the top, each under the envelope and with the
    // unison stack they shared.
    const auto e = olderSession ({}, { { ParamID::osc2On, 1.0f } });
    const Bench::Model benchE (e->getState());
    check (benchE.isPresent (osc1) && benchE.isPresent (osc2) && benchE.isPresent (env1) && benchE.isPresent (env2)
               && benchE.present().size() == 4,
           "a session from before the bench gets OSC 1, OSC 2 (it was on), and an envelope for each");
    check (benchE.positionOf (osc2) == Bench::defaultPosition (osc2) && benchE.positionOf (env2) == Bench::defaultPosition (env2),
           "each envelope under its oscillator");
    check (approx (param (*e, ParamID::osc2Voices), 7.0f, 1e-6f) && approx (param (*e, ParamID::osc2Detune), 33.0f, 0.01f)
               && approx (param (*e, ParamID::env2Attack), 0.5f, 0.001f) && approx (param (*e, ParamID::env2Release), 3.0f, 0.001f),
           "OSC 2 keeping the unison stack, and ENV 2 the envelope, it shared with OSC 1");
    check (e->currentSettings().oscillators[1].envelope == 1 && approx (param (*e, ParamID::signalFlow), 0.0f, 1e-6f),
           "in a fixed order, so it sounds as it did");

    // One saved with the switches but no layout keeps them as saved.
    const auto f = olderSession ({}, { { ParamID::osc1On, 1.0f }, { ParamID::osc2On, 0.0f }, { ParamID::unisonOn, 0.0f }, { ParamID::envelopeOn, 0.0f } });
    const Bench::Model benchF (f->getState());
    check (benchF.isPresent (osc1) && ! benchF.isPresent (env1) && benchF.present().size() == 1,
           "one that saved its modules' switches, but no layout, keeps the switches as saved");
    check (approx (param (*f, ParamID::osc1Voices), 1.0f, 1e-6f), "with unison off, each oscillator plays one voice");

    // One from the first bench: its unison module goes into the oscillators,
    // its envelope becomes ENV 1 and a copy for OSC 2, and the cables run
    // through them.
    juce::ValueTree firstBench (Bench::benchType);
    firstBench.setProperty (Bench::versionProperty, 1, nullptr);
    for (const auto& [id, x, y] : { std::tuple ("osc:0", 0.0f, 0.0f), std::tuple ("osc:1", 252.0f, 0.0f),
                                    std::tuple ("unison", 0.0f, 256.0f), std::tuple ("envelope", 252.0f, 256.0f) })
    {
        juce::ValueTree module (Bench::moduleType);
        module.setProperty (Bench::idProperty, id, nullptr);
        module.setProperty (Bench::xProperty, x, nullptr);
        module.setProperty (Bench::yProperty, y, nullptr);
        firstBench.appendChild (module, nullptr);
    }
    juce::ValueTree cable (Bench::cableType);
    cable.setProperty (Bench::fromProperty, "osc:0", nullptr);
    cable.setProperty (Bench::toProperty, "out", nullptr);
    firstBench.appendChild (cable, nullptr);
    const auto g = olderSession (firstBench, { { ParamID::osc1On, 1.0f }, { ParamID::osc2On, 1.0f },
                                               { ParamID::unisonOn, 1.0f }, { ParamID::envelopeOn, 1.0f } });
    const Bench::Model benchG (g->getState());
    check (benchG.isPresent (env1) && benchG.isPresent (env2) && benchG.present().size() == 4,
           "a session from the first bench gets an envelope for each oscillator");
    bool apart = true;
    for (const auto& one : benchG.present())
        for (const auto& other : benchG.present())
            apart = apart && (one == other || ! benchG.boundsOf (one).intersects (benchG.boundsOf (other)));
    check (apart && benchG.positionOf (osc1) == juce::Point<float>(), "moved off one another, now oscillators are taller");
    check (benchG.inputOf (env1) == std::optional (osc1) && benchG.isPatchedToOutput (env1) && ! benchG.isPatchedToOutput (osc1),
           "OSC 1's cable to the output now runs through ENV 1");
    check (approx (param (*g, ParamID::osc2Voices), 7.0f, 1e-6f), "and OSC 2 plays the unison stack it shared");
    check ((int) g->getState().state.getChildWithName (Bench::benchType)[Bench::versionProperty] == Bench::currentVersion,
           "brought up to the current bench");

    // Junk is ignored rather than resetting the sound.
    const char junk[] = "not a synth state";
    b.setStateInformation (junk, (int) sizeof (junk));
    check (approx (param (b, ParamID::osc1Voices), 7.0f, 1e-6f), "a bad state is ignored");
}

// ---- The editor ------------------------------------------------------------------

// With SYNTH2_SNAPSHOTS set to a folder, saves `image` there as `name`.png, to
// look the faces over by eye.
void snapshot (const juce::Image& image, const char* name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable ("SYNTH2_SNAPSHOTS", {});
    if (folder.isEmpty())
        return;
    const auto file = juce::File (folder).getChildFile (juce::String (name) + ".png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
}

// A mouse event on `target` at `at`, for a press that began at `downAt`.
juce::MouseEvent mouseAt (juce::Component& target, juce::Point<float> at, std::optional<juce::Point<float>> downAt = {})
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &target, &target, juce::Time::getCurrentTime(), downAt.value_or (at), juce::Time::getCurrentTime(), 1,
                             downAt.has_value());
}

// Drags `module` by its header so its top left (on the bench) moves by `by`.
void dragHeader (BenchModule& module, juce::Point<float> by)
{
    const auto grab = module.headerBounds().getCentre().translated (-40.0f, 0.0f);
    module.mouseDown (mouseAt (module, grab));
    module.mouseDrag (mouseAt (module, grab + by * 0.5f, grab));
    module.mouseDrag (mouseAt (module, grab + by, grab));
    module.mouseUp (mouseAt (module, grab + by, grab));
}

// Holds middle C for a while and returns how loud the last block was.
float holdAndMeasure (Synth2AudioProcessor& p)
{
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    return playAndMeasure (p, on, 8);
}

void releaseAll (Synth2AudioProcessor& p)
{
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    playAndMeasure (p, off, 40);
}

// Whether two modules on the bench cover each other.
bool anyOverlap (BenchView& bench)
{
    std::vector<juce::Rectangle<int>> shown;
    for (const auto& id : Bench::allModules)
        if (bench.getModule (id).isVisible())
            shown.push_back (bench.getModule (id).getBounds());
    for (size_t a = 0; a < shown.size(); ++a)
        for (size_t b = a + 1; b < shown.size(); ++b)
            if (shown[a].intersects (shown[b]))
                return true;
    return false;
}

void testEditor()
{
    std::printf ("Editor:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    check (editor != nullptr, "creates the Synth2 editor");
    if (editor == nullptr)
        return;

    check (editor->getWidth() == 1040 && editor->getHeight() == 916, "opens at its 1040 x 916 design size");
    check (editor->isResizable(), "is resizable");

    // An empty bench, inviting a click, and the keyboard and OUTPUT below it.
    auto& bench = editor->getBench();
    bool noneShown = true;
    for (const auto& id : Bench::allModules)
        noneShown = noneShown && ! bench.getModule (id).isVisible();
    check (noneShown && bench.isShowingHint(), "opens on an empty bench, with a hint to click it");
    check (editor->getLocalBounds().contains (bench.getBounds()) && bench.getWidth() == 992 && bench.getHeight() == 688,
           "the bench fills the middle of the panel");
    check (editor->getLocalBounds().contains (editor->getKeyboard().getBounds()) && editor->getKeyboard().getY() > bench.getBottom()
               && editor->getKeyboard().getHeight() > 80,
           "the keyboard runs along the bottom");
    check (editor->getOutputDial().getX() > editor->getKeyboard().getRight() && editor->getOutputDial().getWidth() >= 40,
           "with OUTPUT at its right");
    const auto empty = render (*editor);
    check (hasDetail (empty), "renders its face");
    snapshot (empty, "1-empty");

    // A click on the bench offers what can go on it, there.
    auto& tray = editor->getTrays().getAdd();
    const auto clickBench = [&] (juce::Point<float> at)
    {
        bench.mouseDown (mouseAt (bench, at));
        bench.mouseUp (mouseAt (bench, at, at));
    };
    const auto addAt = [&] (int choice, juce::Point<float> at)
    {
        clickBench (at);
        tray.mouseDown (mouseAt (tray, tray.choiceBounds (choice).getCentre()));
    };

    clickBench ({ 150.0f, 40.0f });
    check (tray.isOpen() && tray.getCaption() == "ADD TO THE BENCH" && tray.getNumChoices() == 2, "a click on the bench opens the add tray");
    check (tray.getChoice (0).name == "Oscillator" && tray.getChoice (0).detail.startsWith ("4 of 4 left")
               && tray.getChoice (1).name == "Envelope" && tray.getChoice (1).detail.startsWith ("4 of 4 left"),
           "offering an oscillator and an envelope, four of each");
    check (tray.getChoice (0).enabled && tray.getChoice (1).enabled, "both free");
    check (editor->getLocalBounds().toFloat().contains (tray.trayBounds())
               && tray.trayBounds().getPosition().getDistanceFrom ((juce::Point<float> (150.0f, 40.0f) + bench.getPosition().toFloat())) < 1.0f,
           "opening where the click was");
    check (pumpUntil ([&tray] { return tray.getAlpha() >= 1.0f; }), "fading in");
    snapshot (render (*editor), "2-add-tray");

    tray.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (! tray.isOpen() && bench.isShowingHint(), "Escape closes it, adding nothing");

    addAt (0, { 150.0f, 40.0f });
    auto& osc1 = bench.getOscillator (0);
    Bench::Model model (p.getState());
    check (! tray.isOpen() && param (p, ParamID::osc1On) >= 0.5f, "picking Oscillator puts OSC 1 on the bench");
    check (pumpUntil ([&osc1] { return osc1.isVisible() && osc1.getAlpha() >= 1.0f; }), "which fades in");
    check (osc1.getCaption() == "OSC 1" && model.positionOf (osc1.getId()) == juce::Point<float> (32.0f, 25.0f)
               && osc1.getBounds() == juce::Rectangle<int> (32, 25, 236, 336),
           "under the click, its header where the click was");
    check (osc1.getNumDials() == 6, "with its own unison stack under PITCH / FINE / LEVEL");
    check (! bench.isShowingHint(), "and the hint goes");
    check (holdAndMeasure (p) > 0.02f, "a note now sounds, on and off with the key without an envelope");
    releaseAll (p);

    // Dragging a module by its header moves it, and the bench remembers.
    dragHeader (osc1, { 220.0f, -25.0f });
    check (model.positionOf (osc1.getId()) == juce::Point<float> (252.0f, 0.0f) && osc1.getPosition() == juce::Point<int> (252, 0),
           "dragging OSC 1's header moves it, and the bench keeps the spot");
    dragHeader (osc1, { 2000.0f, 0.0f });
    check (osc1.getRight() <= bench.getWidth() && model.positionOf (osc1.getId()).x <= Bench::viewWidth - Bench::moduleWidth,
           "a module can't be dragged off the bench");
    dragHeader (osc1, { -(float) osc1.getX() + 252.0f, 0.0f });

    // A module dropped on another slides off it to the nearest free spot.
    addAt (0, { 900.0f, 40.0f });
    auto& osc2 = bench.getOscillator (1);
    check (param (p, ParamID::osc2On) >= 0.5f && pumpUntil ([&osc2] { return osc2.isVisible(); }), "a second oscillator is OSC 2");
    check (juce::exactlyEqual (model.positionOf (osc2.getId()).x, Bench::viewWidth - Bench::moduleWidth), "kept inside the bench's box");
    dragHeader (osc2, juce::Point<float> (300.0f, 30.0f) - osc2.getPosition().toFloat());
    const auto landed = model.positionOf (osc2.getId());
    check (! model.boundsOf (osc2.getId()).intersects (model.boundsOf (osc1.getId()))
               && landed != juce::Point<float> (300.0f, 30.0f) && landed.getDistanceFrom ({ 300.0f, 30.0f }) < 200.0f,
           "dropped on OSC 1, OSC 2 slides off to the nearest free spot");
    check (pumpUntil ([&] { return osc2.getBounds() == bench.boundsFor (osc2.getId()); }) && ! anyOverlap (bench), "and glides there");

    // The cross takes a module off the bench.
    osc2.mouseDown (mouseAt (osc2, osc2.closeBounds().getCentre()));
    check (param (p, ParamID::osc2On) < 0.5f && pumpUntil ([&osc2] { return ! osc2.isVisible(); }), "the cross takes OSC 2 off the bench");

    // Fill the bench: the oscillators along the top, OSC 1 where it was, and
    // the envelopes under them.
    addAt (0, { 118.0f, 15.0f });     // OSC 2, back again, at (0, 0)
    addAt (0, { 622.0f, 15.0f });     // OSC 3 at (504, 0)
    addAt (0, { 874.0f, 15.0f });     // OSC 4 at (756, 0)
    clickBench ({ 118.0f, 367.0f });
    check (! tray.getChoice (0).enabled && tray.getChoice (0).detail == "All four are on the bench", "a fifth oscillator is greyed out");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (0).getCentre()));
    check (tray.isOpen(), "and picking it does nothing");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (1).getCentre()));   // ENV 1 at (0, 352)
    auto& env1 = bench.getEnvelope (0);
    check (pumpUntil ([&env1] { return env1.isVisible() && env1.getAlpha() >= 1.0f; }) && env1.getCaption() == "ENV 1"
               && env1.getBounds() == juce::Rectangle<int> (0, 352, 236, 336),
           "picking Envelope puts ENV 1 on the bench");
    addAt (1, { 370.0f, 367.0f });    // ENV 2 at (252, 352)
    addAt (1, { 622.0f, 367.0f });    // ENV 3 at (504, 352)
    addAt (1, { 874.0f, 367.0f });    // ENV 4 at (756, 352)
    const auto allShown = [&bench]
    {
        return std::all_of (Bench::allModules.begin(), Bench::allModules.end(),
                            [&bench] (const auto& id) { return bench.getModule (id).isVisible() && bench.getModule (id).getAlpha() >= 1.0f; });
    };
    check (pumpUntil (allShown) && ! anyOverlap (bench), "eight modules, none covering another");
    check (model.positionOf (bench.getOscillator (1).getId()) == juce::Point<float> (0.0f, 0.0f)
               && model.positionOf (bench.getEnvelope (3).getId()) == juce::Point<float> (756.0f, 352.0f),
           "each where it was put");
    clickBench ({ 500.0f, 650.0f });
    check (! tray.getChoice (0).enabled && ! tray.getChoice (1).enabled, "now the tray has nothing left to add");
    tray.close();

    // In a fixed order, ENV n shapes OSC n, wherever they sit, and says so.
    const auto tagsAre = [&bench] (std::initializer_list<const char*> expected)
    {
        int e = 0;
        return std::all_of (expected.begin(), expected.end(), [&] (const char* tag) { return bench.getEnvelope (e++).getHeader().getTag() == tag; });
    };
    check (pumpUntil ([&] { return tagsAre ({ "> OSC 1", "> OSC 2", "> OSC 3", "> OSC 4" }); }),
           "each envelope shows the oscillator with its number");
    snapshot (render (*editor), "3-full-bench");
    setParam (p, ParamID::osc3On, 0.0f);
    check (pumpUntil ([&] { return bench.getEnvelope (2).isDimmed() && bench.getEnvelope (2).getHeader().getTag().isEmpty(); })
               && ! bench.getEnvelope (1).isDimmed(),
           "without OSC 3, ENV 3 shapes nothing and is greyed out");
    setParam (p, ParamID::osc3On, 1.0f);
    check (pumpUntil ([&] { return ! bench.getEnvelope (2).isDimmed() && model.positionOf (bench.getOscillator (2).getId()) == juce::Point<float> (504.0f, 0.0f); }),
           "and OSC 3 comes back where it was");

    // Dials in the modules drive their own parameters, and the wells follow.
    auto& osc3 = bench.getOscillator (2);
    osc3.getDial (0).setValue (12.0, juce::sendNotificationSync);
    osc3.getDial (2).setValue (0.5, juce::sendNotificationSync);
    check (approx (param (p, ParamID::osc3Pitch), 12.0f, 1e-6f) && approx (param (p, ParamID::osc3Level), 0.5f, 1e-6f),
           "OSC 3's dials set its own PITCH and LEVEL");
    check (pumpUntil ([&osc3] { return approx (osc3.getDisplay().getOsc().level, 0.5f, 1e-6f) && osc3.getDisplay().getOsc().cycles > 3.9f; }),
           "and its well shows twice the cycles, half as tall");
    const float osc1VoicesBefore = param (p, ParamID::osc1Voices);
    osc3.getDial (3).setValue (5.0, juce::sendNotificationSync);
    osc3.getDial (4).setValue (40.0, juce::sendNotificationSync);
    check (approx (param (p, ParamID::osc3Voices), 5.0f, 1e-6f) && approx (param (p, ParamID::osc3Detune), 40.0f, 0.01f)
               && approx (param (p, ParamID::osc1Voices), osc1VoicesBefore, 1e-6f),
           "its VOICES and DETUNE stack OSC 3 alone");
    check (pumpUntil ([&osc3] { return osc3.getDisplay().getState().voices == 5; }), "and its well shows a copy per voice");

    // The wave picker, laid over each oscillator's WAVE well.
    auto& picker = osc1.getPicker();
    auto& well   = osc1.getDisplay();
    auto& menu   = editor->getWaveMenu (0);
    check (picker.getBounds() == well.getBounds() && osc1.getLocalBounds().contains (picker.getBounds()),
           "OSC 1's picker sits over its well");
    check (picker.getSelected() == 2, "its pill shows SAW, the default");
    picker.mouseDown (mouseAt (picker, picker.nextArrow().getCentre()));
    check (approx (param (p, ParamID::osc1Wave), (float) Waveform::square, 1e-6f) && picker.getSelected() == 3,
           "the right arrow steps SAW to SQUARE");
    picker.mouseDown (mouseAt (picker, picker.nextArrow().getCentre()));
    picker.mouseDown (mouseAt (picker, picker.previousArrow().getCentre()));
    check (picker.getSelected() == 3 && ! menu.isOpen(), "and round to SINE and back, without opening the tray");

    juce::MouseWheelDetails wheel {};
    wheel.deltaY = -0.25f;
    picker.mouseWheelMove (mouseAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 0, "scrolling over the well flips the shape");
    wheel.deltaY = 0.25f;
    picker.mouseWheelMove (mouseAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 3 && pumpUntil ([&well] { return well.getShownWave() == Waveform::square; }), "and the well follows");

    picker.mouseDown (mouseAt (picker, { 60.0f, 60.0f }));
    const auto wellInEditor = editor->getLocalArea (&well, well.getLocalBounds()).toFloat();
    bool tilesFit = true;
    for (int i = 0; i < WaveMenu::numWaves; ++i)
        tilesFit = tilesFit && menu.trayBounds().contains (menu.tileBounds (i)) && menu.tileBounds (i).getY() > wellInEditor.getBottom()
                && editor->getLocalBounds().toFloat().contains (menu.tileBounds (i));
    check (menu.isOpen() && menu.getBounds() == editor->getLocalBounds() && tilesFit, "a click on the well drops its tray from under it");
    check (pumpUntil ([&menu] { return menu.getAlpha() >= 1.0f; }), "fading in");
    snapshot (render (*editor), "4-wave-tray");
    menu.mouseMove (mouseAt (menu, menu.tileBounds (1).getCentre()));
    check (menu.getHighlighted() == 1 && well.getShownWave() == Waveform::triangle
               && approx (param (p, ParamID::osc1Wave), (float) Waveform::square, 1e-6f),
           "hovering TRIANGLE previews it in the well, changing nothing");
    menu.mouseDown (mouseAt (menu, menu.tileBounds (0).getCentre()));
    check (approx (param (p, ParamID::osc1Wave), (float) Waveform::sine, 1e-6f) && ! menu.isOpen(), "clicking SINE picks it and closes the tray");
    auto& osc4Picker = bench.getOscillator (3).getPicker();
    osc4Picker.mouseDown (mouseAt (osc4Picker, osc4Picker.nextArrow().getCentre()));
    check (approx (param (p, ParamID::osc4Wave), (float) Waveform::saw, 1e-6f) && approx (param (p, ParamID::osc1Wave), (float) Waveform::sine, 1e-6f),
           "OSC 4's arrows step its own shape (TRIANGLE to SAW), not OSC 1's");
    setParam (p, ParamID::osc1Wave, (float) Waveform::square);
    check (pumpUntil ([&picker] { return picker.getSelected() == 3; }), "a host change of WAVE shows in the pill");

    // Keyboard: the keys the host plays light up, without being played twice.
    auto& keyboard = editor->getKeyboard();
    check (keyboard.getLocalBounds().toFloat().contains (keyboard.getRectangleForKey (60).getCentre()), "middle C is on the keyboard");
    juce::MidiBuffer hostNote;
    hostNote.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
    playAndMeasure (p, hostNote, 2);
    check (pumpUntil ([&p] { return p.getKeyboardState().isNoteOn (1, 64); }), "a note the host plays lights its key");
    playAndMeasure (p, {}, 2);
    check (p.getNumActiveNotes() == 1, "and isn't played again from the keyboard");
    juce::MidiBuffer hostOff;
    hostOff.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
    playAndMeasure (p, hostOff, 2);
    check (pumpUntil ([&p] { return ! p.getKeyboardState().isNoteOn (1, 64); }), "and goes dark when the host lets go");
    releaseAll (p);

    // An envelope's well: a dot rides the envelope for each note.
    auto& amp = env1.getDisplay();
    setParam (p, ParamID::env1Attack, 0.5f);
    check (pumpUntil ([&amp] { return approx (amp.getAmpState().attackSeconds, 0.5f, 0.001f); }), "ENV 1's well shows its ATTACK");
    setParam (p, ParamID::env2Attack, 1.5f);
    check (pumpUntil ([&] { return approx (bench.getEnvelope (1).getDisplay().getAmpState().attackSeconds, 1.5f, 0.001f); })
               && approx (amp.getAmpState().attackSeconds, 0.5f, 0.001f),
           "and ENV 2's its own");
    juce::MidiBuffer chordOn;
    for (const int note : { 60, 64 })
        chordOn.addEvent (juce::MidiMessage::noteOn (2, note, 0.8f), 0);
    playAndMeasure (p, chordOn, 4);
    check (pumpUntil ([&amp] { return amp.getAmpState().numSounding() == 2; }), "two notes, two dots");
    juce::MidiBuffer chordOff;
    for (const int note : { 60, 64 })
        chordOff.addEvent (juce::MidiMessage::noteOff (2, note), 0);
    playAndMeasure (p, chordOff, 4);
    playAndMeasure (p, {}, 100);
    check (pumpUntil ([&amp] { return amp.getAmpState().numSounding() == 0; }), "which leave once the notes have faded");
    setParam (p, ParamID::env1Attack, 0.005f);
    setParam (p, ParamID::env2Attack, 0.005f);

    // An envelope's six dials, DELAY / ATTACK / HOLD over DECAY / SUSTAIN /
    // RELEASE, drive its own stages, and its well draws them.
    auto& env2 = bench.getEnvelope (1);
    check (env2.getNumDials() == 6, "an envelope has six dials");
    const auto before = render (env2);
    env2.getDial (0).setValue (0.2, juce::sendNotificationSync);
    env2.getDial (2).setValue (0.1, juce::sendNotificationSync);
    env2.getDial (4).setValue (0.3, juce::sendNotificationSync);
    check (approx (param (p, ParamID::env2Delay), 0.2f, 1e-4f) && approx (param (p, ParamID::env2Hold), 0.1f, 1e-4f)
               && approx (param (p, ParamID::env2Sustain), 0.3f, 1e-4f) && approx (param (p, ParamID::env1Sustain), 1.0f, 1e-6f),
           "ENV 2's DELAY, HOLD and SUSTAIN set its own stages");
    check (pumpUntil ([&env2] { const auto& dials = env2.getDisplay().getAmpState().dials;
                                return approx (dials.sustain, 0.3f, 1e-4f) && dials.delay > 0.0f && dials.hold > 0.0f; }),
           "and its well follows");
    check (pixelsChanged (before, render (env2)) > 50, "drawing the wait, the hold and the fall to the sustain level");
    snapshot (render (*editor), "4b-envelope-stages");
    setParam (p, ParamID::env2Delay, 0.0f);
    setParam (p, ParamID::env2Hold, 0.0f);
    setParam (p, ParamID::env2Sustain, 1.0f);

    // The gear picks how sound flows: following the layout numbers the modules,
    // and each envelope shapes the first oscillator before it without one.
    auto& settings = editor->getTrays().getSettings();
    editor->mouseDown (mouseAt (*editor, editor->gearBounds().getCentre()));
    check (settings.isOpen() && settings.getCaption() == "SIGNAL FLOW" && settings.getNumChoices() == 8
               && settings.getChoice (0).ticked && ! settings.getChoice (1).ticked,
           "the gear opens the signal-flow settings, Fixed order ticked");
    check (settings.getChoice (3).heading == "ENVELOPES" && settings.getChoice (5).name == "Both" && settings.getChoice (5).ticked
               && settings.getChoice (6).heading == "OSCILLATORS" && settings.getChoice (6).name == "Full" && settings.getChoice (6).ticked,
           "then how envelopes and oscillators show, both and full ticked");
    check (editor->getLocalBounds().toFloat().contains (settings.trayBounds()) && settings.trayBounds().getY() > editor->gearBounds().getBottom(),
           "under the gear");
    check (pumpUntil ([&settings] { return settings.getAlpha() >= 1.0f; }), "fading in");
    snapshot (render (*editor), "5-settings");
    settings.mouseDown (mouseAt (settings, settings.choiceBounds (1).getCentre()));
    check (! settings.isOpen() && approx (param (p, ParamID::signalFlow), 1.0f, 1e-6f), "picking Follow layout sets the flow");

    const auto orderIs = [&bench] (std::initializer_list<std::pair<Bench::ModuleId, int>> expected)
    {
        return std::all_of (expected.begin(), expected.end(), [&bench] (const auto& e) { return bench.getModule (e.first).getHeader().getOrder() == e.second; });
    };
    const Bench::ModuleId o1 { Bench::Kind::oscillator, 0 }, o2 { Bench::Kind::oscillator, 1 }, o3 { Bench::Kind::oscillator, 2 },
                          o4 { Bench::Kind::oscillator, 3 }, e1 { Bench::Kind::envelope, 0 },   e2 { Bench::Kind::envelope, 1 },
                          e3 { Bench::Kind::envelope, 2 },   e4 { Bench::Kind::envelope, 3 };
    check (pumpUntil ([&] { return orderIs ({ { o2, 1 }, { o1, 2 }, { o3, 3 }, { o4, 4 }, { e1, 5 }, { e2, 6 }, { e3, 7 }, { e4, 8 } }); }),
           "each module shows its place, row by row, left to right");
    check (pumpUntil ([&] { return tagsAre ({ "> OSC 2", "> OSC 1", "> OSC 3", "> OSC 4" }); }),
           "and each envelope shapes the oscillator over it (ENV 1 OSC 2, which comes first)");
    snapshot (render (*editor), "6-follow-layout");
    env1.mouseDown (mouseAt (env1, env1.closeBounds().getCentre()));
    check (pumpUntil ([&] { return orderIs ({ { o4, 4 }, { e2, 5 }, { e3, 6 }, { e4, 7 } }) && tagsAre ({ "", "> OSC 2", "> OSC 1", "> OSC 3" }); }),
           "taking ENV 1 away renumbers them, and the envelopes after it move up an oscillator");
    addAt (1, { 118.0f, 367.0f });
    check (pumpUntil ([&] { return allShown() && tagsAre ({ "> OSC 2", "> OSC 1", "> OSC 3", "> OSC 4" }); }), "putting it back restores them");

    // Patch cables: oscillators get an OUT jack, envelopes an IN and an OUT,
    // and the output an IN; only what reaches the output is heard.
    editor->mouseDown (mouseAt (*editor, editor->gearBounds().getCentre()));
    settings.mouseDown (mouseAt (settings, settings.choiceBounds (2).getCentre()));
    check (approx (param (p, ParamID::signalFlow), 2.0f, 1e-6f), "picking Patch cables sets the flow");
    using Jack = BenchModule::Jack;
    check (pumpUntil ([&] { return osc1.getHeader().isJackShown() && ! osc1.getHeader().isJackShown (Jack::in) && osc1.getHeader().getOrder() == 0; })
               && env1.getHeader().isJackShown (Jack::in) && env1.getHeader().isJackShown (Jack::out) && env1.getHeader().getTag().isEmpty(),
           "the oscillators show OUT jacks, the envelopes IN and OUT, and no numbers");
    bool allDimmed = true;
    for (const auto& id : Bench::allModules)
        allDimmed = allDimmed && bench.getModule (id).isDimmed();
    check (allDimmed, "with nothing patched, everything is greyed out");
    check (holdAndMeasure (p) < 1e-4f, "and silent");
    const auto unpatched = render (*editor);

    // Drags a cable from `module`'s `jack` and lets go at `to` (in the editor).
    const auto drag = [&editor] (BenchModule& module, Jack jack, juce::Point<float> to)
    {
        const auto from = module.jackBounds (jack).getCentre();
        module.mouseDown (mouseAt (module, from));
        module.mouseUp (mouseAt (module, module.getLocalPoint (editor, to), from));
    };
    const auto outputIn = editor->getCables().outputJackBounds().getCentre();

    const auto jack = osc1.jackBounds().getCentre();
    osc1.mouseDown (mouseAt (osc1, jack));
    osc1.mouseDrag (mouseAt (osc1, jack + juce::Point<float> (-80.0f, 300.0f), jack));
    check (editor->getCables().isPatching(), "dragging from OSC 1's OUT jack pulls a cable");
    snapshot (render (*editor), "7-patching");
    osc1.mouseUp (mouseAt (osc1, osc1.getLocalPoint (editor, editor->getCables().jackCentre (e1, Jack::in)), jack));
    check (! editor->getCables().isPatching() && model.inputOf (e1) == std::optional (o1), "dropped on ENV 1's IN jack, it's patched");
    check (osc1.isDimmed() && holdAndMeasure (p) < 1e-4f, "but not heard until ENV 1 reaches the output");
    releaseAll (p);
    drag (env1, Jack::out, outputIn);
    check (model.isPatchedToOutput (e1) && ! osc1.isDimmed() && ! env1.isDimmed() && osc2.isDimmed(),
           "ENV 1's OUT to the output's IN: OSC 1 and ENV 1 light up, the others stay grey");
    check (holdAndMeasure (p) > 0.02f, "and OSC 1 is heard");
    releaseAll (p);
    check (pumpUntil ([&] { return pixelsChanged (unpatched, render (*editor)) > 200; }), "the cables hang between the jacks");

    // ENV 1 shapes only OSC 1: a slow attack on it leaves OSC 2, patched
    // straight to the output, sounding at once.
    setParam (p, ParamID::env1Attack, 3.0f);
    check (holdAndMeasure (p) < 0.01f, "a slow ATTACK on ENV 1 fades OSC 1 in");
    releaseAll (p);
    drag (osc2, Jack::out, outputIn);
    check (model.isPatchedToOutput (o2) && holdAndMeasure (p) > 0.02f, "while OSC 2, straight to the output, sounds at once");
    releaseAll (p);
    setParam (p, ParamID::env1Attack, 0.005f);
    snapshot (render (*editor), "8-cables");

    // Picking a cable up: by its oscillator, an envelope's IN or the output's IN.
    drag (osc1, Jack::out, { 400.0f, 860.0f });
    check (! model.inputOf (e1).has_value() && osc1.isDimmed() && env1.isDimmed(), "a cable dropped anywhere else is unplugged");
    drag (osc1, Jack::out, editor->getCables().jackCentre (e1, Jack::in));
    const auto envIn = env1.jackBounds (Jack::in).getCentre();
    env1.mouseDown (mouseAt (env1, envIn));
    check (editor->getCables().isPatching() && ! model.inputOf (e1).has_value(), "grabbing ENV 1's IN jack picks up the cable into it");
    env1.mouseUp (mouseAt (env1, envIn, envIn));
    check (model.inputOf (e1) == std::optional (o1), "and dropping it back plugs it in again");
    check (! model.connect (e1, e2) && model.inputOf (e2) == std::nullopt, "an envelope can't be patched into another");

    editor->mouseDown (mouseAt (*editor, outputIn));
    check (editor->getCables().isPatching() && ! model.isPatchedToOutput (o2) && model.isPatchedToOutput (e1),
           "grabbing the output's IN jack picks up the last cable patched to it");
    editor->mouseUp (mouseAt (*editor, outputIn + juce::Point<float> (-300.0f, -200.0f), outputIn));
    check (! editor->getCables().isPatching() && ! model.isPatchedToOutput (o2), "and dropping it away unplugs it");

    osc1.mouseDown (mouseAt (osc1, osc1.closeBounds().getCentre()));
    check (! model.inputOf (e1).has_value() && param (p, ParamID::osc1On) < 0.5f, "taking an oscillator off the bench unplugs its cable");
    env1.mouseDown (mouseAt (env1, env1.closeBounds().getCentre()));
    check (! model.isPatchedToOutput (e1) && param (p, ParamID::env1On) < 0.5f, "and taking an envelope off unplugs its own");
    releaseAll (p);

    // Back to a fixed order: no jacks, nothing greyed out.
    auto& osc4 = bench.getOscillator (3);
    setParam (p, ParamID::signalFlow, 0.0f);
    check (pumpUntil ([&] { return ! osc4.getHeader().isJackShown() && ! osc4.isDimmed() && bench.getEnvelope (3).getHeader().getTag() == "> OSC 4"; }),
           "a host change of the flow shows too");

    // A session restored while the editor is open moves the cards there.
    juce::MemoryBlock layoutSaved;
    p.getStateInformation (layoutSaved);
    model.move (o4, { 252.0f, 0.0f });
    check (pumpUntil ([&] { return osc4.getPosition() == juce::Point<int> (252, 0); }),
           "a module moved on the bench (by undo, say) moves on screen");
    p.setStateInformation (layoutSaved.getData(), (int) layoutSaved.getSize());
    check (pumpUntil ([&] { return osc4.getPosition() == juce::Point<int> (756, 0); }) && osc4.getBounds() == bench.boundsFor (o4),
           "and restoring a session puts the cards back where it had them");

    // Scaled up: the same face at 2x.
    editor->setSize (2080, 1832);
    check (osc4.getWidth() == 472 && osc4.getHeight() == 672 && approx ((float) osc4.getDial (0).getWidth(), 104.0f, 1.0f),
           "at 2x every module and dial doubles");
    check (osc4.getPosition() == (model.positionOf (o4) * 2.0f).toInt(), "and the modules keep their places");
    check (hasDetail (render (*editor)), "renders at 2x");

    // The aspect ratio is held when a host (or the corner) resizes it.
    if (auto* constrainer = editor->getConstrainer())
        constrainer->setBoundsForComponent (editor, { 0, 0, 1300, 800 }, false, false, true, true);
    const float aspect = (float) editor->getWidth() / (float) editor->getHeight();
    check (approx (aspect, 1040.0f / 916.0f, 0.02f), "resizing keeps the design's proportions");
}
void testBypass()
{
    std::printf ("Bypass:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    bool allOn = true;
    for (size_t k = 0; k < std::size (ParamID::oscillators); ++k)
        for (const auto* id : { ParamID::oscillators[k].active, ParamID::envelopes[k].active })
            allOn = allOn && p.getState().getParameter (id) != nullptr && param (p, id) >= 0.5f;
    check (allOn, "every oscillator and envelope has a power switch, on to start with");

    // ENV 1 shapes OSC 1 with a slow attack; switched off, OSC 1 plays at once.
    fillBench (p);
    setParam (p, ParamID::env1Attack, 3.0f);
    setParam (p, ParamID::env1Release, 2.0f);
    check (holdAndMeasure (p) < 0.01f && approx ((float) p.getTailLengthSeconds(), 2.0f, 0.01f),
           "ENV 1's slow ATTACK fades OSC 1 in, and its RELEASE is the tail");
    releaseAll (p);
    setParam (p, ParamID::env1Active, 0.0f);
    check (p.currentSettings().oscillators[0].envelope == -1 && holdAndMeasure (p) > 0.02f,
           "ENV 1 switched off: OSC 1 plays unshaped, at once");
    check (approx ((float) p.getTailLengthSeconds(), Synth2Engine::gateReleaseSeconds, 1e-4f), "and the tail is the short gate");
    releaseAll (p);
    setParam (p, ParamID::env1Active, 1.0f);
    setParam (p, ParamID::env1Attack, 0.005f);

    setParam (p, ParamID::osc1Active, 0.0f);
    check (holdAndMeasure (p) < 1e-4f, "OSC 1 switched off is silent");
    releaseAll (p);
    setParam (p, ParamID::osc1Active, 1.0f);
    check (holdAndMeasure (p) > 0.02f, "and plays again switched back on");
    releaseAll (p);

    // On the bench: each module's power switch, at the left of its header.
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;
    auto& osc1 = editor->getBench().getOscillator (0);
    auto& env1 = editor->getBench().getEnvelope (0);
    check (pumpUntil ([&] { return osc1.getAlpha() >= 1.0f && env1.getAlpha() >= 1.0f; }) && ! osc1.isDimmed() && ! env1.isDimmed(),
           "OSC 1 and ENV 1 are on the bench, both on");
    check (osc1.getPower().getBounds().getRight() < 40 && osc1.headerBounds().contains (osc1.getPower().getBounds().toFloat()),
           "the power switch sits at the left of the header");

    osc1.getPower().triggerClick();
    check (pumpUntil ([&] { return param (p, ParamID::osc1Active) < 0.5f; }) && osc1.isDimmed() && osc1.isVisible(),
           "clicking OSC 1's power switches it off, greyed out but still on the bench");
    check (osc1.getDial (0).getAlpha() < 0.5f && osc1.getPower().getAlpha() >= 1.0f
               && osc1.getDial (0).getTooltip().startsWith ("Switched off"),
           "its insides grey out, saying why, but not the switch");
    check (holdAndMeasure (p) < 1e-4f, "and it is silent");
    releaseAll (p);
    snapshot (render (*editor), "14-switched-off");
    setParam (p, ParamID::osc1Active, 1.0f);
    check (pumpUntil ([&] { return ! osc1.isDimmed() && osc1.getDial (0).getAlpha() >= 1.0f; }),
           "a host switching it back on lights it again");

    env1.getPower().triggerClick();
    check (pumpUntil ([&] { return param (p, ParamID::env1Active) < 0.5f; }) && env1.isDimmed() && ! osc1.isDimmed(),
           "clicking ENV 1's power switches it off, leaving OSC 1 lit");
    check (env1.getDial (0).getTooltip().contains ("no envelope"), "saying OSC 1 plays as if it had no envelope");
    env1.getPower().triggerClick();
    check (pumpUntil ([&] { return param (p, ParamID::env1Active) >= 0.5f; }) && ! env1.isDimmed(), "and back on");
}

// Counts the change gestures a host would see.
struct GestureCount final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begun; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override   { ++ended; }
    int begun = 0;
    int ended = 0;
};

// Drags on `well` from `from` to `to`, in steps, as a mouse would.
void dragOn (AmpDisplay& well, juce::Point<float> from, juce::Point<float> to)
{
    well.mouseDown (mouseAt (well, from));
    well.mouseDrag (mouseAt (well, from + (to - from) * 0.5f, from));
    well.mouseDrag (mouseAt (well, to, from));
    well.mouseUp (mouseAt (well, to, from));
}

void testViews()
{
    std::printf ("Envelope graph and views:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    fillBench (p);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;
    auto& bench = editor->getBench();
    auto& osc1  = bench.getOscillator (0);
    auto& env1  = bench.getEnvelope (0);
    auto& well  = env1.getDisplay();
    check (pumpUntil ([&] { return osc1.getAlpha() >= 1.0f && env1.getAlpha() >= 1.0f; }), "OSC 1 and ENV 1 are on the bench");

    // Dragging the graph's corners.
    using Handle = AmpDisplay::Handle;
    auto* attack  = p.getState().getParameter (ParamID::env1Attack);
    auto* hold    = p.getState().getParameter (ParamID::env1Hold);
    auto* sustain = p.getState().getParameter (ParamID::env1Sustain);
    GestureCount gestures;
    p.addListener (&gestures);

    // The shape fills the plot: from its left edge to its right, and up to its top.
    const auto fills = [&well]
    {
        const auto plot = well.plotArea();
        return approx (well.handleCentre (Handle::delay).x, plot.getX(), 0.5f)
            && approx (well.handleCentre (Handle::release).x, plot.getRight(), 0.5f)
            && approx (well.handleCentre (Handle::attack).y, plot.getY(), 0.5f);
    };
    check (fills(), "the envelope fills its graph");

    const auto attackAt = well.handleCentre (Handle::attack);
    well.mouseMove (mouseAt (well, attackAt.translated (1.0f, 1.0f)));
    check (well.getHotHandle() == (int) Handle::attack, "hovering ATTACK's corner picks it out");
    const float attackBefore = attack->getValue();
    const auto  attackTo     = attackAt.translated (24.0f, 0.0f);
    const float expected     = Synth2VisualModel::dialAt (well.getAmpState().dials, Handle::attack,
                                                          (attackTo.x - well.plotArea().getX()) / well.plotArea().getWidth());
    well.mouseDown (mouseAt (well, attackAt));
    well.mouseDrag (mouseAt (well, attackAt.translated (12.0f, 0.0f), attackAt));
    well.mouseDrag (mouseAt (well, attackTo, attackAt));
    check (attack->getValue() > attackBefore + 0.05f && approx (attack->getValue(), expected, 1e-4f), "dragging it right lengthens ATTACK");
    check (well.handleCentre (Handle::attack).getDistanceFrom (attackTo) < 1.5f && fills(),
           "the corner stays under the pointer, the rest making room so the envelope still fills the graph");
    well.mouseUp (mouseAt (well, attackTo, attackAt));
    check (gestures.begun == 1 && gestures.ended == 1, "the whole drag is one gesture for the host");

    // A HOLD of 0 puts its corner on ATTACK's: pressing just right of it pulls HOLD out.
    const auto holdAt = well.handleCentre (Handle::hold);
    check (holdAt.getDistanceFrom (well.handleCentre (Handle::attack)) < 0.5f, "with no HOLD, its corner sits on ATTACK's");
    const float attackNow = attack->getValue();
    well.mouseMove (mouseAt (well, holdAt.translated (4.0f, 0.0f)));
    check (well.getHotHandle() == (int) Handle::hold, "just right of it, HOLD's corner is picked out");
    dragOn (well, holdAt.translated (4.0f, 0.0f), holdAt.translated (16.0f, 0.0f));
    check (hold->getValue() > 0.1f && approx (attack->getValue(), attackNow, 1e-6f), "and dragging there pulls HOLD out, ATTACK stays");

    // DECAY's corner moves SUSTAIN up and down too.
    check (pumpUntil ([&] { return approx (well.getAmpState().dials.hold, hold->getValue(), 1e-6f); }), "the well catches up");
    const auto decayAt = well.handleCentre (Handle::decay);
    const auto decayTo = decayAt.translated (10.0f, 20.0f);
    well.mouseDown (mouseAt (well, decayAt));
    well.mouseDrag (mouseAt (well, decayTo, decayAt));
    check (sustain->convertFrom0to1 (sustain->getValue()) < 0.9f && p.getState().getParameter (ParamID::env1Decay)->getValue() > 0.4f,
           "dragging DECAY's corner down and right lowers SUSTAIN and lengthens DECAY");
    check (well.handleCentre (Handle::decay).getDistanceFrom (decayTo) < 1.5f, "and it follows the pointer");
    well.mouseUp (mouseAt (well, decayTo, decayAt));

    // The attack's dot, halfway up its ramp, bends it: dragged up, the ramp
    // bulges up under the pointer; a double-click straightens it again.
    auto* attackCurve = p.getState().getParameter (ParamID::env1AttackCurve);
    check (pumpUntil ([&] { return approx (well.getAmpState().dials.sustain, sustain->convertFrom0to1 (sustain->getValue()), 1e-6f); }),
           "the well catches up with SUSTAIN");
    const auto bendAt = well.handleCentre (Handle::attackCurve);
    well.mouseMove (mouseAt (well, bendAt));
    check (well.getHotHandle() == (int) Handle::attackCurve, "hovering the attack's dot picks it out");
    const auto bendTo = bendAt.translated (0.0f, -20.0f);
    well.mouseDown (mouseAt (well, bendAt));
    well.mouseDrag (mouseAt (well, bendTo, bendAt));
    check (attackCurve->convertFrom0to1 (attackCurve->getValue()) > 0.1f, "dragging it up bends the attack up");
    check (well.handleCentre (Handle::attackCurve).getDistanceFrom (bendTo) < 1.5f, "and the dot follows the pointer");
    well.mouseUp (mouseAt (well, bendTo, bendAt));
    check (p.currentSettings().envelopes[0].attackCurve > 0.1f, "the engine is given the bend");
    well.mouseDoubleClick (mouseAt (well, well.handleCentre (Handle::attackCurve)));
    check (approx (attackCurve->convertFrom0to1 (attackCurve->getValue()), 0.0f, 1e-6f), "a double-click on the dot straightens the ramp");

    // A double-click as JUCE sends it: down, up, down, up, then the double-click.
    const auto corner = well.handleCentre (Handle::attack);
    gestures.begun = 0;
    gestures.ended = 0;
    for (int click = 0; click < 2; ++click)
    {
        well.mouseDown (mouseAt (well, corner));
        well.mouseUp (mouseAt (well, corner, corner));
    }
    well.mouseDoubleClick (mouseAt (well, corner, corner));
    check (approx (attack->getValue(), attack->getDefaultValue(), 1e-6f), "double-clicking ATTACK's corner puts it back");
    check (gestures.begun == 3 && gestures.ended == 3, "with every gesture closed before the next begins");
    well.mouseDown (mouseAt (well, corner));
    well.mouseDoubleClick (mouseAt (well, corner, corner));
    well.mouseUp (mouseAt (well, corner, corner));
    check (gestures.begun == gestures.ended, "even if a double-click arrives while a click is still down");
    p.removeListener (&gestures);

    // OUTPUT is in the readout: turned down, the shape keeps its height.
    check (pumpUntil ([&] { return approx (well.getAmpState().dials.attack, attack->getValue(), 1e-6f); }), "the well catches up again");
    const auto topBefore = well.handleCentre (Handle::attack);
    setParam (p, ParamID::output, -24.0f);
    check (pumpUntil ([&] { return approx (well.getAmpState().outputDb, -24.0f, 1e-3f); }), "the well hears OUTPUT turned down");
    check (well.handleCentre (Handle::attack) == topBefore && fills(), "and the shape stays as it was");
    setParam (p, ParamID::output, 0.0f);
    dragOn (well, well.handleCentre (Handle::release), well.handleCentre (Handle::release).translated (-30.0f, 0.0f));
    check (fills(), "a shorter RELEASE still fills the graph");
    setParam (p, ParamID::env1AttackCurve, 0.6f);
    setParam (p, ParamID::env1DecayCurve, -0.7f);
    setParam (p, ParamID::env1ReleaseCurve, -0.6f);
    check (pumpUntil ([&] { return approx (well.getAmpState().outputDb, 0.0f, 1e-3f) && approx (well.getAmpState().dials.releaseCurve, -0.6f, 1e-4f); }),
           "the well shows the ramps bent");
    snapshot (render (*editor), "15-envelope-graph");

    // The gear menu's views.
    auto& settings = editor->getTrays().getSettings();
    const auto pick = [&] (int choice)
    {
        editor->mouseDown (mouseAt (*editor, editor->gearBounds().getCentre()));
        settings.mouseDown (mouseAt (settings, settings.choiceBounds (choice).getCentre()));
    };
    const auto oscAt = osc1.getPosition();
    const auto envAt = env1.getPosition();
    Bench::Model model (p.getState());

    // Where the corners sit, as shares of the plot.
    const auto shares = [&well]
    {
        const auto plot = well.plotArea();
        std::array<juce::Point<float>, Synth2VisualModel::numHandles> at;
        for (size_t h = 0; h < at.size(); ++h)
        {
            const auto c = well.handleCentre ((Handle) h);
            at[h] = { (c.x - plot.getX()) / plot.getWidth(), (plot.getBottom() - c.y) / plot.getHeight() };
        }
        return at;
    };
    const auto smallShares = shares();
    pick (3);
    check (model.view().envelopes == Bench::EnvelopeView::knobs && env1.getHeight() == 240 && ! well.isVisible()
               && env1.getDial (0).isVisible(),
           "Knobs: the envelope keeps its dials on a shorter card");
    pick (4);
    check (model.view().envelopes == Bench::EnvelopeView::graph && env1.getHeight() == 336 && well.isVisible()
               && well.getHeight() == 286 && ! env1.getDial (0).isVisible(),
           "Graph: a big graph fills the card, no dials");
    const auto bigShares = shares();
    bool sameShape = fills();
    for (size_t h = 0; h < bigShares.size(); ++h)
        sameShape = sameShape && bigShares[h].getDistanceFrom (smallShares[h]) < 1e-3f;
    check (sameShape, "with the same shape as the small graph, filling it the same way");
    pick (7);
    check (model.view().oscillators == Bench::OscillatorView::compact && osc1.getHeight() == 268
               && ! osc1.getDisplay().isVisible() && osc1.getPicker().isCompact() && osc1.getPicker().getHeight() == 28,
           "Compact: the oscillator shows just its shape's pill over its dials");
    check (osc1.getPosition() == oscAt && env1.getPosition() == envAt, "the cards keep their places");
    osc1.getPicker().mouseDown (mouseAt (osc1.getPicker(), osc1.getPicker().nextArrow().getCentre()));
    check (approx (param (p, ParamID::osc1Wave), (float) Waveform::square, 1e-6f), "the pill still steps the shape");
    snapshot (render (*editor), "16-graph-compact");

    // The view is kept with the set, and presets leave it alone.
    p.loadPreset (1);
    check (model.view() == Bench::View { Bench::EnvelopeView::graph, Bench::OscillatorView::compact }, "loading a preset keeps the view");
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    Synth2AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    check (Bench::Model (restored.getState()).view() == model.view(), "and a saved set opens with it");
    check (Bench::Model (Synth2AudioProcessor().getState()).view() == Bench::View {}, "a new one opens with both and full");

    pick (5);
    pick (6);
    check (env1.getHeight() == 336 && well.getHeight() == 96 && env1.getDial (0).isVisible() && osc1.getDisplay().isVisible()
               && ! osc1.getPicker().isCompact(),
           "Both and Full bring the cards back as they were");
}

// Turns the wheel over `target` at `at` by `delta` (holding Cmd / Ctrl when `command`).
void wheelAt (juce::Component& target, juce::Point<float> at, juce::Point<float> delta, bool command = false)
{
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at,
                              command ? juce::ModifierKeys (juce::ModifierKeys::commandModifier) : juce::ModifierKeys(),
                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, at, now, 0, false);
    juce::MouseWheelDetails wheel {};
    wheel.deltaX   = delta.x;
    wheel.deltaY   = delta.y;
    wheel.isSmooth = true;
    target.mouseWheelMove (e, wheel);
}

void testBenchZoom()
{
    std::printf ("Bench zoom and pan:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;

    auto& bench = editor->getBench();
    auto& pill  = bench.getZoomPill();
    Bench::Model model (p.getState());
    using Kind = Bench::Kind;
    check (bench.getViewport() == Bench::Viewport {} && pill.zoomText() == "100%", "a new bench opens at 100%, on the canvas's top left");
    check (pill.isVisible() && pill.getRight() < bench.getRight() && pill.getRight() > bench.getRight() - 30
               && pill.getY() < bench.getBottom() && pill.getBottom() > bench.getBottom() && pill.getBottom() < editor->getKeyboard().getY(),
           "with the zoom pill on its bottom edge, at its right, clear of the keyboard");
    check (approx (Bench::areaWidth / Bench::areaHeight, Bench::viewWidth / Bench::viewHeight, 0.01f)
               && Bench::areaWidth >= 6.0f * Bench::moduleWidth + 5.0f * Bench::gap && Bench::areaHeight >= 3.0f * Bench::oscillatorHeight + 2.0f * Bench::gap,
           "the canvas holds three rows of six cards, in the box's proportions");

    // Three oscillators and two envelopes, one oscillator and one envelope
    // past what the box shows at 100%.
    const Bench::ModuleId o1 { Kind::oscillator, 0 }, o2 { Kind::oscillator, 1 }, o3 { Kind::oscillator, 2 };
    const Bench::ModuleId e1 { Kind::envelope, 0 },   e2 { Kind::envelope, 1 };
    bench.add (Kind::oscillator, { 0.0f, 0.0f });
    bench.add (Kind::oscillator, { 252.0f, 0.0f });
    bench.add (Kind::envelope, { 0.0f, 352.0f });
    model.add (Kind::oscillator, { 1100.0f, 0.0f });
    model.add (Kind::envelope, { 1260.0f, 704.0f });
    auto& osc1 = bench.getModule (o1);
    auto& osc3 = bench.getModule (o3);
    auto& env1 = bench.getModule (e1);
    auto& env2 = bench.getModule (e2);
    check (pumpUntil ([&] { return osc3.isVisible() && env2.isVisible() && osc3.getAlpha() >= 1.0f; }) && model.positionOf (o3) == juce::Point<float> (1100.0f, 0.0f),
           "cards can sit anywhere on the canvas");
    check (osc3.getX() >= bench.getWidth() && env2.getY() >= bench.getHeight(), "and those past the box are out of sight");

    // Cmd + wheel zooms round the pointer, as does a pinch.
    bench.setViewport ({ 1.0f, { 300.0f, 200.0f } });
    const juce::Point<float> pointer (400.0f, 300.0f);
    const auto under = bench.toDesign (pointer);
    wheelAt (bench, pointer, { 0.0f, 0.1f }, true);
    const float zoomedIn = bench.getViewport().zoom;
    check (zoomedIn > 1.1f && bench.toDesign (pointer).getDistanceFrom (under) < 0.01f, "Cmd + wheel up zooms in, the canvas under the pointer staying put");
    check (osc1.getWidth() == juce::roundToInt (Bench::moduleWidth * zoomedIn) && osc1.getBounds() == bench.boundsFor (o1)
               && approx ((float) osc1.getDial (0).getWidth(), 52.0f * zoomedIn, 1.0f),
           "and the cards are drawn bigger, dials and all");
    wheelAt (bench, pointer, { 0.0f, -0.2f }, true);
    check (bench.getViewport().zoom < 1.0f && bench.toDesign (pointer).getDistanceFrom (under) < 0.01f, "Cmd + wheel down zooms out round it");
    const float beforePinch = bench.getViewport().zoom;
    bench.mouseMagnify (mouseAt (bench, pointer), 1.2f);
    check (approx (bench.getViewport().zoom, beforePinch * 1.2f, 1e-4f) && bench.toDesign (pointer).getDistanceFrom (under) < 0.01f,
           "a pinch zooms round the pointer too");
    wheelAt (bench, pointer, { 0.0f, -5.0f }, true);
    check (juce::exactlyEqual (bench.getViewport().zoom, Bench::fitZoom) && bench.getViewport().visibleArea().contains (juce::Rectangle<float> (Bench::areaWidth, Bench::areaHeight)),
           "zooming out stops with the whole canvas in the box");
    wheelAt (bench, pointer, { 0.0f, 5.0f }, true);
    check (juce::exactlyEqual (bench.getViewport().zoom, Bench::maxZoom) && pill.zoomText() == "150%", "and zooming in at 150%");

    // The pill's minus and plus step through the levels, round the middle.
    bench.setViewport ({ 1.0f, { 200.0f, 150.0f } });
    const auto pressPill = [&pill] (ZoomPill::Part part) { pill.mouseDown (mouseAt (pill, pill.partBounds (part).getCentre())); };
    const auto middle = bench.toDesign (bench.getLocalBounds().toFloat().getCentre());
    pressPill (ZoomPill::Part::zoomIn);
    check (approx (bench.getViewport().zoom, 1.25f, 1e-6f) && pill.zoomText() == "125%"
               && bench.toDesign (bench.getLocalBounds().toFloat().getCentre()).getDistanceFrom (middle) < 0.01f,
           "plus zooms in a step, round the middle of the box");
    pressPill (ZoomPill::Part::zoomIn);
    pressPill (ZoomPill::Part::zoomIn);
    check (juce::exactlyEqual (bench.getViewport().zoom, Bench::maxZoom), "up to 150%");
    std::vector<float> steps;
    for (int i = 0; i < 6; ++i)
    {
        pressPill (ZoomPill::Part::zoomOut);
        steps.push_back (bench.getViewport().zoom);
    }
    check (steps == std::vector<float> { 1.25f, 1.0f, 0.8f, Bench::fitZoom, Bench::fitZoom, Bench::fitZoom }, "minus steps back down to the whole canvas");

    // Fit frames the cards.
    bench.setViewport ({ 1.5f, {} });
    pressPill (ZoomPill::Part::fit);
    bool allInside = true;
    for (const auto& id : model.present())
        allInside = allInside && bench.getLocalBounds().contains (bench.getModule (id).getBounds());
    check (allInside && bench.getViewport().zoom <= 1.0f, "Fit shows every card");
    const float allFramed = bench.getViewport().zoom;
    model.remove (o3);
    model.remove (e2);
    pressPill (ZoomPill::Part::fit);
    check (pumpUntil ([&] { return ! osc3.isVisible() && ! env2.isVisible(); }) && bench.getViewport().zoom > allFramed
               && bench.getViewport().zoom <= 1.0f && bench.getViewport().visibleArea().contains (model.extent()),
           "with fewer cards it comes in closer, but no closer than 100%");
    model.remove (o1);
    model.remove (e1);
    pressPill (ZoomPill::Part::fit);
    check (juce::exactlyEqual (bench.getViewport().zoom, 1.0f) && bench.getViewport().visibleArea().contains (model.extent()), "as for one or two");
    bench.add (Kind::oscillator, { 0.0f, 0.0f });
    bench.add (Kind::envelope, { 0.0f, 352.0f });
    model.add (Kind::envelope, { 1260.0f, 704.0f });
    pumpUntil ([&] { return osc1.isVisible() && env1.isVisible() && env2.isVisible(); });

    // The wheel pans, over the bench or a card; a dial takes it for itself.
    bench.setViewport ({ 1.0f, { 100.0f, 100.0f } });
    wheelAt (bench, { 600.0f, 500.0f }, { -30.0f / 512.0f, -50.0f / 512.0f });
    check (bench.getViewport().pan.getDistanceFrom ({ 130.0f, 150.0f }) < 0.01f, "the wheel (or two fingers) pans");
    check (bench.getViewport().pan == model.viewport().pan, "and the bench keeps where");
    wheelAt (osc1, osc1.headerBounds().getCentre(), { 0.0f, 20.0f / 512.0f });
    check (bench.getViewport().pan.getDistanceFrom ({ 130.0f, 130.0f }) < 0.01f, "over a card too");
    const auto& pitch = osc1.getDial (0);
    const double pitchBefore = pitch.getValue();
    wheelAt (osc1.getDial (0), osc1.getDial (0).getLocalBounds().getCentre().toFloat(), { 0.0f, 0.2f });
    check (! juce::exactlyEqual (pitch.getValue(), pitchBefore) && bench.getViewport().pan.getDistanceFrom ({ 130.0f, 130.0f }) < 0.01f,
           "while over a dial it turns the dial");
    osc1.getDial (0).setValue (pitchBefore, juce::sendNotificationSync);

    // Dragging an empty spot pans; a click there adds a module, where it is on the canvas.
    auto& tray = editor->getTrays().getAdd();
    bench.setViewport ({ 1.0f, {} });
    const juce::Point<float> spot (700.0f, 500.0f);
    bench.mouseDown (mouseAt (bench, spot));
    bench.mouseDrag (mouseAt (bench, spot + juce::Point<float> (-3.0f, 0.0f), spot));
    check (bench.getViewport().pan == juce::Point<float>(), "a press that barely moves doesn't pan");
    bench.mouseDrag (mouseAt (bench, spot + juce::Point<float> (-100.0f, -60.0f), spot));
    bench.mouseUp (mouseAt (bench, spot + juce::Point<float> (-100.0f, -60.0f), spot));
    check (bench.getViewport().pan.getDistanceFrom ({ 100.0f, 60.0f }) < 0.01f && ! tray.isOpen(), "dragging an empty spot pans, opening nothing");

    bench.setViewport ({ 0.75f, { 100.0f, 100.0f } });
    const juce::Point<float> clicked (600.0f, 400.0f);
    const auto canvasAt = bench.toDesign (clicked);
    bench.mouseDown (mouseAt (bench, clicked));
    bench.mouseUp (mouseAt (bench, clicked, clicked));
    check (tray.isOpen() && tray.trayBounds().getPosition().getDistanceFrom (clicked + bench.getPosition().toFloat()) < 1.0f,
           "zoomed out, a click opens the add tray where it was");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (0).getCentre()));
    check (pumpUntil ([&] { return osc3.isVisible(); })
               && model.positionOf (o3).getDistanceFrom (canvasAt - juce::Point<float> (0.5f * Bench::moduleWidth, 15.0f)) < 0.01f
               && osc3.getBounds() == bench.boundsFor (o3) && osc3.getWidth() == juce::roundToInt (Bench::moduleWidth * 0.75f),
           "and the module lands under the click, on the canvas");

    // At 75%, a card dragged by its header lands under the pointer.
    const auto o3Before = model.positionOf (o3);
    const auto o3View   = osc3.getPosition();
    dragHeader (osc3, { 90.0f, -60.0f });
    check (osc3.getPosition() == o3View + juce::Point<int> (90, -60)
               && model.positionOf (o3).getDistanceFrom (o3Before + juce::Point<float> (120.0f, -80.0f)) < 1.5f,
           "a card dragged at 75% lands under the pointer");
    check (bench.toView (model.positionOf (o3)).getDistanceFrom (osc3.getPosition().toFloat()) < 0.5f, "and stays there");

    // A cable patches at 150%; a card out of the box can't take one.
    setParam (p, ParamID::signalFlow, 2.0f);
    check (pumpUntil ([&] { return env1.getHeader().isJackShown (BenchModule::Jack::in); }), "with patch cables, the jacks show");
    bench.setViewport ({ 1.5f, {} });
    const auto patchTo = [&editor] (BenchModule& module, juce::Point<float> to)
    {
        const auto from = module.jackBounds().getCentre();
        module.mouseDown (mouseAt (module, from));
        module.mouseDrag (mouseAt (module, module.getLocalPoint (editor, to), from));
        module.mouseUp (mouseAt (module, module.getLocalPoint (editor, to), from));
    };
    patchTo (osc1, editor->getCables().jackCentre (e1, BenchModule::Jack::in));
    check (model.inputOf (e1) == std::optional (o1), "at 150% a cable from OSC 1 patches into ENV 1's IN");
    patchTo (env1, editor->getCables().outputJackBounds().getCentre());
    check (model.isPatchedToOutput (e1), "and ENV 1 into the output's IN");
    check (! bench.getLocalBounds().contains (osc3.getBounds().getCentre()), "OSC 3 is out of the box");
    model.connect (o3);
    patchTo (bench.getModule (o2), editor->getCables().jackCentre (e2, BenchModule::Jack::in));
    check (! model.inputOf (e2).has_value(), "a cable dropped where a card out of the box would be plugs into nothing");
    pumpUntil ([&] { return false; }, 100);
    snapshot (render (*editor), "bench-zoomed-in");

    // Zoomed out, the whole canvas.
    bench.setViewport ({ Bench::fitZoom, {} });
    check (osc3.getBounds() == bench.boundsFor (o3) && bench.getLocalBounds().contains (osc3.getBounds()), "zoomed right out, every card shows");
    pumpUntil ([&] { return false; }, 100);
    snapshot (render (*editor), "bench-zoomed-out");

    // The zoom and pan are kept with the set.
    bench.setViewport ({ 1.25f, { 120.0f, 80.0f } });
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    Synth2AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    const Bench::Viewport kept { 1.25f, { 120.0f, 80.0f } };
    check (Bench::Model (restored.getState()).viewport() == kept, "a saved set keeps the zoom and pan");
    {
        std::unique_ptr<juce::AudioProcessorEditor> other (restored.createEditor());
        auto* reopened = dynamic_cast<Synth2AudioProcessorEditor*> (other.get());
        check (reopened != nullptr && reopened->getBench().getViewport() == kept
                   && reopened->getBench().getModule (o3).getBounds() == bench.boundsFor (o3),
               "and opens looking as it was left");
    }
    bench.setViewport ({ 0.8f, { 10.0f, 20.0f } });
    p.setStateInformation (saved.getData(), (int) saved.getSize());
    check (pumpUntil ([&] { return bench.getViewport() == kept; }) && pill.zoomText() == "125%", "restoring it with the editor open zooms back");

    // A session from before the zoom opens at 100%, the cards where they were.
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    if (auto* benchXml = xml != nullptr ? xml->getChildByName (Bench::benchType) : nullptr)
        for (const auto& property : { Bench::zoomProperty, Bench::panXProperty, Bench::panYProperty })
            benchXml->removeAttribute (property);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    Synth2AudioProcessor older;
    older.setStateInformation (old.getData(), (int) old.getSize());
    {
        std::unique_ptr<juce::AudioProcessorEditor> other (older.createEditor());
        auto* reopened = dynamic_cast<Synth2AudioProcessorEditor*> (other.get());
        const Bench::Model olderModel (older.getState());
        check (reopened != nullptr && reopened->getBench().getViewport() == Bench::Viewport {}
                   && reopened->getBench().getModule (o1).getBounds() == juce::Rectangle<int> (0, 0, 236, 336)
                   && reopened->getBench().getModule (e1).getBounds() == juce::Rectangle<int> (0, 352, 236, 336)
                   && olderModel.positionOf (o3) == model.positionOf (o3),
               "a set saved before the zoom opens at 100%, its cards where they were");
    }

    // Presets leave the zoom alone, unless their cards wouldn't all show.
    bench.setViewport ({ 0.8f, {} });
    p.loadPreset (2);
    check (pumpUntil ([&] { return model.present().size() > 1; }) && bench.getViewport() == Bench::Viewport { 0.8f, {} },
           "loading a preset keeps the zoom when its cards all show");
    bench.setViewport ({ 1.5f, { 700.0f, 600.0f } });
    p.loadPreset (3);
    check (pumpUntil ([&] { return bench.getViewport() == model.viewport() && bench.getViewport().zoom < 1.5f; })
               && bench.getViewport().visibleArea().contains (model.extent()),
           "and frames them when they wouldn't");
}

// Drags `card` by its header so its top left moves by `by`.
void dragCard (EffectCard& card, juce::Point<float> by)
{
    const auto grab = card.headerBounds().getCentre().translated (-60.0f, 0.0f);
    card.mouseDown (mouseAt (card, grab));
    card.mouseDrag (mouseAt (card, grab + by * 0.5f, grab));
    card.mouseDrag (mouseAt (card, grab + by, grab));
    card.mouseUp (mouseAt (card, grab + by, grab));
}

void testEffectsTab()
{
    std::printf ("Effects tab:\n");

    Synth2AudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;
    using Tab = Synth2AudioProcessorEditor::Tab;
    using Kind = Rack::Kind;

    auto& bench   = editor->getBench();
    auto& effects = editor->getEffects();
    const auto tabs = editor->tabBounds (Tab::bench).getUnion (editor->tabBounds (Tab::effects));
    check (editor->getTab() == Tab::bench && bench.isVisible() && ! effects.isVisible(), "it opens on the bench");
    check (tabs.getBottom() < (float) bench.getY() && ! tabs.intersects (editor->presetBounds()) && tabs.getX() > 140.0f,
           "the tabs sit in the header, between the wordmark and the presets");

    editor->mouseDown (mouseAt (*editor, editor->tabBounds (Tab::effects).getCentre()));
    check (editor->getTab() == Tab::effects && effects.isVisible() && ! bench.isVisible() && effects.getBounds() == bench.getBounds(),
           "EFFECTS swaps the bench for the effects, in its place");
    check (! bench.getZoomPill().isVisible(), "the bench's zoom pill goes with it (the effects don't zoom)");
    check (editor->getKeyboard().isVisible() && editor->getOutputDial().isVisible(), "with the keyboard and OUTPUT still below");

    auto& drive  = effects.getDrive();
    auto& delay  = effects.getDelay();
    auto& reverb = effects.getReverb();
    const Rack::Model rack (p.getState());
    check (! drive.isVisible() && ! delay.isVisible() && ! reverb.isVisible() && effects.isShowingHint() && effects.getChain().empty(),
           "it starts empty, like the bench, with a hint to click it");
    check (effects.rackArea().getY() > 0 && effects.rackArea().getBottom() == effects.getHeight(),
           "the area the cards sit on runs under the line spelling out the chain");
    snapshot (render (*editor), "11-effects-empty");

    // A click on an empty spot offers the effects, there.
    auto& tray = editor->getTrays().getEffect();
    const auto area = effects.rackArea().toFloat();
    // A click that puts a card's top left at `topLeft` (design pixels on the area).
    const auto clickFor = [&] (juce::Point<float> topLeft)
    {
        effects.mouseDown (mouseAt (effects, area.getPosition() + topLeft + juce::Point<float> (0.5f * Rack::cardWidth, 15.0f)));
    };
    const auto addAt = [&] (int choice, juce::Point<float> topLeft)
    {
        clickFor (topLeft);
        tray.mouseDown (mouseAt (tray, tray.choiceBounds (choice).getCentre()));
    };

    clickFor (Rack::defaultPosition (0));
    check (tray.isOpen() && tray.getCaption() == "ADD AN EFFECT" && tray.getNumChoices() == 3
               && tray.getChoice (0).name == "Drive" && tray.getChoice (1).name == "Delay" && tray.getChoice (2).name == "Reverb",
           "a click on the area offers a drive, a delay and a reverb");
    check (tray.getChoice (0).enabled && tray.getChoice (1).enabled && tray.getChoice (2).enabled, "all three free");
    snapshot (render (*editor), "12-effects-tray");
    tray.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (! tray.isOpen() && effects.isShowingHint(), "Escape closes it, adding nothing");

    addAt (0, Rack::defaultPosition (0));
    check (param (p, ParamID::driveInChain) >= 0.5f && param (p, ParamID::driveOn) >= 0.5f && p.currentSettings().effects.drive.on,
           "picking Drive puts the drive in the chain, switched on");
    check (pumpUntil ([&drive] { return drive.isVisible() && drive.getAlpha() >= 1.0f; }), "its card fades in");
    check (rack.positionOf (Kind::drive) == Rack::defaultPosition (0)
               && drive.getBounds() == effects.boundsFor (Kind::drive) && drive.getY() >= effects.rackArea().getY(),
           "under the click, its header where the click was");
    check (! effects.isShowingHint() && drive.getPlace() == 1 && effects.getChain() == std::vector<Kind> { Kind::drive },
           "first in the chain");

    clickFor (Rack::defaultPosition (1));
    check (! tray.getChoice (0).enabled && tray.getChoice (0).detail == "Already in the chain" && tray.getChoice (1).enabled,
           "one of each: the drive is greyed out in the tray now");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (1).getCentre()));
    addAt (2, Rack::defaultPosition (2));
    check (pumpUntil ([&] { return delay.getAlpha() >= 1.0f && reverb.getAlpha() >= 1.0f; }), "the delay and the reverb fade in");
    check (drive.getPlace() == 1 && delay.getPlace() == 2 && reverb.getPlace() == 3
               && drive.getRight() < delay.getX() && delay.getRight() < reverb.getX() && reverb.getRight() <= effects.getWidth(),
           "side by side, in the order they read in");
    check (p.currentSettings().effects.order == Effects::Order { Kind::drive, Kind::delay, Kind::reverb }, "and they play in that order");
    clickFor ({ 0.0f, 0.0f });
    check (! tray.getChoice (0).enabled && ! tray.getChoice (1).enabled && ! tray.getChoice (2).enabled, "with all three in, none is left");
    tray.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));

    check (drive.getNumDials() == 3 && delay.getNumDials() == 5 && reverb.getNumDials() == 5,
           "DRIVE, TONE and MIX; TIME (or its note), FEEDBACK, TONE and MIX; SIZE, PRE-DELAY, DAMPING, WIDTH and MIX");
    check (reverb.getDial (3).getY() > reverb.getDial (0).getY() && delay.getDial (3).getY() > delay.getDial (2).getY()
               && drive.getDisplay().getHeight() > delay.getDisplay().getHeight(),
           "the dials sit in rows of up to three; the drive's one row leaves its well taller");
    check (drive.getDial (0).getAlpha() >= 1.0f && delay.getDial (0).getAlpha() >= 1.0f && reverb.getDial (0).getAlpha() >= 1.0f,
           "all on, so all lit");

    // The power switch bypasses an effect in place.
    delay.getPower().triggerClick();
    check (pumpUntil ([&] { return param (p, ParamID::delayOn) < 0.5f && delay.getDial (0).getAlpha() < 0.5f; }),
           "the delay's power switches it off, greying it out");
    check (delay.isVisible() && param (p, ParamID::delayInChain) >= 0.5f && ! p.currentSettings().effects.delay.on
               && effects.getChain().size() == 3,
           "it stays in its place in the chain, silent");
    setParam (p, ParamID::delayOn, 1.0f);
    check (pumpUntil ([&delay] { return delay.getDial (0).getAlpha() >= 1.0f; }), "a host switching it back on lights it");

    delay.getDial (2).setValue (0.6, juce::sendNotificationSync);
    check (approx (param (p, ParamID::delayFeedback), 0.6f, 1e-4f), "FEEDBACK drives its parameter");
    check (pumpUntil ([&delay] { return approx (delay.getDisplay().getDelayState().feedback, 0.6f, 1e-4f); }), "and the well follows");
    reverb.getDial (0).setValue (0.8, juce::sendNotificationSync);
    check (approx (param (p, ParamID::reverbSize), 0.8f, 1e-4f)
               && pumpUntil ([&reverb] { return approx (reverb.getDisplay().getReverbState().size, 0.8f, 1e-4f); }),
           "SIZE drives its parameter and the room");

    check (delay.getDial (0).isVisible() && ! delay.getDial (1).isVisible(), "TIME turns in milliseconds");
    delay.getSync().setToggleState (true, juce::sendNotificationSync);
    check (param (p, ParamID::delaySync) >= 0.5f
               && pumpUntil ([&delay] { return ! delay.getDial (0).isVisible() && delay.getDial (1).isVisible(); }),
           "with SYNC, it picks a note length instead");
    check (delay.getDial (1).getBounds() == delay.getDial (0).getBounds(), "in the same place");
    check (pumpUntil ([&delay] { return delay.getDisplay().getDelayState().sync; }), "and the well says so");
    delay.getPingPong().setToggleState (true, juce::sendNotificationSync);
    check (param (p, ParamID::delayPingPong) >= 0.5f, "PING-PONG switches on");
    pumpUntil ([&delay] { return delay.getDisplay().getDelayState().pingPong; });

    drive.getDial (0).setValue (0.5, juce::sendNotificationSync);
    check (approx (param (p, ParamID::driveAmount), 0.5f, 1e-4f)
               && p.getState().getParameter (ParamID::driveAmount)->getCurrentValueAsText() == "+18.0 dB",
           "DRIVE drives its parameter, reading in decibels");
    check (pumpUntil ([&drive] { return approx (drive.getDisplay().getDriveState().amount, 0.5f, 1e-4f); }), "and the curve follows");
    auto& shape = drive.getShape();
    check (shape.getNumChoices() == 3 && shape.getSelected() == 0, "WARM, TUBE and HARD, WARM to start with");
    shape.mouseDown (mouseAt (shape, shape.segmentBounds (1).getCentre()));
    check (approx (param (p, ParamID::driveShape), 1.0f, 1e-6f) && shape.getSelected() == 1
               && pumpUntil ([&drive] { return drive.getDisplay().getDriveState().shape == 1; }),
           "clicking TUBE picks it, and the curve leans");
    snapshot (render (*editor), "13-effects-on");

    // Dragging a card moves it anywhere on the area.
    dragCard (delay, { 0.0f, 80.0f });
    check (pumpUntil ([&] { return delay.getBounds() == effects.boundsFor (Kind::delay); })
               && approx (rack.positionOf (Kind::delay).y, Rack::defaultPosition (1).y + 80.0f, 0.5f),
           "dragging the delay's header moves it down, and the area keeps the spot");
    check (effects.getChain() == std::vector<Kind> { Kind::drive, Kind::delay, Kind::reverb }, "still in the same row, so the order holds");

    // Dropping one card on another swaps the two (how a full row is reordered).
    dragCard (reverb, { -2.0f * (Rack::cardWidth + Rack::gap), 0.0f });
    check (pumpUntil ([&] { return reverb.getBounds() == effects.boundsFor (Kind::reverb) && drive.getBounds() == effects.boundsFor (Kind::drive); })
               && rack.positionOf (Kind::reverb) == Rack::defaultPosition (0) && rack.positionOf (Kind::drive) == Rack::defaultPosition (2),
           "dropping the reverb on the drive swaps them, the drive gliding over");
    check (reverb.getPlace() == 1 && delay.getPlace() == 2 && drive.getPlace() == 3
               && p.currentSettings().effects.order == Effects::Order { Kind::reverb, Kind::delay, Kind::drive },
           "so the reverb now comes first and the drive last, in the headers and the sound");
    snapshot (render (*editor), "14-effects-reordered");

    // The cross takes an effect out of the chain, keeping its settings and place.
    const auto cross = delay.closeBounds().getCentre();
    delay.mouseDown (mouseAt (delay, cross));
    check (param (p, ParamID::delayInChain) < 0.5f && ! p.currentSettings().effects.delay.on && param (p, ParamID::delayOn) >= 0.5f,
           "the delay's cross takes it out of the chain, silent, its switch as it was");
    check (pumpUntil ([&delay] { return ! delay.isVisible(); }) && effects.getChain() == std::vector<Kind> { Kind::reverb, Kind::drive }
               && drive.getPlace() == 2,
           "its card fades out, and the drive moves up to second");
    const auto delayWas = rack.positionOf (Kind::delay);
    clickFor ({ 0.0f, 0.0f });
    check (tray.getChoice (1).enabled && ! tray.getChoice (0).enabled, "the delay is back in the tray");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (1).getCentre()));
    check (pumpUntil ([&delay] { return delay.isVisible() && delay.getAlpha() >= 1.0f; })
               && approx (param (p, ParamID::delayFeedback), 0.6f, 1e-4f) && param (p, ParamID::delaySync) >= 0.5f,
           "adding it again brings it back with its settings");
    check (rack.positionOf (Kind::delay) != delayWas && effects.getChain() == std::vector<Kind> { Kind::reverb, Kind::delay, Kind::drive },
           "on the free spot nearest the click, back in the chain where it now reads");

    // A host taking an effect out (undo, a restored set) takes its card away too.
    setParam (p, ParamID::reverbInChain, 0.0f);
    check (pumpUntil ([&reverb] { return ! reverb.isVisible(); })
               && std::find (effects.getChain().begin(), effects.getChain().end(), Kind::reverb) == effects.getChain().end(),
           "a host taking the reverb out hides its card");

    // Scaled up: the same face at 2x.
    editor->setSize (2080, 1832);
    check (delay.getWidth() == 584 && approx ((float) delay.getDial (2).getWidth(), 104.0f, 1.0f), "at 2x the cards and dials double");
    check (delay.getBounds() == effects.boundsFor (Kind::delay), "each card where the area keeps it");
    check (hasDetail (render (*editor)), "renders at 2x");
    editor->setSize (1040, 916);

    editor->mouseDown (mouseAt (*editor, editor->tabBounds (Tab::bench).getCentre()));
    check (editor->getTab() == Tab::bench && bench.isVisible() && ! effects.isVisible(), "BENCH brings the bench back");
}

// The effects' layout as the processor keeps it: saved and restored, an
// older session brought over, a preset setting them out.
void testEffectsRack()
{
    std::printf ("Effects rack:\n");
    using Kind = Rack::Kind;

    Synth2AudioProcessor a;
    Rack::Model rack (a.getState());
    check (rack.add (Kind::reverb, Rack::defaultPosition (0)) && rack.add (Kind::drive, Rack::defaultPosition (1)),
           "effects go into the chain");
    check (! rack.add (Kind::reverb, Rack::defaultPosition (2)), "one of each");
    check (a.currentSettings().effects.order == Effects::Order { Kind::reverb, Kind::drive, Kind::delay },
           "the reverb, then the drive, as they read in");

    // A card brushing another slides off to the nearest free spot.
    const auto drivePlace = rack.positionOf (Kind::drive);
    const auto landed = rack.move (Kind::reverb, Rack::defaultPosition (0) + juce::Point<float> (Rack::cardWidth * 0.4f, 0.0f));
    check (! Rack::boundsAt (landed).intersects (rack.boundsOf (Kind::drive)) && rack.positionOf (Kind::drive) == drivePlace,
           "a card dropped partly over another slides off it, leaving it be");
    rack.move (Kind::reverb, Rack::defaultPosition (1));
    check (rack.positionOf (Kind::reverb) == drivePlace && rack.positionOf (Kind::drive) == landed,
           "dropped right on it, the two swap places");

    // A card moved to a lower row comes after the row above.
    rack.move (Kind::drive, { Rack::defaultPosition (0).x, Rack::areaHeight - Rack::cardHeight });
    rack.move (Kind::reverb, { Rack::defaultPosition (1).x, 0.0f });
    check (rack.readingOrder() == std::vector<Kind> { Kind::reverb, Kind::drive }
               && a.currentSettings().effects.order == Effects::Order { Kind::reverb, Kind::drive, Kind::delay },
           "a card well below the other starts a new row, after it, even to its left");

    juce::MemoryBlock saved;
    a.getStateInformation (saved);
    Synth2AudioProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    const Rack::Model rackB (b.getState());
    check (rackB.inChain() == std::vector<Kind> { Kind::drive, Kind::reverb } && rackB.positionOf (Kind::reverb) == rack.positionOf (Kind::reverb)
               && rackB.positionOf (Kind::drive) == rack.positionOf (Kind::drive),
           "a saved set comes back with the same effects, where they were");
    check (b.currentSettings().effects.order == a.currentSettings().effects.order, "playing in the same order");

    Synth2AudioProcessor fresh;
    fresh.getStateInformation (saved);
    Synth2AudioProcessor c;
    c.setStateInformation (saved.getData(), (int) saved.getSize());
    check (Rack::Model (c.getState()).isEmpty(), "an empty chain comes back empty");

    // A session from before the rack had all three on the tab, side by side
    // in the order they played: it opens as it was, sounding the same.
    for (const float order : { 0.0f, 1.0f })
    {
        auto older = fresh.getState().copyState().createCopy();
        older.removeChild (older.getChildWithName (Rack::rackType), nullptr);
        for (const auto kind : Rack::allEffects)
            older.removeChild (older.getChildWithProperty ("id", juce::String (Rack::inChainParameter (kind))), nullptr);
        juce::ValueTree parameter ("PARAM");
        parameter.setProperty ("id", ParamID::effectsOrder, nullptr);
        parameter.setProperty ("value", order, nullptr);
        older.appendChild (parameter, nullptr);
        older.getChildWithProperty ("id", juce::String (ParamID::reverbOn)).setProperty ("value", 1.0f, nullptr);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*older.createXml(), block);
        Synth2AudioProcessor d;
        d.setStateInformation (block.getData(), (int) block.getSize());
        const Rack::Model rackD (d.getState());
        const auto expected = order > 0.5f ? Effects::Order { Kind::drive, Kind::reverb, Kind::delay } : Effects::defaultOrder;
        check (rackD.inChain().size() == 3 && rackD.readingOrder() == std::vector<Kind> (expected.begin(), expected.end())
                   && d.currentSettings().effects.order == expected,
               order > 0.5f ? "an older set with the reverb first opens with all three, the reverb before the delay"
                            : "an older set opens with all three: the drive, the delay, the reverb");
        check (d.currentSettings().effects.reverb.on && ! d.currentSettings().effects.delay.on,
               "each switched on or off as it was");
    }

    // A preset puts just its effects in the chain, side by side.
    const auto presetNamed = [] (const char* name)
    {
        for (int i = 0; i < Presets::numPresets; ++i)
            if (juce::String (Presets::factory[(size_t) i].name) == name)
                return i;
        return 0;
    };
    a.loadPreset (presetNamed ("Supersaw Pad"));
    check (rack.inChain() == std::vector<Kind> { Kind::delay, Kind::reverb }
               && rack.positionOf (Kind::delay) == Rack::defaultPosition (0) && rack.positionOf (Kind::reverb) == Rack::defaultPosition (1),
           "a preset with a delay and a reverb puts those two in, side by side");
    check (a.currentSettings().effects.order == Effects::Order { Kind::delay, Kind::reverb, Kind::drive }
               && a.currentSettings().effects.delay.on && a.currentSettings().effects.reverb.on,
           "playing the delay, then the reverb");
    a.loadPreset (presetNamed ("Sub Bass"));
    check (rack.isEmpty() && ! a.currentSettings().effects.reverb.on, "one with none empties the chain");
    a.loadPreset (0);
    check (rack.isEmpty(), "and Empty Bench leaves it empty");
}

// ---- Presets ----------------------------------------------------------------------

// The loudest sample over `blocks` blocks of a chord held from the first.
float holdChordPeak (Synth2AudioProcessor& p, int blocks)
{
    juce::AudioBuffer<float> buffer (2, 512);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const int note : { 48, 60, 64, 67, 72 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        p.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 512));
    }
    return peak;
}

void testPresets()
{
    std::printf ("Presets:\n");

    Synth2AudioProcessor p;
    check (p.getNumPrograms() == Presets::numPresets && Presets::numPresets > 10, "the host sees the factory presets as programs");
    check (p.getCurrentProgram() == 0 && p.getProgramName (0) == "Empty Bench" && Bench::Model (p.getState()).isEmpty(),
           "a new instance is on the first, the empty bench");

    juce::StringArray names;
    for (int i = 0; i < p.getNumPrograms(); ++i)
        names.addIfNotAlreadyThere (p.getProgramName (i));
    check (names.size() == Presets::numPresets && ! names.contains ({}), "each has a name of its own");

    // Every preset loads its modules, set out without overlapping, each
    // envelope shaping its own oscillator, and plays a big chord at full
    // velocity loud enough, and without clipping.
    for (int i = 1; i < Presets::numPresets; ++i)
    {
        const auto& preset = Presets::factory[(size_t) i];
        Synth2AudioProcessor q;
        q.prepareToPlay (48000.0, 512);
        q.setCurrentProgram (i);
        const Bench::Model bench (q.getState());
        const auto settings = q.currentSettings();

        bool modulesMatch = true;
        bool shapedByOwn  = true;
        float attack = 0.0f;
        for (int slot = 0; slot < Bench::numOscillators; ++slot)
        {
            const auto& osc = preset.oscillators[(size_t) slot];
            const bool shaped = osc.has_value() && osc->envelope.has_value();
            modulesMatch = modulesMatch && bench.isPresent ({ Bench::Kind::oscillator, slot }) == osc.has_value()
                                        && bench.isPresent ({ Bench::Kind::envelope, slot }) == shaped;
            shapedByOwn  = shapedByOwn && settings.oscillators[(size_t) slot].envelope == (shaped ? slot : -1);
            if (shaped)
                attack = juce::jmax (attack, osc->envelope->delaySeconds + osc->envelope->attackSeconds);
        }

        bool apart = true;
        const auto present = bench.present();
        for (size_t a = 0; a < present.size(); ++a)
            for (size_t b = a + 1; b < present.size(); ++b)
                apart = apart && ! bench.boundsOf (present[a]).intersects (bench.boundsOf (present[b]));

        const float peak = holdChordPeak (q, juce::roundToInt ((attack + 0.5f) * 48000.0f / 512.0f));
        const std::string name = preset.name;
        check (q.getCurrentProgram() == i && modulesMatch && apart, name + ": its modules are on the bench, apart");
        check (shapedByOwn, name + ": each envelope shapes its own oscillator");
        check (peak > 0.2f && peak < 1.0f, name + ": a chord sounds without clipping (peak " + std::to_string (peak) + ")");
    }

    // The pads and keys go through a room, and some through the delay; the basses stay dry.
    const auto presetNamed = [&names] (const char* name) { return Presets::factory[(size_t) names.indexOf (name)]; };
    check (presetNamed ("Supersaw Pad").reverb.has_value() && presetNamed ("Glass Keys").reverb.has_value()
               && presetNamed ("Supersaw Pad").delay.has_value() && ! presetNamed ("Sub Bass").reverb.has_value(),
           "the pads and keys have reverb, the basses none");
    {
        Synth2AudioProcessor q;
        q.setCurrentProgram (names.indexOf ("Supersaw Pad"));
        check (param (q, ParamID::reverbOn) >= 0.5f && param (q, ParamID::delayOn) >= 0.5f && param (q, ParamID::delaySync) >= 0.5f,
               "loading one switches its effects on, the delay on the beat");
        q.setCurrentProgram (names.indexOf ("Sub Bass"));
        check (param (q, ParamID::reverbOn) < 0.5f && param (q, ParamID::delayOn) < 0.5f
                   && approx (param (q, ParamID::reverbSize), 0.5f, 1e-4f),
               "and the next one sets them back to their defaults");
    }

    // The plucks fall away while the key is still held.
    for (const auto* pluck : { "Saw Pluck", "Wood Mallet", "Chord Stab" })
    {
        Synth2AudioProcessor q;
        q.prepareToPlay (48000.0, 512);
        q.setCurrentProgram (names.indexOf (pluck));
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        const float struck = playAndMeasure (q, on, 2);
        const float held   = playAndMeasure (q, {}, 200);   // ~2 s more, the key still down
        check (struck > 0.02f && held < struck * 0.01f, std::string (pluck) + ": a held note dies away");
    }

    // Loading one resets what it doesn't set, and lays the bench out afresh.
    setParam (p, ParamID::osc1Voices, 9.0f);
    setParam (p, ParamID::osc3Voices, 5.0f);
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    Bench::Model bench (p.getState());
    bench.add (Bench::Kind::oscillator, { 700.0f, 200.0f });
    bench.add (Bench::Kind::envelope, { 0.0f, 400.0f });
    bench.connect ({ Bench::Kind::oscillator, 0 }, Bench::ModuleId { Bench::Kind::envelope, 0 });
    bench.connect ({ Bench::Kind::envelope, 0 });
    const int sub = names.indexOf ("Sub Bass");
    p.setCurrentProgram (sub);
    check (approx (param (p, ParamID::osc1Voices), 1.0f, 1e-6f) && approx (param (p, ParamID::osc3Voices), 1.0f, 1e-6f)
               && approx (param (p, ParamID::signalFlow), 0.0f, 1e-6f),
           "a preset puts back what it doesn't set (each stack's VOICES, the flow)");
    check (bench.cables().empty() && bench.positionOf ({ Bench::Kind::oscillator, 0 }) == juce::Point<float> (0.0f, 0.0f)
               && bench.positionOf ({ Bench::Kind::oscillator, 1 }) == juce::Point<float> (Bench::moduleWidth + Bench::gap, 0.0f)
               && bench.positionOf ({ Bench::Kind::envelope, 1 }) == Bench::defaultPosition ({ Bench::Kind::envelope, 1 }),
           "unplugs the cables and sets each envelope under its oscillator");

    // A host setting the loaded program again changes nothing; picking it on
    // the face loads it afresh.
    setParam (p, ParamID::osc1Level, 0.3f);
    p.setCurrentProgram (sub);
    check (approx (param (p, ParamID::osc1Level), 0.3f, 1e-4f), "a host setting the same program again keeps the edits");
    p.loadPreset (sub);
    check (approx (param (p, ParamID::osc1Level), 1.0f, 1e-4f), "loading it again puts it back");

    // Which preset is loaded is saved with the session.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    Synth2AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    check (restored.getCurrentProgram() == sub, "the loaded preset comes back with the session");
    // From off the message thread a program change waits for it; the latest
    // request wins, and a restored session cancels one still waiting.
    const auto fromAnotherThread = [&p] (std::initializer_list<int> programs)
    {
        std::thread host ([&p, programs] { for (const int program : programs) p.setCurrentProgram (program); });
        host.join();
    };
    fromAnotherThread ({ 1, sub });
    pump (50);
    check (p.getCurrentProgram() == sub && approx (param (p, ParamID::osc1Level), 1.0f, 1e-4f),
           "switching back to the loaded program off the message thread cancels the one queued before it");
    fromAnotherThread ({ 1 });
    pump (50);
    check (p.getCurrentProgram() == 1, "a program asked for off the message thread loads on it");
    p.loadPreset (sub);
    setParam (p, ParamID::osc1Level, 0.3f);
    p.getStateInformation (saved);
    fromAnotherThread ({ 2 });
    p.setStateInformation (saved.getData(), (int) saved.getSize());
    pump (50);
    check (p.getCurrentProgram() == sub && approx (param (p, ParamID::osc1Level), 0.3f, 1e-4f),
           "a restored session isn't overwritten by a program asked for before it");

    p.setCurrentProgram (-1);
    p.setCurrentProgram (Presets::numPresets);
    check (p.getCurrentProgram() == sub, "a program out of range is ignored");
    p.setCurrentProgram (0);
    check (Bench::Model (p.getState()).isEmpty() && approx (param (p, ParamID::output), 0.0f, 1e-4f), "Empty Bench clears it");

    // On the face: the picker's arrows step through them, its name opens them all.
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;
    const auto picker = editor->presetBounds();
    check (editor->getLocalBounds().toFloat().contains (picker) && picker.getBottom() < (float) editor->getBench().getY(),
           "the preset picker sits in the header");
    editor->mouseDown (mouseAt (*editor, { picker.getRight() - 6.0f, picker.getCentreY() }));
    check (p.getCurrentProgram() == 1 && ! Bench::Model (p.getState()).isEmpty(), "its right arrow loads the next preset");
    editor->mouseDown (mouseAt (*editor, { picker.getX() + 6.0f, picker.getCentreY() }));
    editor->mouseDown (mouseAt (*editor, { picker.getX() + 6.0f, picker.getCentreY() }));
    check (p.getCurrentProgram() == Presets::numPresets - 1, "its left arrow steps back, wrapping round");

    auto& tray = editor->getTrays().getPreset();
    editor->mouseDown (mouseAt (*editor, picker.getCentre()));
    check (tray.isOpen() && tray.getCaption() == "PRESETS" && tray.getNumChoices() == Presets::numPresets
               && tray.getChoice (Presets::numPresets - 1).ticked && ! tray.getChoice (0).ticked,
           "its name opens a tray of them all, the loaded one ticked");
    check (editor->getLocalBounds().toFloat().contains (tray.trayBounds()), "which fits on the face");
    pumpUntil ([&tray] { return tray.getAlpha() >= 1.0f; });
    snapshot (render (*editor), "9-presets");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (2).getCentre()));
    check (! tray.isOpen() && p.getCurrentProgram() == 2, "picking one loads it");
    auto& osc3 = editor->getBench().getModule ({ Bench::Kind::oscillator, 2 });
    check (pumpUntil ([&] { return osc3.isVisible() && osc3.getAlpha() >= 1.0f; }), "and its modules appear on the bench");
    snapshot (render (*editor), "10-preset-loaded");
}

// ---- The computer's keys (standalone) ---------------------------------------------
void testTypedKeys()
{
    std::printf ("Computer keys:\n");

    {
        Synth2AudioProcessor plugin;
        std::unique_ptr<juce::AudioProcessorEditor> base (plugin.createEditor());
        auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
        check (editor != nullptr && ! editor->playsTypedKeys() && ! editor->getWantsKeyboardFocus()
                   && ! editor->keyPressed (juce::KeyPress ('a', {}, 'a')),
               "in a host the computer's keys are left to the host");
    }

    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    Synth2AudioProcessor p;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    p.prepareToPlay (48000.0, 512);
    fillBench (p);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Synth2AudioProcessorEditor*> (base.get());
    if (editor == nullptr)
        return;
    auto& keyboard = editor->getKeyboard();
    check (editor->playsTypedKeys() && editor->getWantsKeyboardFocus(), "in the standalone app the editor takes the keys");

    std::set<char> down;
    const auto press = [&] (char c)
    {
        down.insert (c);
        keyboard.typedKeysChanged ([&down] (char key) { return down.count (key) > 0; });
        return editor->keyPressed (juce::KeyPress (c, {}, (juce::juce_wchar) c));
    };
    const auto letGo = [&] (char c)
    {
        down.erase (c);
        return keyboard.typedKeysChanged ([&down] (char key) { return down.count (key) > 0; });
    };

    check (press ('a') && p.getKeyboardState().isNoteOn (1, 60), "A plays middle C, and goes no further");
    check (playAndMeasure (p, {}, 8) > 0.05f, "and it sounds");
    press ('w');
    press (';');
    check (p.getKeyboardState().isNoteOn (1, 61) && p.getKeyboardState().isNoteOn (1, 76), "W the C sharp, ; the E above");
    letGo ('w');
    letGo (';');
    check (! p.getKeyboardState().isNoteOn (1, 61) && ! p.getKeyboardState().isNoteOn (1, 76), "letting go stops them");

    check (editor->keyPressed (juce::KeyPress ('z', {}, 'z')) && keyboard.getTypingC() == 48, "Z shifts down an octave");
    check (p.getKeyboardState().isNoteOn (1, 60), "leaving the note held as it was");
    letGo ('a');
    check (! p.getKeyboardState().isNoteOn (1, 60) && ! p.getKeyboardState().isNoteOn (1, 48), "letting A go after the shift stops the note it played");
    press ('a');
    check (p.getKeyboardState().isNoteOn (1, 48), "and now A plays C3");
    letGo ('a');
    for (int i = 0; i < 6; ++i)
        editor->keyPressed (juce::KeyPress ('x', {}, 'x'));
    check (keyboard.getTypingC() == NeonKeyboard::highestTypingC, "X shifts up, as far as C7");
    for (int i = 0; i < 9; ++i)
        editor->keyPressed (juce::KeyPress ('z', {}, 'z'));
    check (keyboard.getTypingC() == NeonKeyboard::lowestTypingC, "and Z down, as far as C1");
    editor->keyPressed (juce::KeyPress ('x', {}, 'x'));
    editor->keyPressed (juce::KeyPress ('x', {}, 'x'));
    editor->keyPressed (juce::KeyPress ('x', {}, 'x'));

    const auto modifiers = juce::ModifierKeys::currentModifiers;
    juce::ModifierKeys::currentModifiers = juce::ModifierKeys (juce::ModifierKeys::commandModifier);
    check (! editor->keyPressed (juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 's')), "a shortcut (Cmd+S) passes on");
    press ('s');
    check (! p.getKeyboardState().isNoteOn (1, 62), "and plays nothing");
    juce::ModifierKeys::currentModifiers = modifiers;
    letGo ('s');
    check (! editor->keyPressed (juce::KeyPress ('q', {}, 'q')), "a key that plays nothing passes on");

    press ('d');
    check (keyboard.holdsTypedNotes(), "a held key is a held note");
    down.clear();
    keyboard.typedKeysChanged ([] (char) { return false; });
    check (! keyboard.holdsTypedNotes() && ! p.getKeyboardState().isNoteOn (1, 64),
           "and keys found up later (let go in another window) are let go");
    releaseAll (p);
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::printf ("Synth2 plugin tests\n===================\n");

    testProcessorBasics();
    testBusLayouts();
    testPlaying();
    testStateRoundTrip();
    testPresets();
    testEditor();
    testBypass();
    testViews();
    testBenchZoom();
    testEffectsTab();
    testEffectsRack();
    testTypedKeys();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

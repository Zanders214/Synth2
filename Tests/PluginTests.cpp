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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <tuple>
#include <string>
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
    Bench::Model (p.getState()).adoptClassicLayout();
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

    for (const auto* id : { ParamID::wave, ParamID::voices, ParamID::detune, ParamID::width,
                            ParamID::attack, ParamID::release, ParamID::output,
                            ParamID::osc1Pitch, ParamID::osc1Fine, ParamID::osc1Level,
                            ParamID::osc2On, ParamID::osc2Wave, ParamID::osc2Pitch, ParamID::osc2Fine, ParamID::osc2Level,
                            ParamID::osc4Voices, ParamID::osc4Detune, ParamID::osc4Width,
                            ParamID::env1On, ParamID::env4On, ParamID::env4Attack, ParamID::env4Release })
        check (p.getState().getParameter (id) != nullptr, std::string ("has the '") + id + "' parameter");
    check (p.getState().getParameter (ParamID::unisonOn) == nullptr && p.getState().getParameter (ParamID::envelopeOn) == nullptr,
           "the first bench's shared unison and envelope switches are gone");

    check (approx (param (p, ParamID::wave), (float) Waveform::saw, 1e-6f), "starts on the saw");
    check (approx (param (p, ParamID::voices), 1.0f, 1e-6f), "starts with one voice");
    check (approx (param (p, ParamID::detune), 15.0f, 1e-4f) && approx (param (p, ParamID::width), 0.5f, 1e-6f),
           "DETUNE and WIDTH start at 15 ct and 50%, ready for when VOICES goes up");

    setParam (p, ParamID::release, 2.0f);
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

    auto* detune = p.getState().getParameter (ParamID::detune);
    check (detune->getText (detune->convertTo0to1 (12.5f), 32) == "12.5 ct", "DETUNE reads in cents");
    auto* attack = p.getState().getParameter (ParamID::attack);
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
    const auto modules = p.currentSettings();
    check (modules.oscillators[3].on && modules.oscillators[3].wave == Waveform::sine && modules.oscillators[3].voices == 5
               && approx (modules.oscillators[3].width, 0.25f, 1e-6f) && modules.oscillators[0].voices == 1
               && approx (modules.envelopes[3].attackSeconds, 0.5f, 0.001f),
           "OSC 4's own unison stack and ENV 4 reach the engine");

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
    setParam (p, ParamID::voices, 9.0f);
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
    setParam (a, ParamID::wave, (float) Waveform::square);
    setParam (a, ParamID::voices, 7.0f);
    setParam (a, ParamID::detune, 33.0f);
    setParam (a, ParamID::width, 0.8f);
    setParam (a, ParamID::attack, 0.5f);
    setParam (a, ParamID::release, 3.0f);
    setParam (a, ParamID::output, -4.0f);

    juce::MemoryBlock saved;
    a.getStateInformation (saved);

    Synth2AudioProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    check (approx (param (b, ParamID::wave), (float) Waveform::square, 1e-6f), "WAVE comes back");
    check (approx (param (b, ParamID::voices), 7.0f, 1e-6f), "VOICES comes back");
    check (approx (param (b, ParamID::detune), 33.0f, 0.01f), "DETUNE comes back");
    check (approx (param (b, ParamID::width), 0.8f, 0.001f), "WIDTH comes back");
    check (approx (param (b, ParamID::attack), 0.5f, 0.001f), "ATTACK comes back");
    check (approx (param (b, ParamID::release), 3.0f, 0.001f), "RELEASE comes back");
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
        for (const auto& osc : { ParamID::oscillators[1], ParamID::oscillators[2], ParamID::oscillators[3] })
            newer.insert (newer.end(), { osc.voices, osc.detune, osc.width });
        for (const auto& env : ParamID::envelopes)
            newer.insert (newer.end(), { env.on, env.attack, env.release });
        for (const auto& id : newer)
            if (id != ParamID::attack && id != ParamID::release)
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
    check (approx (param (*f, ParamID::voices), 1.0f, 1e-6f), "with unison off, each oscillator plays one voice");

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
    check (approx (param (b, ParamID::voices), 7.0f, 1e-6f), "a bad state is ignored");
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
    auto& tray = editor->getAddTray();
    const auto clickBench = [&] (juce::Point<float> at) { bench.mouseDown (mouseAt (bench, at)); };
    const auto addAt = [&] (int choice, juce::Point<float> at)
    {
        clickBench (at);
        tray.mouseDown (mouseAt (tray, tray.choiceBounds (choice).getCentre()));
    };

    clickBench ({ 150.0f, 40.0f });
    check (tray.isOpen() && tray.getTitle() == "ADD TO THE BENCH" && tray.getNumChoices() == 2, "a click on the bench opens the add tray");
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
    check (osc1.getTitle() == "OSC 1" && model.positionOf (osc1.getId()) == juce::Point<float> (32.0f, 25.0f)
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
    check (osc1.getRight() <= bench.getWidth() && model.positionOf (osc1.getId()).x <= Bench::areaWidth - Bench::moduleWidth,
           "a module can't be dragged off the bench");
    dragHeader (osc1, { -(float) osc1.getX() + 252.0f, 0.0f });

    // A module dropped on another slides off it to the nearest free spot.
    addAt (0, { 900.0f, 40.0f });
    auto& osc2 = bench.getOscillator (1);
    check (param (p, ParamID::osc2On) >= 0.5f && pumpUntil ([&osc2] { return osc2.isVisible(); }), "a second oscillator is OSC 2");
    check (juce::exactlyEqual (model.positionOf (osc2.getId()).x, Bench::areaWidth - Bench::moduleWidth), "kept inside the bench");
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
    check (pumpUntil ([&env1] { return env1.isVisible() && env1.getAlpha() >= 1.0f; }) && env1.getTitle() == "ENV 1"
               && env1.getBounds() == juce::Rectangle<int> (0, 352, 236, 240),
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
        return std::all_of (expected.begin(), expected.end(), [&] (const char* tag) { return bench.getEnvelope (e++).getTag() == tag; });
    };
    check (pumpUntil ([&] { return tagsAre ({ "> OSC 1", "> OSC 2", "> OSC 3", "> OSC 4" }); }),
           "each envelope shows the oscillator with its number");
    snapshot (render (*editor), "3-full-bench");
    setParam (p, ParamID::osc3On, 0.0f);
    check (pumpUntil ([&] { return bench.getEnvelope (2).isDimmed() && bench.getEnvelope (2).getTag().isEmpty(); })
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
    const float osc1Voices = param (p, ParamID::voices);
    osc3.getDial (3).setValue (5.0, juce::sendNotificationSync);
    osc3.getDial (4).setValue (40.0, juce::sendNotificationSync);
    check (approx (param (p, ParamID::osc3Voices), 5.0f, 1e-6f) && approx (param (p, ParamID::osc3Detune), 40.0f, 0.01f)
               && approx (param (p, ParamID::voices), osc1Voices, 1e-6f),
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
    check (approx (param (p, ParamID::wave), (float) Waveform::square, 1e-6f) && picker.getSelected() == 3,
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
               && approx (param (p, ParamID::wave), (float) Waveform::square, 1e-6f),
           "hovering TRIANGLE previews it in the well, changing nothing");
    menu.mouseDown (mouseAt (menu, menu.tileBounds (0).getCentre()));
    check (approx (param (p, ParamID::wave), (float) Waveform::sine, 1e-6f) && ! menu.isOpen(), "clicking SINE picks it and closes the tray");
    auto& osc4Picker = bench.getOscillator (3).getPicker();
    osc4Picker.mouseDown (mouseAt (osc4Picker, osc4Picker.nextArrow().getCentre()));
    check (approx (param (p, ParamID::osc4Wave), (float) Waveform::saw, 1e-6f) && approx (param (p, ParamID::wave), (float) Waveform::sine, 1e-6f),
           "OSC 4's arrows step its own shape (TRIANGLE to SAW), not OSC 1's");
    setParam (p, ParamID::wave, (float) Waveform::square);
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
    setParam (p, ParamID::attack, 0.5f);
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
    setParam (p, ParamID::attack, 0.005f);
    setParam (p, ParamID::env2Attack, 0.005f);

    // The gear picks how sound flows: following the layout numbers the modules,
    // and each envelope shapes the first oscillator before it without one.
    auto& settings = editor->getSettingsTray();
    editor->mouseDown (mouseAt (*editor, editor->gearBounds().getCentre()));
    check (settings.isOpen() && settings.getTitle() == "SIGNAL FLOW" && settings.getNumChoices() == 3
               && settings.getChoice (0).ticked && ! settings.getChoice (1).ticked,
           "the gear opens the signal-flow settings, Fixed order ticked");
    check (editor->getLocalBounds().toFloat().contains (settings.trayBounds()) && settings.trayBounds().getY() > editor->gearBounds().getBottom(),
           "under the gear");
    check (pumpUntil ([&settings] { return settings.getAlpha() >= 1.0f; }), "fading in");
    snapshot (render (*editor), "5-settings");
    settings.mouseDown (mouseAt (settings, settings.choiceBounds (1).getCentre()));
    check (! settings.isOpen() && approx (param (p, ParamID::signalFlow), 1.0f, 1e-6f), "picking Follow layout sets the flow");

    const auto orderIs = [&bench] (std::initializer_list<std::pair<Bench::ModuleId, int>> expected)
    {
        return std::all_of (expected.begin(), expected.end(), [&bench] (const auto& e) { return bench.getModule (e.first).getOrder() == e.second; });
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
    check (pumpUntil ([&] { return osc1.isJackShown() && ! osc1.isJackShown (Jack::in) && osc1.getOrder() == 0; })
               && env1.isJackShown (Jack::in) && env1.isJackShown (Jack::out) && env1.getTag().isEmpty(),
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
    const auto outputIn = editor->outputJackBounds().getCentre();

    const auto jack = osc1.jackBounds().getCentre();
    osc1.mouseDown (mouseAt (osc1, jack));
    osc1.mouseDrag (mouseAt (osc1, jack + juce::Point<float> (-80.0f, 300.0f), jack));
    check (editor->isPatching(), "dragging from OSC 1's OUT jack pulls a cable");
    snapshot (render (*editor), "7-patching");
    osc1.mouseUp (mouseAt (osc1, osc1.getLocalPoint (editor, editor->jackCentre (e1, Jack::in)), jack));
    check (! editor->isPatching() && model.inputOf (e1) == std::optional (o1), "dropped on ENV 1's IN jack, it's patched");
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
    setParam (p, ParamID::attack, 3.0f);
    check (holdAndMeasure (p) < 0.01f, "a slow ATTACK on ENV 1 fades OSC 1 in");
    releaseAll (p);
    drag (osc2, Jack::out, outputIn);
    check (model.isPatchedToOutput (o2) && holdAndMeasure (p) > 0.02f, "while OSC 2, straight to the output, sounds at once");
    releaseAll (p);
    setParam (p, ParamID::attack, 0.005f);
    snapshot (render (*editor), "8-cables");

    // Picking a cable up: by its oscillator, an envelope's IN or the output's IN.
    drag (osc1, Jack::out, { 400.0f, 860.0f });
    check (! model.inputOf (e1).has_value() && osc1.isDimmed() && env1.isDimmed(), "a cable dropped anywhere else is unplugged");
    drag (osc1, Jack::out, editor->jackCentre (e1, Jack::in));
    const auto envIn = env1.jackBounds (Jack::in).getCentre();
    env1.mouseDown (mouseAt (env1, envIn));
    check (editor->isPatching() && ! model.inputOf (e1).has_value(), "grabbing ENV 1's IN jack picks up the cable into it");
    env1.mouseUp (mouseAt (env1, envIn, envIn));
    check (model.inputOf (e1) == std::optional (o1), "and dropping it back plugs it in again");
    check (! model.connect (e1, e2) && model.inputOf (e2) == std::nullopt, "an envelope can't be patched into another");

    editor->mouseDown (mouseAt (*editor, outputIn));
    check (editor->isPatching() && ! model.isPatchedToOutput (o2) && model.isPatchedToOutput (e1),
           "grabbing the output's IN jack picks up the last cable patched to it");
    editor->mouseUp (mouseAt (*editor, outputIn + juce::Point<float> (-300.0f, -200.0f), outputIn));
    check (! editor->isPatching() && ! model.isPatchedToOutput (o2), "and dropping it away unplugs it");

    osc1.mouseDown (mouseAt (osc1, osc1.closeBounds().getCentre()));
    check (! model.inputOf (e1).has_value() && param (p, ParamID::osc1On) < 0.5f, "taking an oscillator off the bench unplugs its cable");
    env1.mouseDown (mouseAt (env1, env1.closeBounds().getCentre()));
    check (! model.isPatchedToOutput (e1) && param (p, ParamID::env1On) < 0.5f, "and taking an envelope off unplugs its own");
    releaseAll (p);

    // Back to a fixed order: no jacks, nothing greyed out.
    auto& osc4 = bench.getOscillator (3);
    setParam (p, ParamID::signalFlow, 0.0f);
    check (pumpUntil ([&] { return ! osc4.isJackShown() && ! osc4.isDimmed() && bench.getEnvelope (3).getTag() == "> OSC 4"; }),
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
                attack = juce::jmax (attack, osc->envelope->attackSeconds);
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

    // Loading one resets what it doesn't set, and lays the bench out afresh.
    setParam (p, ParamID::voices, 9.0f);
    setParam (p, ParamID::osc3Voices, 5.0f);
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    Bench::Model bench (p.getState());
    bench.add (Bench::Kind::oscillator, { 700.0f, 200.0f });
    bench.add (Bench::Kind::envelope, { 0.0f, 400.0f });
    bench.connect ({ Bench::Kind::oscillator, 0 }, Bench::ModuleId { Bench::Kind::envelope, 0 });
    bench.connect ({ Bench::Kind::envelope, 0 });
    const int sub = names.indexOf ("Sub Bass");
    p.setCurrentProgram (sub);
    check (approx (param (p, ParamID::voices), 1.0f, 1e-6f) && approx (param (p, ParamID::osc3Voices), 1.0f, 1e-6f)
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

    auto& tray = editor->getPresetTray();
    editor->mouseDown (mouseAt (*editor, picker.getCentre()));
    check (tray.isOpen() && tray.getTitle() == "PRESETS" && tray.getNumChoices() == Presets::numPresets
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

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

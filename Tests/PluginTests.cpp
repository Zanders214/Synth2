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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
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

// Puts OSC 1, unison and the envelope on the bench, as a session from before
// the bench opens, so a processor has something to play.
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
                            ParamID::osc2On, ParamID::osc2Wave, ParamID::osc2Pitch, ParamID::osc2Fine, ParamID::osc2Level })
        check (p.getState().getParameter (id) != nullptr, std::string ("has the '") + id + "' parameter");

    check (approx (param (p, ParamID::wave), (float) Waveform::saw, 1e-6f), "starts on the saw");
    check (approx (param (p, ParamID::voices), 1.0f, 1e-6f), "starts with one voice");
    check (approx (param (p, ParamID::detune), 15.0f, 1e-4f) && approx (param (p, ParamID::width), 0.5f, 1e-6f),
           "DETUNE and WIDTH start at 15 ct and 50%, ready for when VOICES goes up");

    setParam (p, ParamID::release, 2.0f);
    check (approx ((float) p.getTailLengthSeconds(), Synth2Engine::gateReleaseSeconds, 1e-4f), "without the envelope, the tail is a few ms");
    setParam (p, ParamID::envelopeOn, 1.0f);
    check (approx ((float) p.getTailLengthSeconds(), 2.0f, 0.01f), "with it, the tail is the release time");
    setParam (p, ParamID::envelopeOn, 0.0f);

    const auto settings = p.currentSettings();
    check (approx (settings.releaseSeconds, 2.0f, 0.01f) && settings.oscillators[0].wave == Waveform::saw,
           "the engine's settings come from the parameters");

    auto* detune = p.getState().getParameter (ParamID::detune);
    check (detune->getText (detune->convertTo0to1 (12.5f), 32) == "12.5 ct", "DETUNE reads in cents");
    auto* attack = p.getState().getParameter (ParamID::attack);
    check (attack->getText (attack->convertTo0to1 (0.25f), 32) == "250 ms"
               && attack->getText (attack->convertTo0to1 (1.5f), 32) == "1.50 s",
           "times read in ms below a second, s above");

    // The bench starts empty: no oscillator, no unison, no envelope. Each
    // oscillator has its own sound ready for when it's added: OSC 1 at the
    // note at full level, OSC 2 a square an octave down.
    bool empty = true;
    for (const auto& osc : ParamID::oscillators)
        empty = empty && param (p, osc.on) < 0.5f;
    check (empty && param (p, ParamID::unisonOn) < 0.5f && param (p, ParamID::envelopeOn) < 0.5f, "the bench starts empty");
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
    setParam (p, ParamID::unisonOn, 1.0f);
    setParam (p, ParamID::envelopeOn, 1.0f);
    const auto modules = p.currentSettings();
    check (modules.oscillators[3].on && modules.oscillators[3].wave == Waveform::sine && modules.unisonOn && modules.envelopeOn,
           "OSC 4 and the modules' switches reach the engine");

    // Following the cables, only an oscillator patched to the output reaches it.
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    Bench::Model bench (p.getState());
    check (! p.currentSettings().oscillators[1].routed, "with patch cables, an unpatched oscillator isn't routed");
    check (bench.connect ({ Bench::Kind::oscillator, 1 }) && p.currentSettings().oscillators[1].routed
               && ! p.currentSettings().oscillators[3].routed,
           "and patching it to the output routes it");
    check (! bench.connect ({ Bench::Kind::oscillator, 2 }), "an oscillator that isn't on the bench can't be patched");
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::fixed);
    check (p.currentSettings().oscillators[3].routed, "in a fixed order, every oscillator is");
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
    Bench::Model benchA (a.getState());
    benchA.add (Bench::Kind::oscillator, { 300.0f, 20.0f });
    benchA.add (Bench::Kind::oscillator, { 600.0f, 260.0f });
    benchA.add (Bench::Kind::envelope, { 0.0f, 0.0f });
    benchA.connect ({ Bench::Kind::oscillator, 1 });
    setParam (a, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    a.getStateInformation (saved);
    Synth2AudioProcessor c;
    c.setStateInformation (saved.getData(), (int) saved.getSize());
    const Bench::Model benchC (c.getState());
    check (benchC.isPresent ({ Bench::Kind::oscillator, 1 }) && benchC.isPresent ({ Bench::Kind::envelope, 0 })
               && ! benchC.isPresent ({ Bench::Kind::unison, 0 }) && benchC.present().size() == 3,
           "the modules on the bench come back");
    check (benchC.positionOf ({ Bench::Kind::oscillator, 0 }) == juce::Point<float> (300.0f, 20.0f)
               && benchC.positionOf ({ Bench::Kind::oscillator, 1 }) == juce::Point<float> (600.0f, 260.0f),
           "where they were");
    check (benchC.isPatchedToOutput ({ Bench::Kind::oscillator, 1 }) && ! benchC.isPatchedToOutput ({ Bench::Kind::oscillator, 0 }),
           "with their cables");
    check (c.currentSettings().oscillators[1].routed && ! c.currentSettings().oscillators[0].routed, "which the sound follows");

    Synth2AudioProcessor fresh;
    fresh.getStateInformation (saved);
    Synth2AudioProcessor d;
    d.setStateInformation (saved.getData(), (int) saved.getSize());
    check (Bench::Model (d.getState()).isEmpty(), "an empty bench comes back empty");

    // A session from before the bench opens as it looked then: OSC 1 (and OSC 2,
    // if it was on) along the top, unison and the envelope below.
    auto old = juce::ValueTree::fromXml (*juce::parseXML (a.getState().copyState().toXmlString()));
    old.removeChild (old.getChildWithName (Bench::benchType), nullptr);
    for (const auto* id : { ParamID::osc1On, ParamID::unisonOn, ParamID::envelopeOn, ParamID::signalFlow })
        old.removeChild (old.getChildWithProperty ("id", id), nullptr);
    for (const auto* id : { ParamID::osc1On, ParamID::osc2On })
        if (auto prm = old.getChildWithProperty ("id", id); prm.isValid())
            prm.setProperty ("value", id == ParamID::osc2On ? 1.0f : 0.0f, nullptr);
    juce::MemoryBlock oldSaved;
    juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldSaved);
    Synth2AudioProcessor e;
    e.setStateInformation (oldSaved.getData(), (int) oldSaved.getSize());
    const Bench::Model benchE (e.getState());
    check (benchE.isPresent ({ Bench::Kind::oscillator, 0 }) && benchE.isPresent ({ Bench::Kind::oscillator, 1 })
               && benchE.isPresent ({ Bench::Kind::unison, 0 }) && benchE.isPresent ({ Bench::Kind::envelope, 0 })
               && benchE.present().size() == 4,
           "a session from before the bench gets OSC 1, OSC 2 (it was on), unison and the envelope");
    check (benchE.positionOf ({ Bench::Kind::oscillator, 1 }) == Bench::defaultPosition ({ Bench::Kind::oscillator, 1 })
               && benchE.positionOf ({ Bench::Kind::unison, 0 }) == Bench::defaultPosition ({ Bench::Kind::unison, 0 }),
           "set out as the old panel was");
    check (approx (param (e, ParamID::voices), 7.0f, 1e-6f) && approx (param (e, ParamID::signalFlow), 0.0f, 1e-6f),
           "keeping its sound, in a fixed order");

    // One saved with the switches but before the layout keeps them as saved.
    auto between = juce::ValueTree::fromXml (*juce::parseXML (a.getState().copyState().toXmlString()));
    between.removeChild (between.getChildWithName (Bench::benchType), nullptr);
    for (const auto* id : { ParamID::osc1On, ParamID::unisonOn, ParamID::envelopeOn })
        between.getChildWithProperty ("id", id).setProperty ("value", juce::String (id) == ParamID::osc1On ? 1.0f : 0.0f, nullptr);
    juce::AudioProcessor::copyXmlToBinary (*between.createXml(), oldSaved);
    Synth2AudioProcessor f;
    f.setStateInformation (oldSaved.getData(), (int) oldSaved.getSize());
    const Bench::Model benchF (f.getState());
    check (benchF.isPresent ({ Bench::Kind::oscillator, 0 }) && ! benchF.isPresent ({ Bench::Kind::unison, 0 })
               && ! benchF.isPresent ({ Bench::Kind::envelope, 0 }),
           "one that saved its modules' switches, but no layout, keeps the switches as saved");
    check (benchF.positionOf ({ Bench::Kind::oscillator, 0 }) == Bench::defaultPosition ({ Bench::Kind::oscillator, 0 }),
           "and gets the old panel's layout");

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

    check (editor->getWidth() == 1040 && editor->getHeight() == 740, "opens at its 1040 x 740 design size");
    check (editor->isResizable(), "is resizable");

    // An empty bench, inviting a click, and the keyboard and OUTPUT below it.
    auto& bench = editor->getBench();
    bool noneShown = true;
    for (const auto& id : Bench::allModules)
        noneShown = noneShown && ! bench.getModule (id).isVisible();
    check (noneShown && bench.isShowingHint(), "opens on an empty bench, with a hint to click it");
    check (editor->getLocalBounds().contains (bench.getBounds()) && bench.getWidth() == 992 && bench.getHeight() == 512,
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
    check (tray.isOpen() && tray.getTitle() == "ADD TO THE BENCH" && tray.getNumChoices() == 3, "a click on the bench opens the add tray");
    check (tray.getChoice (0).name == "Oscillator" && tray.getChoice (0).detail.startsWith ("4 of 4 left")
               && tray.getChoice (1).name == "Unison" && tray.getChoice (2).name == "Envelope",
           "offering an oscillator (4 left), unison and the envelope");
    check (tray.getChoice (0).enabled && tray.getChoice (1).enabled && tray.getChoice (2).enabled, "all of them free");
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
               && osc1.getBounds() == juce::Rectangle<int> (32, 25, 236, 240),
           "under the click, its header where the click was");
    check (! bench.isShowingHint(), "and the hint goes");
    check (holdAndMeasure (p) > 0.02f, "a note now sounds");
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
    check (! Bench::boundsAt (landed).intersects (Bench::boundsAt (model.positionOf (osc1.getId())))
               && landed != juce::Point<float> (300.0f, 30.0f) && landed.getDistanceFrom ({ 300.0f, 30.0f }) < 200.0f,
           "dropped on OSC 1, OSC 2 slides off to the nearest free spot");
    check (pumpUntil ([&] { return osc2.getBounds() == bench.boundsFor (osc2.getId()); }) && ! anyOverlap (bench), "and glides there");

    // The cross takes a module off the bench.
    osc2.mouseDown (mouseAt (osc2, osc2.closeBounds().getCentre()));
    check (param (p, ParamID::osc2On) < 0.5f && pumpUntil ([&osc2] { return ! osc2.isVisible(); }), "the cross takes OSC 2 off the bench");

    // Fill the bench: a 4 x 2 grid, OSC 1 where it was.
    addAt (0, { 118.0f, 15.0f });     // OSC 2, back again, at (0, 0)
    addAt (0, { 118.0f, 271.0f });    // OSC 3 at (0, 256)
    addAt (0, { 622.0f, 15.0f });     // OSC 4 at (504, 0)
    clickBench ({ 370.0f, 271.0f });
    check (! tray.getChoice (0).enabled && tray.getChoice (0).detail == "All four are on the bench", "a fifth oscillator is greyed out");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (0).getCentre()));
    check (tray.isOpen(), "and picking it does nothing");
    tray.mouseDown (mouseAt (tray, tray.choiceBounds (1).getCentre()));   // unison at (252, 256)
    addAt (2, { 622.0f, 271.0f });                                         // the envelope at (504, 256)
    check (pumpUntil ([&] { return bench.getEnvelope().isVisible() && bench.getEnvelope().getAlpha() >= 1.0f; }),
           "unison and the envelope go on too");
    bool allShown = true;
    for (const auto& id : Bench::allModules)
        allShown = allShown && bench.getModule (id).isVisible();
    check (allShown && ! anyOverlap (bench), "six modules, none covering another");
    check (model.positionOf (bench.getOscillator (1).getId()) == juce::Point<float> (0.0f, 0.0f)
               && model.positionOf (bench.getUnison().getId()) == juce::Point<float> (252.0f, 256.0f),
           "each where it was put");
    clickBench ({ 870.0f, 271.0f });
    check (! tray.getChoice (0).enabled && ! tray.getChoice (1).enabled && ! tray.getChoice (2).enabled, "now the tray has nothing left to add");
    tray.close();
    snapshot (render (*editor), "3-full-bench");

    // Dials in the modules drive their parameters, and the wells follow.
    auto& unison = bench.getUnison();
    unison.getDial (0).setValue (5.0, juce::sendNotificationSync);
    unison.getDial (1).setValue (40.0, juce::sendNotificationSync);
    check (approx (param (p, ParamID::voices), 5.0f, 1e-6f) && approx (param (p, ParamID::detune), 40.0f, 0.01f),
           "UNISON's dials set VOICES and DETUNE");
    check (pumpUntil ([&unison] { return unison.getDisplay().getState().voices == 5; }), "and its well shows the stack");
    auto& osc3 = bench.getOscillator (2);
    osc3.getDial (0).setValue (12.0, juce::sendNotificationSync);
    osc3.getDial (2).setValue (0.5, juce::sendNotificationSync);
    check (approx (param (p, ParamID::osc3Pitch), 12.0f, 1e-6f) && approx (param (p, ParamID::osc3Level), 0.5f, 1e-6f),
           "OSC 3's dials set its own PITCH and LEVEL");
    check (pumpUntil ([&osc3] { return approx (osc3.getDisplay().getOsc().level, 0.5f, 1e-6f) && osc3.getDisplay().getOsc().cycles > 3.9f; }),
           "and its well shows twice the cycles, half as tall");

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

    // The envelope's well: a dot rides the envelope for each note.
    auto& amp = bench.getEnvelope().getDisplay();
    setParam (p, ParamID::attack, 0.5f);
    check (pumpUntil ([&amp] { return approx (amp.getAmpState().attackSeconds, 0.5f, 0.001f); }), "the ENVELOPE well shows ATTACK");
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

    // The gear picks how sound flows: following the layout numbers the modules.
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
                          o4 { Bench::Kind::oscillator, 3 }, un { Bench::Kind::unison, 0 }, env { Bench::Kind::envelope, 0 };
    check (orderIs ({ { o2, 1 }, { o1, 2 }, { o4, 3 }, { o3, 4 }, { un, 5 }, { env, 6 } }),
           "each module shows its place, row by row, left to right");
    snapshot (render (*editor), "6-follow-layout");
    dragHeader (bench.getOscillator (3), { 252.0f, 256.0f });
    check (pumpUntil ([&] { return orderIs ({ { o2, 1 }, { o1, 2 }, { o3, 3 }, { un, 4 }, { env, 5 }, { o4, 6 } }); }),
           "moving OSC 4 to the end of the second row renumbers them");

    // Patch cables: the oscillators get OUT jacks, the output an IN jack, and
    // only an oscillator patched to the output is heard.
    editor->mouseDown (mouseAt (*editor, editor->gearBounds().getCentre()));
    settings.mouseDown (mouseAt (settings, settings.choiceBounds (2).getCentre()));
    check (approx (param (p, ParamID::signalFlow), 2.0f, 1e-6f), "picking Patch cables sets the flow");
    check (pumpUntil ([&] { return osc1.isJackShown() && osc1.getOrder() == 0; }) && ! unison.isJackShown(),
           "the oscillators show OUT jacks (unison doesn't), and no numbers");
    bool allDimmed = true;
    for (int slot = 0; slot < Bench::numOscillators; ++slot)
        allDimmed = allDimmed && bench.getOscillator (slot).isDimmed();
    check (allDimmed && ! unison.isDimmed() && ! bench.getEnvelope().isDimmed(), "with nothing patched, every oscillator is greyed out");
    check (holdAndMeasure (p) < 1e-4f, "and silent");
    const auto unpatched = render (*editor);

    const auto jack = osc1.jackBounds().getCentre();
    const auto inJack = osc1.getLocalPoint (editor, editor->outputJackBounds().getCentre());
    osc1.mouseDown (mouseAt (osc1, jack));
    osc1.mouseDrag (mouseAt (osc1, jack + juce::Point<float> (40.0f, 200.0f), jack));
    check (editor->isPatching(), "dragging from OSC 1's OUT jack pulls a cable");
    snapshot (render (*editor), "7-patching");
    osc1.mouseUp (mouseAt (osc1, inJack, jack));
    check (! editor->isPatching() && model.isPatchedToOutput (o1), "dropped on the output's IN jack, it's patched");
    check (! osc1.isDimmed() && bench.getOscillator (1).isDimmed(), "OSC 1 lights up, the others stay grey");
    check (holdAndMeasure (p) > 0.02f, "and is heard");
    check (pumpUntil ([&] { return pixelsChanged (unpatched, render (*editor)) > 200; }), "the cable hangs between the jacks");
    snapshot (render (*editor), "8-cables");

    osc1.mouseDown (mouseAt (osc1, jack));
    osc1.mouseUp (mouseAt (osc1, jack + juce::Point<float> (0.0f, 120.0f), jack));
    check (! model.isPatchedToOutput (o1) && osc1.isDimmed(), "a cable dropped anywhere else is unplugged");
    check (holdAndMeasure (p) < 1e-3f, "and OSC 1 goes quiet");

    osc1.mouseDown (mouseAt (osc1, jack));
    osc1.mouseUp (mouseAt (osc1, inJack, jack));
    auto& osc4 = bench.getOscillator (3);
    const auto jack4 = osc4.jackBounds().getCentre();
    osc4.mouseDown (mouseAt (osc4, jack4));
    osc4.mouseUp (mouseAt (osc4, osc4.getLocalPoint (editor, editor->outputJackBounds().getCentre()), jack4));
    check (model.isPatchedToOutput (o1) && model.isPatchedToOutput (o4), "more than one oscillator can be patched to the output");

    const auto in = editor->outputJackBounds().getCentre();
    editor->mouseDown (mouseAt (*editor, in));
    check (editor->isPatching() && ! model.isPatchedToOutput (o4) && model.isPatchedToOutput (o1),
           "grabbing the IN jack picks up the last cable patched to it");
    editor->mouseUp (mouseAt (*editor, in + juce::Point<float> (-300.0f, -200.0f), in));
    check (! editor->isPatching() && ! model.isPatchedToOutput (o4), "and dropping it away unplugs it");

    osc1.mouseDown (mouseAt (osc1, osc1.closeBounds().getCentre()));
    check (! model.isPatchedToOutput (o1) && param (p, ParamID::osc1On) < 0.5f, "taking an oscillator off the bench unplugs its cable");
    releaseAll (p);

    // Back to a fixed order: no jacks, nothing greyed out.
    setParam (p, ParamID::signalFlow, 0.0f);
    check (pumpUntil ([&] { return ! osc4.isJackShown() && ! osc4.isDimmed(); }), "a host change of the flow shows too");

    // A session restored while the editor is open moves the cards there.
    juce::MemoryBlock layoutSaved;
    p.getStateInformation (layoutSaved);
    model.move (o4, { 756.0f, 0.0f });
    check (pumpUntil ([&] { return osc4.getPosition() == juce::Point<int> (756, 0); }),
           "a module moved on the bench (by undo, say) moves on screen");
    p.setStateInformation (layoutSaved.getData(), (int) layoutSaved.getSize());
    check (pumpUntil ([&] { return osc4.getPosition() == juce::Point<int> (756, 256); }) && osc4.getBounds() == bench.boundsFor (o4),
           "and restoring a session puts the cards back where it had them");

    // Scaled up: the same face at 2x.
    editor->setSize (2080, 1480);
    check (osc4.getWidth() == 472 && approx ((float) osc4.getDial (0).getWidth(), 104.0f, 1.0f), "at 2x every module and dial doubles");
    check (osc4.getPosition() == (model.positionOf (o4) * 2.0f).toInt(), "and the modules keep their places");
    check (hasDetail (render (*editor)), "renders at 2x");

    // The aspect ratio is held when a host (or the corner) resizes it.
    if (auto* constrainer = editor->getConstrainer())
        constrainer->setBoundsForComponent (editor, { 0, 0, 1300, 800 }, false, false, true, true);
    const float aspect = (float) editor->getWidth() / (float) editor->getHeight();
    check (approx (aspect, 1040.0f / 740.0f, 0.02f), "resizing keeps the design's proportions");
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
    testEditor();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

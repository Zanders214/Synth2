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
#include "ParamIDs.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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
    check (approx ((float) p.getTailLengthSeconds(), 2.0f, 0.01f), "the tail is the release time");

    const auto settings = p.currentSettings();
    check (approx (settings.releaseSeconds, 2.0f, 0.01f) && settings.oscillators[0].wave == Waveform::saw,
           "the engine's settings come from the parameters");

    auto* detune = p.getState().getParameter (ParamID::detune);
    check (detune->getText (detune->convertTo0to1 (12.5f), 32) == "12.5 ct", "DETUNE reads in cents");
    auto* attack = p.getState().getParameter (ParamID::attack);
    check (attack->getText (attack->convertTo0to1 (0.25f), 32) == "250 ms"
               && attack->getText (attack->convertTo0to1 (1.5f), 32) == "1.50 s",
           "times read in ms below a second, s above");

    // The oscillators: OSC 1 plays as before, OSC 2 waits switched off,
    // set up as a square an octave down.
    check (approx (param (p, ParamID::osc1Pitch), 0.0f, 1e-6f) && approx (param (p, ParamID::osc1Fine), 0.0f, 1e-6f)
               && approx (param (p, ParamID::osc1Level), 1.0f, 1e-6f),
           "OSC 1 starts at the note, full level");
    check (param (p, ParamID::osc2On) < 0.5f, "OSC 2 starts switched off");
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
    setParam (p, ParamID::unisonOn, 0.0f);
    const auto modules = p.currentSettings();
    check (modules.oscillators[3].on && modules.oscillators[3].wave == Waveform::sine && ! modules.unisonOn && modules.envelopeOn,
           "OSC 4 and the modules' switches reach the engine");

    // Following the cables, only an oscillator patched to the output reaches it.
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::cables);
    p.setRoutedToOutput (1, false);
    const auto patched = p.currentSettings();
    check (! patched.oscillators[1].routed && patched.oscillators[0].routed, "with patch cables, an unpatched oscillator isn't routed");
    setParam (p, ParamID::signalFlow, (float) Synth2AudioProcessor::SignalFlow::fixed);
    check (p.currentSettings().oscillators[1].routed, "in a fixed order, every oscillator is");
    p.setRoutedToOutput (1, true);
    setParam (p, ParamID::osc4On, 0.0f);
    setParam (p, ParamID::unisonOn, 1.0f);
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

    // Junk is ignored rather than resetting the sound.
    const char junk[] = "not a synth state";
    b.setStateInformation (junk, (int) sizeof (junk));
    check (approx (param (b, ParamID::voices), 7.0f, 1e-6f), "a bad state is ignored");
}

// ---- The editor ------------------------------------------------------------------
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

    check (editor->getWidth() == 480 && editor->getHeight() == 612, "opens at its 480 x 612 design size");
    check (editor->isResizable(), "is resizable");

    const auto first = render (*editor);
    check (hasDetail (first), "renders its face");

    // Big enough to grab: every dial in view (OSC 2's wait until it is added)
    // and the keyboard.
    bool dialsFit = true;
    int  inView   = 0;
    for (int i = 0; i < Synth2AudioProcessorEditor::numDials; ++i)
    {
        if (! editor->getDial (i).isVisible())
            continue;
        ++inView;
        dialsFit = dialsFit && editor->getDial (i).getWidth() >= 40 && editor->getLocalBounds().contains (editor->getDial (i).getBounds());
    }
    check (dialsFit && inView == 9, "the nine dials in view are laid out inside the panel");
    check (editor->getLocalBounds().contains (editor->getKeyboard().getBounds()) && editor->getKeyboard().getHeight() > 80,
           "the keyboard fits along the bottom");

    // Dials drive their parameters.
    editor->getDial (6).setValue (5.0, juce::sendNotificationSync);
    check (approx (param (p, ParamID::voices), 5.0f, 1e-6f), "the VOICES dial sets VOICES");
    editor->getDial (7).setValue (40.0, juce::sendNotificationSync);
    check (approx (param (p, ParamID::detune), 40.0f, 0.01f), "the DETUNE dial sets DETUNE");

    // The displays follow the parameters on the editor's timer.
    const auto& unison = editor->getUnisonDisplay();
    check (pumpUntil ([&unison] { return unison.getState().voices == 5 && approx (unison.getState().detune, 40.0f, 0.01f); }),
           "the UNISON well shows the stack");
    const auto spread = render (*editor);
    check (pixelsChanged (first, spread) > 200, "and redraws when the stack changes");

    // The wave picker, laid over the WAVE well: its arrows step through the
    // shapes and wrap round, the wheel flips through them, a click opens the tray.
    auto& picker = editor->getWavePicker();
    auto& menu   = editor->getWaveMenu();
    auto& well   = editor->getWaveDisplay();
    check (picker.getBounds() == well.getBounds() && editor->getLocalBounds().contains (picker.getBounds()),
           "OSC 1's picker sits over its well");
    check (picker.getSelected() == 2 && picker.pillBounds().getWidth() > 80.0f
               && picker.getLocalBounds().toFloat().contains (picker.pillBounds()),
           "its pill shows SAW, the default, in the well's corner");

    const auto clickAt = [] (juce::Component& target, juce::Point<float> at)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    picker.mouseDown (clickAt (picker, picker.nextArrow().getCentre()));
    check (approx (param (p, ParamID::wave), (float) Waveform::square, 1e-6f) && picker.getSelected() == 3,
           "the right arrow steps SAW to SQUARE");
    picker.mouseDown (clickAt (picker, picker.nextArrow().getCentre()));
    check (picker.getSelected() == 0, "and on round to SINE");
    picker.mouseDown (clickAt (picker, picker.previousArrow().getCentre()));
    check (picker.getSelected() == 3, "the left arrow steps back, round to SQUARE");
    check (! menu.isOpen(), "the arrows don't open the tray");

    juce::MouseWheelDetails wheel {};
    wheel.deltaY = -0.25f;   // one notch down
    picker.mouseWheelMove (clickAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 0, "scrolling down over the well flips to the next shape");
    wheel.deltaY = 0.05f;
    picker.mouseWheelMove (clickAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 0, "a nudge too small to count doesn't");
    wheel.deltaY = 0.25f;
    picker.mouseWheelMove (clickAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 3, "scrolling up flips back");

    // A trackpad with natural scrolling: many small, reversed deltas add up.
    wheel.deltaY     = 0.05f;
    wheel.isReversed = true;
    for (int i = 0; i < 3; ++i)
        picker.mouseWheelMove (clickAt (picker, { 40.0f, 60.0f }), wheel);
    check (picker.getSelected() == 0, "small reversed trackpad deltas gather into a step");
    picker.step (-1);
    wheel.isReversed = false;

    // A morph, not a jump, then the new shape.
    check (pumpUntil ([&well] { return well.getState().wave == Waveform::square; }), "the WAVE well follows the pick");
    check (well.getShownWave() == Waveform::square, "and shows the new shape");
    check (pumpUntil ([&well] { return ! well.isMorphing(); }), "morphing into it, briefly");

    picker.mouseDown (clickAt (picker, { 60.0f, 60.0f }));
    check (menu.isOpen() && menu.getBounds() == editor->getLocalBounds(), "a click on the well opens the tray over the panel");
    bool tilesFit = true;
    for (int i = 0; i < WaveMenu::numWaves; ++i)
        tilesFit = tilesFit && menu.tileBounds (i).getWidth() > 60.0f && menu.trayBounds().contains (menu.tileBounds (i))
                && menu.tileBounds (i).getY() > (float) well.getBottom();
    check (tilesFit, "its four tiles drop from under the well");
    check (pumpUntil ([&menu] { return menu.getAlpha() >= 1.0f; }), "fading in");
    const auto trayShown = render (*editor);

    juce::MouseEvent hover (clickAt (menu, menu.tileBounds (1).getCentre()));
    menu.mouseMove (hover);
    check (menu.getHighlighted() == 1 && well.getShownWave() == Waveform::triangle, "hovering TRIANGLE previews it in the well");
    check (approx (param (p, ParamID::wave), (float) Waveform::square, 1e-6f), "without changing WAVE");
    check (pixelsChanged (trayShown, render (*editor)) > 50, "the hovered tile lights up");
    menu.mouseExit (hover);
    check (well.getShownWave() == Waveform::square, "leaving the tiles puts the chosen shape back");

    menu.mouseDown (clickAt (menu, menu.tileBounds (0).getCentre()));
    check (approx (param (p, ParamID::wave), (float) Waveform::sine, 1e-6f) && picker.getSelected() == 0,
           "clicking the SINE tile picks the sine");
    check (! menu.isOpen(), "and closes the tray");

    picker.mouseDown (clickAt (picker, { 60.0f, 60.0f }));
    menu.mouseDown (clickAt (menu, { 10.0f, (float) editor->getHeight() - 10.0f }));
    check (! menu.isOpen() && picker.getSelected() == 0, "a click outside the tiles closes it, changing nothing");

    picker.mouseDown (clickAt (picker, { 60.0f, 60.0f }));
    menu.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    menu.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (menu.getHighlighted() == 2, "the arrow keys walk along the tiles");
    menu.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    check (picker.getSelected() == 2 && ! menu.isOpen(), "and Return picks");
    picker.mouseDown (clickAt (picker, { 60.0f, 60.0f }));
    menu.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (! menu.isOpen() && picker.getSelected() == 2, "Escape closes it");

    // Host automation reaches the picker too.
    setParam (p, ParamID::wave, (float) Waveform::square);
    check (pumpUntil ([&picker] { return picker.getSelected() == 3; }), "a host change of WAVE shows SQUARE in the pill");
    check (pumpUntil ([&well] { return well.getShownWave() == Waveform::square; }), "and in the well");

    // Keyboard: middle C is on it, and the keys the host plays light up on it
    // without being played a second time.
    auto& keyboard = editor->getKeyboard();
    const auto keyArea = keyboard.getRectangleForKey (60);
    check (! keyArea.isEmpty() && keyboard.getLocalBounds().toFloat().contains (keyArea.getCentre()), "middle C is on the keyboard");

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

    // Each group's display sits right above its three dials.
    const auto above = [] (juce::Component& display, juce::Slider& firstDial, juce::Slider& lastDial)
    {
        return display.getBottom() <= firstDial.getY() && display.getX() <= firstDial.getX()
            && display.getRight() >= lastDial.getRight();
    };
    check (above (editor->getWaveDisplay (0), editor->getDial (0), editor->getDial (2)), "the OSC 1 well sits over its PITCH, FINE and LEVEL");
    check (above (editor->getUnisonDisplay(), editor->getDial (6), editor->getDial (8)), "the UNISON well sits over VOICES, DETUNE and WIDTH");
    check (above (editor->getAmpDisplay(), editor->getDial (9), editor->getDial (11)), "the AMP well sits over ATTACK, RELEASE and OUTPUT");
    check (editor->getWaveDisplay (0).getBottom() < editor->getUnisonDisplay().getY(), "the oscillators come first, the voice below");

    // OSC 2 is out of sight until it is added: OSC 1 takes the whole row, with
    // a "+ OSC 2" chip in its well.
    auto& osc2     = editor->getWavePicker (1);
    auto& osc2Well = editor->getWaveDisplay (1);
    auto& osc1Well = editor->getWaveDisplay (0);
    auto& addOsc2  = editor->getAddOsc2Chip();
    check (osc2.hasPowerSwitch() && ! editor->getWavePicker (0).hasPowerSwitch(), "OSC 2 has a power switch, OSC 1 doesn't");
    check (! editor->isOsc2Shown() && ! osc2Well.isVisible() && ! osc2.isVisible()
               && ! editor->getDial (3).isVisible() && ! editor->getDial (4).isVisible() && ! editor->getDial (5).isVisible(),
           "while OSC 2 is off, its well and dials are hidden");
    check (osc1Well.getWidth() == editor->getUnisonDisplay().getWidth() * 2 + (editor->getAmpDisplay().getX() - editor->getUnisonDisplay().getRight()),
           "and OSC 1's well spans the whole row");
    check (editor->getDial (2).getRight() > editor->getAmpDisplay().getX(), "its dials spread across it");
    check (addOsc2.isVisible() && osc1Well.getBounds().contains (addOsc2.getBounds())
               && ! addOsc2.getBounds().toFloat().intersects (osc1Well.getBounds().toFloat().withTrimmedLeft ((float) osc1Well.getWidth() - 130.0f)),
           "with a + OSC 2 chip in its corner, clear of its pill");
    const auto osc2Off = render (*editor);

    const auto clickOn = [] (juce::Component& target, juce::Point<float> at)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    addOsc2.mouseDown (clickOn (addOsc2, addOsc2.getLocalBounds().getCentre().toFloat()));
    check (param (p, ParamID::osc2On) >= 0.5f && osc2.isOn() && editor->isOsc2Shown(), "the chip adds OSC 2");
    check (pumpUntil ([&] { return osc2Well.isVisible() && osc2Well.getAlpha() >= 1.0f && editor->getDial (5).getAlpha() >= 1.0f; }),
           "which fades in with its dials");
    check (! addOsc2.isVisible(), "and the chip goes");
    check (above (osc2Well, editor->getDial (3), editor->getDial (5)) && osc2Well.getX() > osc1Well.getRight(),
           "OSC 2's well sits on the right, over its own dials");
    check (osc1Well.getWidth() == editor->getUnisonDisplay().getWidth(), "and OSC 1 moves over to the left half");
    check (pixelsChanged (osc2Off, render (*editor)) > 200, "visibly");
    check (! editor->getWaveMenu (1).isOpen() && ! editor->getWaveMenu (0).isOpen(), "without opening a tray");

    const float osc1Wave = param (p, ParamID::wave);
    osc2.mouseDown (clickOn (osc2, osc2.nextArrow().getCentre()));
    check (approx (param (p, ParamID::osc2Wave), (float) Waveform::sine, 1e-6f) && approx (param (p, ParamID::wave), osc1Wave, 1e-6f),
           "OSC 2's arrows step its own shape (SQUARE round to SINE), not OSC 1's");
    check (pumpUntil ([&osc2Well] { return osc2Well.getShownWave() == Waveform::sine; }), "and its well follows");

    osc2.mouseDown (clickOn (osc2, { 60.0f, 60.0f }));
    auto& osc2Menu = editor->getWaveMenu (1);
    check (osc2Menu.isOpen() && osc2Menu.trayBounds().getX() <= (float) editor->getWaveDisplay (0).getX()
               && osc2Menu.trayBounds().getRight() >= (float) osc2Well.getRight(),
           "OSC 2's tray opens across both wells");
    osc2Menu.mouseDown (clickOn (osc2Menu, osc2Menu.tileBounds (2).getCentre()));
    check (approx (param (p, ParamID::osc2Wave), (float) Waveform::saw, 1e-6f) && ! osc2Menu.isOpen(), "and picks OSC 2's shape");

    // PITCH and LEVEL show in the well: an octave up, twice the cycles.
    editor->getDial (3).setValue (12.0, juce::sendNotificationSync);
    editor->getDial (5).setValue (0.5, juce::sendNotificationSync);
    check (approx (param (p, ParamID::osc2Pitch), 12.0f, 1e-6f) && approx (param (p, ParamID::osc2Level), 0.5f, 1e-6f),
           "OSC 2's dials set its PITCH and LEVEL");
    check (pumpUntil ([&osc2Well] { return approx (osc2Well.getOsc().cycles, 4.0f, 1e-3f) && approx (osc2Well.getOsc().level, 0.5f, 1e-6f); }),
           "its well shows twice the cycles, half as tall");

    osc2.mouseDown (clickOn (osc2, osc2.powerBounds().getCentre()));
    check (param (p, ParamID::osc2On) < 0.5f, "its power chip switches OSC 2 off");
    check (pumpUntil ([&] { return ! editor->isOsc2Shown(); }) && ! osc2Well.isVisible() && ! editor->getDial (3).isVisible()
               && addOsc2.isVisible() && osc1Well.getRight() > editor->getAmpDisplay().getX(),
           "and it goes out of sight again, OSC 1 taking the row back");

    setParam (p, ParamID::osc2On, 1.0f);
    check (pumpUntil ([&] { return editor->isOsc2Shown() && osc2Well.isVisible(); }), "the host switching it on brings it back too");
    setParam (p, ParamID::osc2On, 0.0f);
    check (pumpUntil ([&] { return ! editor->isOsc2Shown(); }), "and off takes it away");

    // The AMP well follows its dials, and a dot rides the envelope for each note.
    playAndMeasure (p, {}, 100);      // let the earlier tests' notes fade out
    setParam (p, ParamID::attack, 0.5f);
    setParam (p, ParamID::output, -6.0f);
    const auto& amp = editor->getAmpDisplay();
    check (pumpUntil ([&amp] { return approx (amp.getAmpState().attackSeconds, 0.5f, 0.001f)
                                   && approx (amp.getAmpState().outputDb, -6.0f, 0.01f); }),
           "the AMP well shows ATTACK and OUTPUT");
    const auto beforeNotes = render (*editor);

    juce::MidiBuffer chordOn;
    for (const int note : { 60, 64 })
        chordOn.addEvent (juce::MidiMessage::noteOn (2, note, 0.8f), 0);
    playAndMeasure (p, chordOn, 4);   // ~40 ms into a 500 ms attack
    check (pumpUntil ([&amp] { return amp.getAmpState().numSounding() == 2; }), "two notes, two dots");
    const auto& rising = amp.getAmpState();
    bool climbing = true;
    for (const auto& note : rising.notes)
        if (note.stage != NoteActivity::Stage::idle)
            climbing = climbing && note.stage == NoteActivity::Stage::attack && note.level > 0.0f && note.level < 0.5f;
    check (climbing, "early in the attack, they are partway up the rise");
    check (pixelsChanged (beforeNotes, render (*editor)) > 20, "and they show in the AMP well");

    // Every sounding note is in `stage` (and there are two of them).
    const auto allIn = [&amp] (NoteActivity::Stage stage)
    {
        const auto& state = amp.getAmpState();
        bool all = state.numSounding() == 2;
        for (const auto& note : state.notes)
            if (note.stage != NoteActivity::Stage::idle)
                all = all && note.stage == stage;
        return all;
    };

    playAndMeasure (p, {}, 60);       // past the attack
    check (pumpUntil ([&] { return allIn (NoteActivity::Stage::hold); }), "with the keys down, they rest on the top");

    juce::MidiBuffer chordOff;
    for (const int note : { 60, 64 })
        chordOff.addEvent (juce::MidiMessage::noteOff (2, note), 0);
    playAndMeasure (p, chordOff, 4);
    check (pumpUntil ([&] { return allIn (NoteActivity::Stage::release); }), "let go, they release");
    bool falling = true;
    for (const auto& note : amp.getAmpState().notes)
        if (note.stage != NoteActivity::Stage::idle)
            falling = falling && note.level < note.releaseFrom;
    check (falling, "and walk down from where they were");
    playAndMeasure (p, {}, 100);
    check (pumpUntil ([&amp] { return amp.getAmpState().numSounding() == 0; }), "and leave once the notes have faded");
    setParam (p, ParamID::attack, 0.005f);
    setParam (p, ParamID::output, 0.0f);

    // Scaled up: the same face at 2x.
    editor->setSize (960, 1224);
    check (approx ((float) editor->getDial (0).getWidth() / 2.0f, 52.0f, 1.0f), "at 2x every control doubles");
    const auto doubled = render (*editor);
    check (hasDetail (doubled), "renders at 2x");

    // The aspect ratio is held when a host (or the corner) resizes it.
    if (auto* constrainer = editor->getConstrainer())
        constrainer->setBoundsForComponent (editor, { 0, 0, 720, 600 }, false, false, true, true);
    const float aspect = (float) editor->getWidth() / (float) editor->getHeight();
    check (approx (aspect, 480.0f / 612.0f, 0.02f), "resizing keeps the design's proportions");
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

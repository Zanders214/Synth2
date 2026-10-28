#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace
{
    // The MIDI channel the on-screen keyboard plays on.
    constexpr int keyboardChannel = 1;
}

Synth2AudioProcessor::Synth2AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    keyboardState.addListener (this);
}

Synth2AudioProcessor::~Synth2AudioProcessor()
{
    keyboardState.removeListener (this);
}

juce::AudioProcessorValueTreeState::ParameterLayout Synth2AudioProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percentFromValue = [] (float value, int) { return String (roundToInt (value * 100.0f)) + " %"; };
    const auto timeFromValue    = [] (float value, int)
    {
        return value < 1.0f ? String (roundToInt (value * 1000.0f)) + " ms"
                            : String (value, 2) + " s";
    };

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::wave, 1 }, "Wave",
        StringArray { "Sine", "Triangle", "Saw", "Square" }, (int) Waveform::saw));

    // Unison: how many copies play each note, how far apart they are tuned and
    // how wide they spread (see Unison.h).
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamID::voices, 1 }, "Voices", 1, Unison::maxVoices, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::detune, 1 }, "Detune",
        NormalisableRange<float> (0.0f, Unison::maxDetuneCents), 15.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int) { return String (value, 1) + " ct"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::width, 1 }, "Width",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percentFromValue)));

    // The amp envelope, in seconds. Skewed so the short times, where the ear
    // is fussiest, get most of the knob.
    NormalisableRange<float> attackRange (0.001f, 5.0f);
    attackRange.setSkewForCentre (0.2f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::attack, 1 }, "Attack", attackRange, 0.005f,
        AudioParameterFloatAttributes().withStringFromValueFunction (timeFromValue)));

    NormalisableRange<float> releaseRange (0.005f, 10.0f);
    releaseRange.setSkewForCentre (0.5f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::release, 1 }, "Release", releaseRange, 0.3f,
        AudioParameterFloatAttributes().withStringFromValueFunction (timeFromValue)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

std::array<NoteActivity, Synth2Engine::maxNotes> Synth2AudioProcessor::getNoteActivity() const noexcept
{
    std::array<NoteActivity, Synth2Engine::maxNotes> activity {};
    for (size_t i = 0; i < activity.size(); ++i)
    {
        activity[i].stage       = (NoteActivity::Stage) noteActivity[i].stage.load (std::memory_order_relaxed);
        activity[i].level       = noteActivity[i].level.load (std::memory_order_relaxed);
        activity[i].releaseFrom = noteActivity[i].releaseFrom.load (std::memory_order_relaxed);
    }
    return activity;
}

Synth2Engine::Settings Synth2AudioProcessor::currentSettings() const noexcept
{
    const auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    Synth2Engine::Settings settings;
    settings.wave           = (Waveform) juce::jlimit (0, 3, juce::roundToInt (param (ParamID::wave)));
    settings.voices         = juce::roundToInt (param (ParamID::voices));
    settings.detuneCents    = param (ParamID::detune);
    settings.width          = param (ParamID::width);
    settings.attackSeconds  = param (ParamID::attack);
    settings.releaseSeconds = param (ParamID::release);
    settings.outputDb       = param (ParamID::output);
    return settings;
}

void Synth2AudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setSettings (currentSettings());
    engine.prepare (sampleRate);
}

void Synth2AudioProcessor::releaseResources()
{
    // Nothing to free: the engine's voices are fixed-size and live as long as
    // the processor, so there is no per-session allocation to release here.
}

bool Synth2AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

void Synth2AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    engine.setSettings (currentSettings());
    playOnScreenKeys();
    engine.process (buffer, midi);
    activeNotes.store (engine.getNumActiveNotes());

    const auto activity = engine.getActivity();
    for (size_t i = 0; i < activity.size(); ++i)
    {
        noteActivity[i].stage      .store ((int) activity[i].stage, std::memory_order_relaxed);
        noteActivity[i].level      .store (activity[i].level,       std::memory_order_relaxed);
        noteActivity[i].releaseFrom.store (activity[i].releaseFrom, std::memory_order_relaxed);
    }

    const auto held = engine.getHeldNotes();
    hostHeldNotes[0].store (held[0], std::memory_order_relaxed);
    hostHeldNotes[1].store (held[1], std::memory_order_relaxed);

    // An instrument makes no MIDI of its own.
    midi.clear();
}

// ---- The on-screen keyboard ---------------------------------------------------------

void Synth2AudioProcessor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (showingHostNotes)
        return;   // only lighting a key the host plays, not playing it again

    keysHeldByMouse.set ((size_t) midiNoteNumber);
    if (const auto scope = keyFifo.write (1); scope.blockSize1 > 0)
        keyEvents[(size_t) scope.startIndex1] = { midiNoteNumber, juce::jmax (0.01f, velocity) };
}

void Synth2AudioProcessor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (showingHostNotes)
        return;

    keysHeldByMouse.reset ((size_t) midiNoteNumber);
    if (const auto scope = keyFifo.write (1); scope.blockSize1 > 0)
        keyEvents[(size_t) scope.startIndex1] = { midiNoteNumber, 0.0f };
}

// Audio thread: plays the keys pressed since the last block, at its start.
void Synth2AudioProcessor::playOnScreenKeys() noexcept
{
    const auto scope = keyFifo.read (keyFifo.getNumReady());
    const auto play  = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& key = keyEvents[(size_t) i];
            if (key.velocity > 0.0f)
                engine.noteOn (keyboardChannel, key.note, key.velocity);
            else
                engine.noteOff (keyboardChannel, key.note);
        }
    };
    play (scope.startIndex1, scope.blockSize1);
    play (scope.startIndex2, scope.blockSize2);
}

void Synth2AudioProcessor::showHostNotesOnKeyboard()
{
    const std::array<std::uint64_t, 2> held { hostHeldNotes[0].load (std::memory_order_relaxed),
                                              hostHeldNotes[1].load (std::memory_order_relaxed) };

    const juce::ScopedValueSetter<bool> lighting (showingHostNotes, true);
    for (int note = 0; note < 128; ++note)
    {
        if (keysHeldByMouse.test ((size_t) note))
            continue;

        const bool hostHolds = ((held[(size_t) note / 64] >> (note % 64)) & 1u) != 0;
        if (hostHolds != keyboardState.isNoteOn (1, note))
        {
            if (hostHolds)
                keyboardState.noteOn (1, note, 0.8f);
            else
                keyboardState.noteOff (1, note, 0.0f);
        }
    }
}

juce::AudioProcessorEditor* Synth2AudioProcessor::createEditor()
{
    return new Synth2AudioProcessorEditor (*this);   // NOSONAR(cpp:S5025): JUCE's API hands ownership of a raw pointer to the host wrapper
}

bool Synth2AudioProcessor::hasEditor() const            { return true; }

const juce::String Synth2AudioProcessor::getName() const { return JucePlugin_Name; }   // NOSONAR(cpp:S5951): signature fixed by juce::AudioProcessor

bool Synth2AudioProcessor::acceptsMidi() const          { return true; }
bool Synth2AudioProcessor::producesMidi() const         { return false; }
bool Synth2AudioProcessor::isMidiEffect() const         { return false; }

// Once the last key goes up, the notes ring on for the release time.
double Synth2AudioProcessor::getTailLengthSeconds() const
{
    return apvts.getRawParameterValue (ParamID::release)->load();
}

int Synth2AudioProcessor::getNumPrograms()              { return 1; }
int Synth2AudioProcessor::getCurrentProgram()           { return 0; }
// A single unnamed program: there are no presets to switch to or rename.
void Synth2AudioProcessor::setCurrentProgram (int)      { /* only program 0 exists */ }
const juce::String Synth2AudioProcessor::getProgramName (int) { return {}; }   // NOSONAR(cpp:S5951): signature fixed by juce::AudioProcessor
void Synth2AudioProcessor::changeProgramName (int, const juce::String&) { /* the one program has no name */ }

void Synth2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Synth2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)   // NOSONAR(cpp:S5008): signature fixed by juce::AudioProcessor
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Synth2AudioProcessor();   // NOSONAR(cpp:S5025): JUCE's plugin entry point returns an owning raw pointer
}

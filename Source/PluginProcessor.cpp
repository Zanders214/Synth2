#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Bench.h"

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
    Bench::Model (apvts).ensureBench();
    apvts.state.addListener (this);
    followCables();
}

Synth2AudioProcessor::~Synth2AudioProcessor()
{
    apvts.state.removeListener (this);
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

    const StringArray waves { "Sine", "Triangle", "Saw", "Square" };
    const auto semitonesFromValue = [] (int value, int) { return (value > 0 ? "+" : "") + String (value) + " st"; };
    const auto centsFromValue     = [] (float value, int) { return (value > 0.0f ? "+" : "") + String (value, 1) + " ct"; };

    // The four oscillators: each on the bench or not, a shape, a transposition,
    // a fine tune and a level. The bench starts empty, so all four start off.
    // (OSC 1's shape keeps its original "wave" ID, and OSC 2 its version 2
    // IDs, so older sessions load as they were.)
    const auto onOff = [] (bool automatable) { return AudioParameterBoolAttributes().withAutomatable (automatable); };
    const auto addOscillator = [&] (int index, int version)
    {
        const auto& ids      = ParamID::oscillators[index];
        const auto  defaults = Synth2Engine::Settings {}.oscillators[(size_t) index];
        const String name    = "Osc " + String (index + 1) + " ";

        // Whether an oscillator is on decides whether it is on the bench, so
        // it is part of the patch rather than something to automate (OSC 2's
        // switch was automatable before the bench, and stays so).
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ids.on, index == 1 ? 2 : 3 }, name + "On", false, onOff (index == 1)));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ids.wave, index == 0 ? 1 : version }, name + "Wave", waves, (int) defaults.wave));
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.pitch, version }, name + "Pitch", -Synth2Engine::maxSemitones, Synth2Engine::maxSemitones, defaults.semitones,
            AudioParameterIntAttributes().withStringFromValueFunction (semitonesFromValue)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.fine, version }, name + "Fine",
            NormalisableRange<float> (-Synth2Engine::maxFineCents, Synth2Engine::maxFineCents, 0.1f), defaults.fineCents,
            AudioParameterFloatAttributes().withStringFromValueFunction (centsFromValue)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.level, version }, name + "Level",
            NormalisableRange<float> (0.0f, 1.0f), defaults.level,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentFromValue)));
    };

    addOscillator (0, 2);
    addOscillator (1, 2);
    addOscillator (2, 3);
    addOscillator (3, 3);

    // Whether the unison stack and the envelope are on the bench (not until
    // they are added), and how sound flows between the modules.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::unisonOn, 3 },   "Unison On",   false, onOff (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::envelopeOn, 3 }, "Envelope On", false, onOff (false)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::signalFlow, 3 }, "Signal Flow", StringArray { "Fixed order", "Follow layout", "Patch cables" },
        (int) SignalFlow::fixed, AudioParameterChoiceAttributes().withAutomatable (false)));

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
    NormalisableRange attackRange (0.001f, 5.0f);
    attackRange.setSkewForCentre (0.2f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::attack, 1 }, "Attack", attackRange, 0.005f,
        AudioParameterFloatAttributes().withStringFromValueFunction (timeFromValue)));

    NormalisableRange releaseRange (0.005f, 10.0f);
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
        activity[i].stage       = (NoteActivity::Stage) noteActivity[i].stage.load();
        activity[i].level       = noteActivity[i].level.load();
        activity[i].releaseFrom = noteActivity[i].releaseFrom.load();
    }
    return activity;
}

Synth2Engine::Settings Synth2AudioProcessor::currentSettings() const noexcept
{
    const auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    Synth2Engine::Settings settings;
    const bool cables = juce::roundToInt (param (ParamID::signalFlow)) == (int) SignalFlow::cables;
    for (size_t k = 0; k < settings.oscillators.size(); ++k)
    {
        const auto& ids  = ParamID::oscillators[k];
        auto& oscillator = settings.oscillators[k];
        oscillator.on        = param (ids.on) >= 0.5f;
        oscillator.routed    = ! cables || routedToOutput[k].load (std::memory_order_relaxed);
        oscillator.wave      = (Waveform) juce::jlimit (0, 3, juce::roundToInt (param (ids.wave)));
        oscillator.semitones = juce::roundToInt (param (ids.pitch));
        oscillator.fineCents = param (ids.fine);
        oscillator.level     = param (ids.level);
    }
    settings.unisonOn       = param (ParamID::unisonOn) >= 0.5f;
    settings.voices         = juce::roundToInt (param (ParamID::voices));
    settings.detuneCents    = param (ParamID::detune);
    settings.width          = param (ParamID::width);
    settings.envelopeOn     = param (ParamID::envelopeOn) >= 0.5f;
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
        noteActivity[i].stage      .store ((int) activity[i].stage);
        noteActivity[i].level      .store (activity[i].level);
        noteActivity[i].releaseFrom.store (activity[i].releaseFrom);
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

    const juce::ScopedValueSetter lighting (showingHostNotes, true);
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
    if (apvts.getRawParameterValue (ParamID::envelopeOn)->load() < 0.5f)
        return Synth2Engine::gateReleaseSeconds;
    return apvts.getRawParameterValue (ParamID::release)->load();
}

void Synth2AudioProcessor::setRoutedToOutput (int oscillator, bool routed) noexcept
{
    routedToOutput[(size_t) juce::jlimit (0, Synth2Engine::numOscillators - 1, oscillator)].store (routed, std::memory_order_relaxed);
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

    // A session from before the bench had OSC 1, unison and the envelope
    // always there: set them out on the bench as they looked then.
    // (replaceState shares the tree and fills in the parameters it lacks,
    // so what was saved is read from a copy.)
    const auto restored = juce::ValueTree::fromXml (*xml);
    const auto saved    = restored.createCopy();
    apvts.replaceState (restored);

    Bench::Model bench (apvts);
    if (! Bench::Model::hasBench (saved))
        bench.adoptClassicLayout (saved);
    bench.ensureBench();
    followCables();
}

// The cables patched to the output decide which oscillators reach it when the
// sound follows them. Message thread, whenever the bench changes.
void Synth2AudioProcessor::followCables()
{
    const Bench::Model bench (apvts);
    for (int slot = 0; slot < Synth2Engine::numOscillators; ++slot)
        setRoutedToOutput (slot, bench.isPatchedToOutput ({ Bench::Kind::oscillator, slot }));
}

void Synth2AudioProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (Bench::benchType))
        followCables();
}

void Synth2AudioProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (Bench::benchType))
        followCables();
}

void Synth2AudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    followCables();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Synth2AudioProcessor();   // NOSONAR(cpp:S5025): JUCE's plugin entry point returns an owning raw pointer
}

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Bench.h"
#include "Presets.h"

namespace
{
    // The MIDI channel the on-screen keyboard plays on.
    constexpr int keyboardChannel = 1;

    // Which factory preset is loaded, kept on the state tree.
    const juce::Identifier presetProperty { "preset" };
}

Synth2AudioProcessor::Synth2AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    keyboardState.addListener (this);
    Bench::Model (apvts).ensureBench();
    apvts.state.addListener (this);
    for (const auto& id : Bench::allModules)
        apvts.addParameterListener (Bench::parameterFor (id), this);
    followBench();
}

Synth2AudioProcessor::~Synth2AudioProcessor()
{
    cancelPendingUpdate();
    for (const auto& id : Bench::allModules)
        apvts.removeParameterListener (Bench::parameterFor (id), this);
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
    // a fine tune, a level and its own unison stack: how many copies play each
    // note, how far apart they are tuned and how wide they spread (see
    // Unison.h). The bench starts empty, so all four start off. (OSC 1's shape
    // and stack keep their original IDs, and OSC 2 its version 2 IDs, so
    // older sessions load as they were.)
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

        const int    stackVersion = index == 0 ? 1 : 4;
        const String stackName    = index == 0 ? String() : name;
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.voices, stackVersion }, stackName + "Voices", 1, Unison::maxVoices, defaults.voices));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.detune, stackVersion }, stackName + "Detune",
            NormalisableRange<float> (0.0f, Unison::maxDetuneCents), defaults.detuneCents,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return String (value, 1) + " ct"; })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.width, stackVersion }, stackName + "Width",
            NormalisableRange<float> (0.0f, 1.0f), defaults.width,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentFromValue)));
    };

    addOscillator (0, 2);
    addOscillator (1, 2);
    addOscillator (2, 3);
    addOscillator (3, 3);

    // The four envelopes: each on the bench or not, its times in seconds,
    // skewed so the short times, where the ear is fussiest, get most of the
    // knob, and its sustain level. (ENV 1's attack and release keep their
    // original IDs; the DAHDSR stages came in version 5, set so that older
    // sessions sound as they did: no delay or hold, sustaining at the top.)
    const auto addEnvelope = [&] (int index)
    {
        const auto& ids      = ParamID::envelopes[index];
        const auto  defaults = Synth2Engine::Settings {}.envelopes[(size_t) index];
        const String name    = index == 0 ? String() : "Env " + String (index + 1) + " ";
        const int    version = index == 0 ? 1 : 4;

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ids.on, 4 }, "Env " + String (index + 1) + " On", false, onOff (false)));

        const String stageName = "Env " + String (index + 1) + " ";
        const auto time = [&] (const char* id, int idVersion, const String& timeName, float from, float to, float centre, float value)
        {
            NormalisableRange range (from, to);
            range.setSkewForCentre (centre);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id, idVersion }, timeName, range, value,
                AudioParameterFloatAttributes().withStringFromValueFunction (timeFromValue)));
        };
        time (ids.delay,   5,       stageName + "Delay",   0.0f,   5.0f,  0.2f, defaults.delaySeconds);
        time (ids.attack,  version, name + "Attack",       0.001f, 5.0f,  0.2f, defaults.attackSeconds);
        time (ids.hold,    5,       stageName + "Hold",    0.0f,   5.0f,  0.2f, defaults.holdSeconds);
        time (ids.decay,   5,       stageName + "Decay",   0.001f, 10.0f, 0.5f, defaults.decaySeconds);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.sustain, 5 }, stageName + "Sustain", NormalisableRange<float> (0.0f, 1.0f), defaults.sustainLevel,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentFromValue)));
        time (ids.release, version, name + "Release",      0.005f, 10.0f, 0.5f, defaults.releaseSeconds);
    };

    for (int index = 0; index < Synth2Engine::numEnvelopes; ++index)
        addEnvelope (index);

    // How sound flows between the modules.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::signalFlow, 3 }, "Signal Flow", StringArray { "Fixed order", "Follow layout", "Patch cables" },
        (int) SignalFlow::fixed, AudioParameterChoiceAttributes().withAutomatable (false)));

    // The effects after the mix (version 6), both off to start with, so
    // older sessions sound as they did.
    const Effects::Settings effects;
    const auto percent = [&] (const char* id, const String& effectName, float to, float value)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 6 }, effectName, NormalisableRange<float> (0.0f, to), value,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentFromValue)));
    };
    StringArray notes;
    for (const auto& division : Effects::divisions)
        notes.add (division.name);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayOn, 6 }, "Delay On", effects.delay.on));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delaySync, 6 }, "Delay Sync", effects.delay.sync));
    NormalisableRange<float> delayRange (Effects::minTimeMs, Effects::maxTimeMs, 1.0f);
    delayRange.setSkewForCentre (300.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::delayTime, 6 }, "Delay Time", delayRange, effects.delay.timeMs,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float value, int) { return String (roundToInt (value)) + " ms"; })));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::delayNote, 6 }, "Delay Note", notes, effects.delay.division));
    percent (ParamID::delayFeedback, "Delay Feedback", Effects::maxFeedback, effects.delay.feedback);
    percent (ParamID::delayTone,     "Delay Tone",     1.0f, effects.delay.tone);
    percent (ParamID::delayMix,      "Delay Mix",      1.0f, effects.delay.mix);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayPingPong, 6 }, "Delay Ping-Pong", effects.delay.pingPong));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::reverbOn, 6 }, "Reverb On", effects.reverb.on));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::reverbSize, 6 }, "Reverb Size", NormalisableRange<float> (0.0f, 1.0f), effects.reverb.size,
        AudioParameterFloatAttributes().withStringFromValueFunction ([timeFromValue] (float value, int maximumLength)
        {
            return timeFromValue ((float) Effects::reverbSeconds (value), maximumLength);
        })));
    percent (ParamID::reverbDamping, "Reverb Damping", 1.0f, effects.reverb.damping);
    percent (ParamID::reverbWidth,   "Reverb Width",   1.0f, effects.reverb.width);
    percent (ParamID::reverbMix,     "Reverb Mix",     1.0f, effects.reverb.mix);
    NormalisableRange<float> preDelayRange (0.0f, Effects::maxPreDelayMs, 1.0f);
    preDelayRange.setSkewForCentre (40.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::reverbPreDelay, 6 }, "Reverb Pre-Delay", preDelayRange, effects.reverb.preDelayMs,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float value, int) { return String (roundToInt (value)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::effectsOrder, 6 }, "Effects Order", StringArray { "Delay, then reverb", "Reverb, then delay" },
        (int) effects.reverbFirst, AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

std::array<NoteActivity, Synth2Engine::maxNotes> Synth2AudioProcessor::getNoteActivity (int osc) const noexcept
{
    const auto& published = noteActivity[(size_t) juce::jlimit (0, Synth2Engine::numOscillators - 1, osc)];
    std::array<NoteActivity, Synth2Engine::maxNotes> activity {};
    for (size_t i = 0; i < activity.size(); ++i)
    {
        activity[i].stage       = (NoteActivity::Stage) published[i].stage.load();
        activity[i].level       = published[i].level.load();
        activity[i].progress = published[i].progress.load();
    }
    return activity;
}

Synth2Engine::Settings Synth2AudioProcessor::currentSettings() const noexcept
{
    const auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    Synth2Engine::Settings settings;
    const int  flow   = juce::jlimit (0, 2, juce::roundToInt (param (ParamID::signalFlow)));
    const bool cables = flow == (int) SignalFlow::cables;
    for (size_t k = 0; k < settings.oscillators.size(); ++k)
    {
        const auto& ids  = ParamID::oscillators[k];
        auto& oscillator = settings.oscillators[k];
        oscillator.on          = param (ids.on) >= 0.5f;
        oscillator.routed      = ! cables || routedToOutput[k].load (std::memory_order_relaxed);
        oscillator.wave        = (Waveform) juce::jlimit (0, 3, juce::roundToInt (param (ids.wave)));
        oscillator.semitones   = juce::roundToInt (param (ids.pitch));
        oscillator.fineCents   = param (ids.fine);
        oscillator.level       = param (ids.level);
        oscillator.voices      = juce::roundToInt (param (ids.voices));
        oscillator.detuneCents = param (ids.detune);
        oscillator.width       = param (ids.width);
        oscillator.envelope    = envelopeFor[(size_t) flow][k].load (std::memory_order_relaxed);
    }
    for (size_t e = 0; e < settings.envelopes.size(); ++e)
    {
        const auto& ids = ParamID::envelopes[e];
        auto& envelope  = settings.envelopes[e];
        envelope.delaySeconds   = param (ids.delay);
        envelope.attackSeconds  = param (ids.attack);
        envelope.holdSeconds    = param (ids.hold);
        envelope.decaySeconds   = param (ids.decay);
        envelope.sustainLevel   = param (ids.sustain);
        envelope.releaseSeconds = param (ids.release);
    }
    auto& delay = settings.effects.delay;
    delay.on       = param (ParamID::delayOn) >= 0.5f;
    delay.sync     = param (ParamID::delaySync) >= 0.5f;
    delay.timeMs   = param (ParamID::delayTime);
    delay.division = juce::roundToInt (param (ParamID::delayNote));
    delay.feedback = param (ParamID::delayFeedback);
    delay.tone     = param (ParamID::delayTone);
    delay.mix      = param (ParamID::delayMix);
    delay.pingPong = param (ParamID::delayPingPong) >= 0.5f;

    auto& reverb = settings.effects.reverb;
    reverb.on      = param (ParamID::reverbOn) >= 0.5f;
    reverb.size    = param (ParamID::reverbSize);
    reverb.damping = param (ParamID::reverbDamping);
    reverb.width   = param (ParamID::reverbWidth);
    reverb.mix     = param (ParamID::reverbMix);
    reverb.preDelayMs = param (ParamID::reverbPreDelay);

    settings.effects.reverbFirst = param (ParamID::effectsOrder) >= 0.5f;
    settings.effects.bpm         = hostBpm.load (std::memory_order_relaxed);
    settings.outputDb = param (ParamID::output);
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

    // A synced delay follows the host's tempo (or keeps the last one it had).
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);

    engine.setSettings (currentSettings());
    playOnScreenKeys();
    engine.process (buffer, midi);
    activeNotes.store (engine.getNumActiveNotes());

    for (size_t k = 0; k < noteActivity.size(); ++k)
    {
        const auto activity = engine.getActivity ((int) k);
        for (size_t i = 0; i < activity.size(); ++i)
        {
            noteActivity[k][i].stage   .store ((int) activity[i].stage);
            noteActivity[k][i].level   .store (activity[i].level);
            noteActivity[k][i].progress.store (activity[i].progress);
        }
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

// Once the last key goes up, the notes ring on for the longest release of the
// envelopes shaping the oscillators on the bench (or a moment, with none),
// and then the delay's repeats and the reverb's ring.
double Synth2AudioProcessor::getTailLengthSeconds() const
{
    const auto settings = currentSettings();
    double tail = Synth2Engine::gateReleaseSeconds;
    for (const auto& oscillator : settings.oscillators)
        if (oscillator.on && oscillator.envelope >= 0)
            tail = juce::jmax (tail, (double) settings.envelopes[(size_t) oscillator.envelope].releaseSeconds);
    return tail + Effects::tailSeconds (settings.effects);
}

void Synth2AudioProcessor::setRoutedToOutput (int oscillator, bool routed) noexcept
{
    routedToOutput[(size_t) juce::jlimit (0, Synth2Engine::numOscillators - 1, oscillator)].store (routed, std::memory_order_relaxed);
}

// The host's programs are the factory presets.
int Synth2AudioProcessor::getNumPrograms()              { return Presets::numPresets; }
int Synth2AudioProcessor::getCurrentProgram()           { return currentPreset.load(); }

// The latest request wins: one for the preset already loaded cancels any
// still waiting for the message thread rather than being ignored.
void Synth2AudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= Presets::numPresets)
        return;
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        pendingPreset.store (-1);
        if (index != currentPreset.load())
            loadPreset (index);
        return;
    }
    if (index == currentPreset.load())
    {
        pendingPreset.store (-1);
        return;
    }
    pendingPreset.store (index);
    triggerAsyncUpdate();
}

const juce::String Synth2AudioProcessor::getProgramName (int index)   // NOSONAR(cpp:S5951): signature fixed by juce::AudioProcessor
{
    return juce::isPositiveAndBelow (index, Presets::numPresets) ? juce::String (Presets::factory[(size_t) index].name) : juce::String();
}

void Synth2AudioProcessor::changeProgramName (int, const juce::String&) { /* the factory presets keep their names */ }

void Synth2AudioProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, Presets::numPresets))
        return;
    Presets::apply (apvts, Presets::factory[(size_t) index]);
    currentPreset.store (index);
    apvts.state.setProperty (presetProperty, index, nullptr);
    followBench();
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}


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

    // A program a host asked for before this session arrived must not load
    // over it once the message thread gets to it.
    pendingPreset.store (-1);

    // An older session is brought up to date (see Bench::Model::migrate).
    // (replaceState shares the tree and fills in the parameters it lacks,
    // so what was saved is read from a copy.)
    const auto restored = juce::ValueTree::fromXml (*xml);
    const auto saved    = restored.createCopy();
    apvts.replaceState (restored);
    Bench::Model (apvts).migrate (saved);
    currentPreset.store (juce::jlimit (0, Presets::numPresets - 1, (int) saved.getProperty (presetProperty, 0)));
    followBench();
}

void Synth2AudioProcessor::followBench()
{
    const Bench::Model bench (apvts);
    for (const auto flow : { SignalFlow::fixed, SignalFlow::layout, SignalFlow::cables })
    {
        const auto assignment = bench.envelopeAssignment (flow);
        for (size_t k = 0; k < assignment.size(); ++k)
            envelopeFor[(size_t) flow][k].store (assignment[k], std::memory_order_relaxed);
    }
    const auto heard = bench.heardThroughCables();
    for (size_t k = 0; k < heard.size(); ++k)
        setRoutedToOutput ((int) k, heard[k]);
}

void Synth2AudioProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (Bench::benchType))
        followBench();
}

void Synth2AudioProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (Bench::benchType))
        followBench();
}

void Synth2AudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (Bench::moduleType) || tree.hasType (Bench::cableType))
        followBench();
}

void Synth2AudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    followBench();
}

// A module came onto the bench or left it. That usually happens on the
// message thread (the editor, a restored session); from anywhere else, the
// bench is followed there shortly after.
void Synth2AudioProcessor::parameterChanged (const juce::String&, float)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        followBench();
    else
        triggerAsyncUpdate();
}

void Synth2AudioProcessor::handleAsyncUpdate()
{
    if (const int index = pendingPreset.exchange (-1); index >= 0)
        loadPreset (index);
    followBench();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Synth2AudioProcessor();   // NOSONAR(cpp:S5025): JUCE's plugin entry point returns an owning raw pointer
}

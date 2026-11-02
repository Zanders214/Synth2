#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Bench.h"
#include "ArpModel.h"
#include "Presets.h"
#include "Rack.h"

#include <tuple>

namespace
{
    // The MIDI channel the on-screen keyboard plays on.
    constexpr int keyboardChannel = 1;

    // Which factory preset is loaded, kept on the state tree, and the name
    // of the user preset loaded since, if one was.
    const juce::Identifier presetProperty     { "preset" };
    const juce::Identifier userPresetProperty { "userPreset" };
}

Synth2AudioProcessor::Synth2AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // The on-screen keyboard and the follower (the bench's layout and the
    // effects' rack in the state, and listening for changes to them) set
    // themselves up as members.
    played.ensureSize ((size_t) Arp::midiRoom);
    arpMidi.ensureSize ((size_t) Arp::midiRoom);
    for (size_t f = 0; f < filterValues.size(); ++f)
    {
        const auto& ids   = ParamID::filters[f];
        const auto  value = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        filterValues[f] = { value (ids.active), value (ids.type), value (ids.cutoff), value (ids.resonance), value (ids.envAmount),
                            { value (ids.sweep.delay), value (ids.sweep.attack), value (ids.sweep.hold), value (ids.sweep.decay),
                              value (ids.sweep.sustain), value (ids.sweep.release), value (ids.sweep.attackCurve),
                              value (ids.sweep.decayCurve), value (ids.sweep.releaseCurve) } };
    }
}

Synth2AudioProcessor::~Synth2AudioProcessor()
{
    cancelPendingUpdate();
}

// ---- The parameters -------------------------------------------------------------------------

// (In a namespace of their own, so `using namespace juce` stays in it.)
namespace
{
namespace ParameterParts
{
    using namespace juce;
    using Layout = AudioProcessorValueTreeState::ParameterLayout;
    using EnvelopeSettings = Synth2Engine::EnvelopeSettings;

    // How the parameters read in a host.
    String percentText (float value, int) { return String (roundToInt (value * 100.0f)) + " %"; }

    String signedText (int value, const char* unit) { return (value > 0 ? "+" : "") + String (value) + unit; }

    String curveText (float value, int)
    {
        const int bend = roundToInt (value * 100.0f);
        if (bend == 0)
            return "Straight";
        return signedText (bend, " %");
    }

    String timeText (float value, int)
    {
        return value < 1.0f ? String (roundToInt (value * 1000.0f)) + " ms"
                            : String (value, 2) + " s";
    }

    String hzText (float value, int)
    {
        if (roundToInt (value) < 1000)
            return String (roundToInt (value)) + " Hz";
        return String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz";
    }

    String millisecondsText (float value, int) { return String (roundToInt (value)) + " ms"; }

    AudioParameterBoolAttributes onOff (bool automatable) { return AudioParameterBoolAttributes().withAutomatable (automatable); }

    void addPercent (Layout& layout, ParameterID id, const String& name, float to, float value)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            id, name, NormalisableRange<float> (0.0f, to), value,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    }

    // A time in seconds, skewed so the short times, where the ear is fussiest,
    // get most of the knob.
    void addTime (Layout& layout, ParameterID id, const String& name, Range<float> range, float centre, float value)
    {
        NormalisableRange times (range.getStart(), range.getEnd());
        times.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (
            id, name, times, value, AudioParameterFloatAttributes().withStringFromValueFunction (timeText)));
    }

    // How each of an envelope's ramps bends: straight to start with.
    void addCurves (Layout& layout, const ParamID::Envelope& ids, int version, const String& name, const EnvelopeSettings& defaults)
    {
        for (const auto& [id, rampName, value] : { std::tuple (ids.attackCurve, "Attack Curve", defaults.attackCurve),
                                                   std::tuple (ids.decayCurve, "Decay Curve", defaults.decayCurve),
                                                   std::tuple (ids.releaseCurve, "Release Curve", defaults.releaseCurve) })
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id, version }, name + rampName, NormalisableRange<float> (-1.0f, 1.0f), value,
                AudioParameterFloatAttributes().withStringFromValueFunction (curveText)));
    }

    // An oscillator: on the bench or not, a shape, a transposition and a fine
    // tune. Whether it is on decides whether it is on the bench, so it is part
    // of the patch rather than something to automate (OSC 2's switch was
    // automatable before the bench, and stays so).
    void addOscillator (Layout& layout, int index, int version)
    {
        const auto& ids      = ParamID::oscillator (index);
        const auto  defaults = Synth2Engine::Settings {}.oscillators[(size_t) index];
        const String name    = "Osc " + String (index + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ids.on, index == 1 ? 2 : 3 }, name + "On", false, onOff (index == 1)));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ids.wave, index == 0 ? 1 : version }, name + "Wave", StringArray { "Sine", "Triangle", "Saw", "Square" },
            (int) defaults.wave));
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.pitch, version }, name + "Pitch", -Synth2Engine::maxSemitones, Synth2Engine::maxSemitones, defaults.semitones,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int value, int) { return signedText (value, " st"); })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.fine, version }, name + "Fine",
            NormalisableRange<float> (-Synth2Engine::maxFineCents, Synth2Engine::maxFineCents, 0.1f), defaults.fineCents,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return (value > 0.0f ? "+" : "") + String (value, 1) + " ct";
            })));
    }

    // An oscillator's level and its unison stack: how many copies play each
    // note, how far apart they are tuned and how wide they spread (see
    // Unison.h).
    void addLevelAndStack (Layout& layout, int index, int version)
    {
        const auto& ids          = ParamID::oscillator (index);
        const auto  defaults     = Synth2Engine::Settings {}.oscillators[(size_t) index];
        const int   stackVersion = index == 0 ? 1 : 4;
        const String stackName   = index == 0 ? String() : "Osc " + String (index + 1) + " ";
        addPercent (layout, ParameterID { ids.level, version }, "Osc " + String (index + 1) + " Level", 1.0f, defaults.level);
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.voices, stackVersion }, stackName + "Voices", 1, Unison::maxVoices, defaults.voices));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.detune, stackVersion }, stackName + "Detune",
            NormalisableRange<float> (0.0f, Unison::maxDetuneCents), defaults.detuneCents,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return String (value, 1) + " ct"; })));
        addPercent (layout, ParameterID { ids.width, stackVersion }, stackName + "Width", 1.0f, defaults.width);
    }

    // An envelope: on the bench or not, its times and its sustain level, and
    // how its ramps bend (version 10). ENV 1's attack and release keep their
    // original IDs; the DAHDSR stages came in version 5, set so that older
    // sessions sound as they did: no delay or hold, sustaining at the top.
    void addEnvelope (Layout& layout, int index)
    {
        const auto& ids       = ParamID::envelope (index);
        const auto  defaults  = Synth2Engine::Settings {}.envelopes[(size_t) index];
        const String name     = index == 0 ? String() : "Env " + String (index + 1) + " ";
        const int    version  = index == 0 ? 1 : 4;
        const String stageName = "Env " + String (index + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids.on, 4 }, stageName + "On", false, onOff (false)));
        addTime (layout, ParameterID { ids.delay, 5 },        stageName + "Delay", { 0.0f, 5.0f },    0.2f, defaults.delaySeconds);
        addTime (layout, ParameterID { ids.attack, version }, name + "Attack",     { 0.001f, 5.0f },  0.2f, defaults.attackSeconds);
        addTime (layout, ParameterID { ids.hold, 5 },         stageName + "Hold",  { 0.0f, 5.0f },    0.2f, defaults.holdSeconds);
        addTime (layout, ParameterID { ids.decay, 5 },        stageName + "Decay", { 0.001f, 10.0f }, 0.5f, defaults.decaySeconds);
        addPercent (layout, ParameterID { ids.sustain, 5 }, stageName + "Sustain", 1.0f, defaults.sustainLevel);
        addTime (layout, ParameterID { ids.release, version }, name + "Release",   { 0.005f, 10.0f }, 0.5f, defaults.releaseSeconds);
        addCurves (layout, ids, 10, stageName, defaults);
    }

    // A filter (version 11): on the bench or not, its power switch, its type,
    // cutoff, resonance, and how far its own sweep envelope moves the cutoff,
    // in semitones, then the sweep. A new filter is a plucky low-pass.
    void addFilter (Layout& layout, int index)
    {
        const auto& ids      = ParamID::filter (index);
        const auto  defaults = Synth2Engine::Settings {}.filters[(size_t) index];
        const String name    = "Filter " + String (index + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids.on, 11 }, name + "On", false, onOff (false)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids.active, 11 }, name + "Active", true));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ids.type, 11 }, name + "Type", StringArray { "Low 24", "Low 12", "Band", "High 12", "High 24", "Notch" },
            (int) defaults.type));
        NormalisableRange cutoff (Filter::minHz, Filter::maxHz);
        cutoff.setSkewForCentre (632.0f);   // the geometric middle: even octaves across the knob
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.cutoff, 11 }, name + "Cutoff", cutoff, defaults.cutoffHz,
            AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
        addPercent (layout, ParameterID { ids.resonance, 11 }, name + "Resonance", 1.0f, defaults.resonance);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.envAmount, 11 }, name + "Env Amount",
            NormalisableRange<float> (-Synth2Engine::maxEnvAmount, Synth2Engine::maxEnvAmount), defaults.envAmount,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) { return signedText (roundToInt (value), " st"); })));

        const auto& sweep  = ids.sweep;
        const auto& stages = defaults.sweep;
        addTime (layout, ParameterID { sweep.delay, 11 },   name + "Delay",   { 0.0f, 5.0f },    0.2f, stages.delaySeconds);
        addTime (layout, ParameterID { sweep.attack, 11 },  name + "Attack",  { 0.001f, 5.0f },  0.2f, stages.attackSeconds);
        addTime (layout, ParameterID { sweep.hold, 11 },    name + "Hold",    { 0.0f, 5.0f },    0.2f, stages.holdSeconds);
        addTime (layout, ParameterID { sweep.decay, 11 },   name + "Decay",   { 0.001f, 10.0f }, 0.5f, stages.decaySeconds);
        addPercent (layout, ParameterID { sweep.sustain, 11 }, name + "Sustain", 1.0f, stages.sustainLevel);
        addTime (layout, ParameterID { sweep.release, 11 }, name + "Release", { 0.005f, 10.0f }, 0.5f, stages.releaseSeconds);
        addCurves (layout, sweep, 11, name, stages);
    }

    // The delay and the reverb (version 6), both off to start with, so older
    // sessions sound as they did.
    void addDelayAndReverb (Layout& layout, const Effects::Settings& effects)
    {
        StringArray notes;
        for (const auto& division : Effects::divisions)
            notes.add (division.name);

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayOn, 6 }, "Delay On", effects.delay.on));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delaySync, 6 }, "Delay Sync", effects.delay.sync));
        NormalisableRange delayRange (Effects::minTimeMs, Effects::maxTimeMs, 1.0f);
        delayRange.setSkewForCentre (300.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::delayTime, 6 }, "Delay Time", delayRange, effects.delay.timeMs,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsText)));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::delayNote, 6 }, "Delay Note", notes, effects.delay.division));
        addPercent (layout, ParameterID { ParamID::delayFeedback, 6 }, "Delay Feedback", Effects::maxFeedback, effects.delay.feedback);
        addPercent (layout, ParameterID { ParamID::delayTone, 6 },     "Delay Tone",     1.0f, effects.delay.tone);
        addPercent (layout, ParameterID { ParamID::delayMix, 6 },      "Delay Mix",      1.0f, effects.delay.mix);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayPingPong, 6 }, "Delay Ping-Pong", effects.delay.pingPong));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::reverbOn, 6 }, "Reverb On", effects.reverb.on));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::reverbSize, 6 }, "Reverb Size", NormalisableRange<float> (0.0f, 1.0f), effects.reverb.size,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int maximumLength)
            {
                return timeText ((float) Effects::reverbSeconds (value), maximumLength);
            })));
        addPercent (layout, ParameterID { ParamID::reverbDamping, 6 }, "Reverb Damping", 1.0f, effects.reverb.damping);
        addPercent (layout, ParameterID { ParamID::reverbWidth, 6 },   "Reverb Width",   1.0f, effects.reverb.width);
        addPercent (layout, ParameterID { ParamID::reverbMix, 6 },     "Reverb Mix",     1.0f, effects.reverb.mix);
        NormalisableRange preDelayRange (0.0f, Effects::maxPreDelayMs, 1.0f);
        preDelayRange.setSkewForCentre (40.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::reverbPreDelay, 6 }, "Reverb Pre-Delay", preDelayRange, effects.reverb.preDelayMs,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (millisecondsText)));
    }

    // The drive (version 8), off to start with.
    void addDrive (Layout& layout, const Effects::Settings& effects)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::driveOn, 8 }, "Drive On", effects.drive.on));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::driveAmount, 8 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), effects.drive.amount,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float value, int)
            {
                return "+" + String (Effects::maxDriveDb * value, 1) + " dB";
            })));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::driveShape, 8 }, "Drive Shape", StringArray { "Warm", "Tube", "Hard" }, effects.drive.shape));
        addPercent (layout, ParameterID { ParamID::driveTone, 8 }, "Drive Tone", 1.0f, effects.drive.tone);
        addPercent (layout, ParameterID { ParamID::driveMix, 8 },  "Drive Mix",  1.0f, effects.drive.mix);
    }

    // A frequency, even octaves across the knob; at the end `outAt` (if
    // given) the filter is out, and reads so.
    void addHz (Layout& layout, ParameterID id, const String& name, Range<float> range, float value, float outAt = 0.0f)
    {
        NormalisableRange hz (range.getStart(), range.getEnd());
        hz.setSkewForCentre (std::sqrt (range.getStart() * range.getEnd()));
        layout.add (std::make_unique<AudioParameterFloat> (
            id, name, hz, value,
            AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([outAt] (float hzValue, int maximumLength)
            {
                if (outAt > 0.0f && std::abs (hzValue - outAt) < 0.001f * outAt)
                    return String ("Off");
                return hzText (hzValue, maximumLength);
            })));
    }

    void addGain (Layout& layout, ParameterID id, const String& name, float value)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            id, name, NormalisableRange<float> (-Effects::maxEqGainDb, Effects::maxEqGainDb, 0.1f), value,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float db, int)
            {
                return (db > 0.05f ? "+" : "") + String (db, 1) + " dB";
            })));
    }

    // The arp (version 14): off, in sixteenths, straight, not latched.
    void addArp (Layout& layout)
    {
        const Arp::Settings arp;
        StringArray rates;
        for (const auto& rate : Arp::rates)
            rates.add (rate.name);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::arpOn, 14 }, "Arp On", arp.on));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::arpRate, 14 }, "Arp Rate", rates, arp.rate));
        addPercent (layout, ParameterID { ParamID::arpSwing, 14 }, "Arp Swing", 1.0f, arp.swing);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::arpLatch, 14 }, "Arp Latch", arp.latch));
    }

    // The EQ (version 13), off and flat to start with.
    void addEq (Layout& layout, const Effects::Settings& effects)
    {
        const auto& eq = effects.eq;
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqOn, 13 }, "EQ On", eq.on));
        addHz (layout, ParameterID { ParamID::eqLowCut, 13 }, "EQ Low Cut", { Effects::minLowCutHz, Effects::maxLowCutHz },
               eq.lowCutHz, Effects::minLowCutHz);
        addHz (layout, ParameterID { ParamID::eqHighCut, 13 }, "EQ High Cut", { Effects::minHighCutHz, Effects::maxHighCutHz },
               eq.highCutHz, Effects::maxHighCutHz);
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::eqSlope, 13 }, "EQ Cut Slope", StringArray { "12 dB/oct", "24 dB/oct" }, eq.slope));
        addGain (layout, ParameterID { ParamID::eqLowGain, 13 }, "EQ Low Gain", eq.lowGainDb);
        addHz (layout, ParameterID { ParamID::eqLowFreq, 13 }, "EQ Low Freq", { Effects::minLowHz, Effects::maxLowHz }, eq.lowHz);
        addGain (layout, ParameterID { ParamID::eqMidGain, 13 }, "EQ Mid Gain", eq.midGainDb);
        addHz (layout, ParameterID { ParamID::eqMidFreq, 13 }, "EQ Mid Freq", { Effects::minMidHz, Effects::maxMidHz }, eq.midHz);
        NormalisableRange q (Effects::minEqQ, Effects::maxEqQ);
        q.setSkewForCentre (std::sqrt (Effects::minEqQ * Effects::maxEqQ));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::eqMidQ, 13 }, "EQ Mid Q", q, eq.midQ,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) { return String (value, 2); })));
        addGain (layout, ParameterID { ParamID::eqHighGain, 13 }, "EQ High Gain", eq.highGainDb);
        addHz (layout, ParameterID { ParamID::eqHighFreq, 13 }, "EQ High Freq", { Effects::minHighHz, Effects::maxHighHz }, eq.highHz);
    }
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Synth2AudioProcessor::createParameterLayout()
{
    using namespace juce;
    using namespace ParameterParts;

    AudioProcessorValueTreeState::ParameterLayout layout;

    // The four oscillators, each with its own unison stack. The bench starts
    // empty, so all four start off. (OSC 1's shape and stack keep their
    // original IDs, and OSC 2 its version 2 IDs, so older sessions load as
    // they were.)
    for (int index = 0; index < Synth2Engine::numOscillators; ++index)
    {
        addOscillator (layout, index, index < 2 ? 2 : 3);
        addLevelAndStack (layout, index, index < 2 ? 2 : 3);
    }

    // Each oscillator's and envelope's power switch (version 7), on to start
    // with. Unlike whether a module is on the bench, these can be automated.
    for (int index = 0; index < Synth2Engine::numOscillators; ++index)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::oscillator (index).active, 7 }, "Osc " + String (index + 1) + " Active", true));
    for (int index = 0; index < Synth2Engine::numEnvelopes; ++index)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::envelope (index).active, 7 }, "Env " + String (index + 1) + " Active", true));

    for (int index = 0; index < Synth2Engine::numEnvelopes; ++index)
        addEnvelope (layout, index);
    for (int index = 0; index < Synth2Engine::numFilters; ++index)
        addFilter (layout, index);

    // How sound flows between the modules: along the cables, unless a set
    // from before them says otherwise (see Bench::Migration).
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::signalFlow, 3 }, "Signal Flow", StringArray { "Fixed order", "Follow layout", "Patch cables", "Panel" },
        (int) SignalFlow::cables, AudioParameterChoiceAttributes().withAutomatable (false)));

    const Effects::Settings effects;
    addDelayAndReverb (layout, effects);
    addDrive (layout, effects);

    // Whether each effect is in the chain (version 9): the EFFECTS tab starts
    // empty, like the bench. Not automatable, like the signal flow; an
    // effect's power switch above is what a song brings in and out.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::driveInChain, 9 },  "Drive In Chain",  false, onOff (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayInChain, 9 },  "Delay In Chain",  false, onOff (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::reverbInChain, 9 }, "Reverb In Chain", false, onOff (false)));

    // GLIDE and MONO (version 12): off to start with, so older sessions
    // play as they did.
    NormalisableRange glideRange (0.0f, Synth2Engine::maxGlideSeconds);
    glideRange.setSkewForCentre (0.25f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::glide, 12 }, "Glide", glideRange, 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int maximumLength)
        {
            return value <= 0.0f ? String ("Off") : timeText (value, maximumLength);
        })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::mono, 12 }, "Mono", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // The EQ (version 13), out of the chain to start with, like the other
    // effects. Last, so every parameter before it keeps its place.
    addEq (layout, effects);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqInChain, 13 }, "EQ In Chain", false, onOff (false)));

    // The arp (version 14), off to start with.
    addArp (layout);

    return layout;
}

std::array<NoteActivity, Synth2Engine::maxNotes> Synth2AudioProcessor::getNoteActivity (int osc) const noexcept
{
    return read (noteActivity[(size_t) juce::jlimit (0, Synth2Engine::numOscillators - 1, osc)]);
}

std::array<NoteActivity, Synth2Engine::maxNotes> Synth2AudioProcessor::getSweepActivity (int osc) const noexcept
{
    return read (sweepActivity[(size_t) juce::jlimit (0, Synth2Engine::numOscillators - 1, osc)]);
}

std::array<NoteActivity, Synth2Engine::maxNotes> Synth2AudioProcessor::read (const PublishedNotes& published) noexcept
{
    std::array<NoteActivity, Synth2Engine::maxNotes> activity {};
    for (size_t i = 0; i < activity.size(); ++i)
    {
        activity[i].stage    = (NoteActivity::Stage) published[i].stage.load();
        activity[i].level    = published[i].level.load();
        activity[i].progress = published[i].progress.load();
    }
    return activity;
}

Synth2Engine::Settings Synth2AudioProcessor::currentSettings() const noexcept
{
    const auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    Synth2Engine::Settings settings;
    // The panel goes by number, as the fixed order does, and plays only its
    // two oscillators: OSC 3 and 4 wait, silent, for the bench.
    const int  chosen = juce::jlimit (0, Bench::numFlows - 1, juce::roundToInt (param (ParamID::signalFlow)));
    const bool panel  = chosen == (int) SignalFlow::panel;
    const int  flow   = panel ? (int) SignalFlow::fixed : chosen;
    const bool cables = flow == (int) SignalFlow::cables;
    for (size_t k = 0; k < settings.oscillators.size(); ++k)
    {
        const auto& ids  = ParamID::oscillators[k];
        auto& oscillator = settings.oscillators[k];
        oscillator.on          = param (ids.on) >= 0.5f && (! panel || (int) k < Bench::panelSlots);
        oscillator.routed      = ! cables || routedToOutput[k].load (std::memory_order_relaxed);
        oscillator.wave        = (Waveform) juce::jlimit (0, 3, juce::roundToInt (param (ids.wave)));
        oscillator.semitones   = juce::roundToInt (param (ids.pitch));
        oscillator.fineCents   = param (ids.fine);
        oscillator.level       = param (ids.level);
        oscillator.voices      = juce::roundToInt (param (ids.voices));
        oscillator.detuneCents = param (ids.detune);
        oscillator.width       = param (ids.width);
        oscillator.active      = param (ids.active) >= 0.5f;

        // A switched-off envelope lets its oscillator through unshaped, and
        // a switched-off filter lets it through unfiltered.
        const int shapedBy  = envelopeFor[(size_t) flow][k].load (std::memory_order_relaxed);
        oscillator.envelope = shapedBy >= 0 && param (ParamID::envelope (shapedBy).active) >= 0.5f ? shapedBy : -1;
        const int filteredBy = filterFor[(size_t) flow][k].load (std::memory_order_relaxed);
        oscillator.filter    = filteredBy >= 0 && filterValues[(size_t) filteredBy].active->load() >= 0.5f ? filteredBy : -1;
    }
    const auto readStages = [&param] (const ParamID::Envelope& ids, Synth2Engine::EnvelopeSettings& envelope)
    {
        envelope.delaySeconds   = param (ids.delay);
        envelope.attackSeconds  = param (ids.attack);
        envelope.holdSeconds    = param (ids.hold);
        envelope.decaySeconds   = param (ids.decay);
        envelope.sustainLevel   = param (ids.sustain);
        envelope.releaseSeconds = param (ids.release);
        envelope.attackCurve    = param (ids.attackCurve);
        envelope.decayCurve     = param (ids.decayCurve);
        envelope.releaseCurve   = param (ids.releaseCurve);
    };
    for (size_t e = 0; e < settings.envelopes.size(); ++e)
        readStages (ParamID::envelopes[e], settings.envelopes[e]);
    for (size_t f = 0; f < settings.filters.size(); ++f)
    {
        const auto& values = filterValues[f];
        auto& filter       = settings.filters[f];
        filter.type      = (Filter::Type) juce::jlimit (0, Filter::numTypes - 1, juce::roundToInt (values.type->load()));
        filter.cutoffHz  = values.cutoff->load();
        filter.resonance = values.resonance->load();
        filter.envAmount = values.envAmount->load();
        auto& sweep = filter.sweep;
        for (auto [stage, value] : { std::pair (&sweep.delaySeconds, values.sweep[0]), std::pair (&sweep.attackSeconds, values.sweep[1]),
                                     std::pair (&sweep.holdSeconds, values.sweep[2]), std::pair (&sweep.decaySeconds, values.sweep[3]),
                                     std::pair (&sweep.sustainLevel, values.sweep[4]), std::pair (&sweep.releaseSeconds, values.sweep[5]),
                                     std::pair (&sweep.attackCurve, values.sweep[6]), std::pair (&sweep.decayCurve, values.sweep[7]),
                                     std::pair (&sweep.releaseCurve, values.sweep[8]) })
            *stage = value->load();
    }
    // An effect plays while it is both in the chain and switched on.
    const auto playing = [&param] (const char* power, const char* inChain) { return param (power) >= 0.5f && param (inChain) >= 0.5f; };
    auto& drive = settings.effects.drive;
    drive.on     = playing (ParamID::driveOn, ParamID::driveInChain);
    drive.amount = param (ParamID::driveAmount);
    drive.shape  = juce::roundToInt (param (ParamID::driveShape));
    drive.tone   = param (ParamID::driveTone);
    drive.mix    = param (ParamID::driveMix);

    auto& delay = settings.effects.delay;
    delay.on       = playing (ParamID::delayOn, ParamID::delayInChain);
    delay.sync     = param (ParamID::delaySync) >= 0.5f;
    delay.timeMs   = param (ParamID::delayTime);
    delay.division = juce::roundToInt (param (ParamID::delayNote));
    delay.feedback = param (ParamID::delayFeedback);
    delay.tone     = param (ParamID::delayTone);
    delay.mix      = param (ParamID::delayMix);
    delay.pingPong = param (ParamID::delayPingPong) >= 0.5f;

    auto& reverb = settings.effects.reverb;
    reverb.on      = playing (ParamID::reverbOn, ParamID::reverbInChain);
    reverb.size    = param (ParamID::reverbSize);
    reverb.damping = param (ParamID::reverbDamping);
    reverb.width   = param (ParamID::reverbWidth);
    reverb.mix     = param (ParamID::reverbMix);
    reverb.preDelayMs = param (ParamID::reverbPreDelay);

    auto& eq = settings.effects.eq;
    eq.on         = playing (ParamID::eqOn, ParamID::eqInChain);
    eq.lowCutHz   = param (ParamID::eqLowCut);
    eq.highCutHz  = param (ParamID::eqHighCut);
    eq.slope      = juce::roundToInt (param (ParamID::eqSlope));
    eq.lowGainDb  = param (ParamID::eqLowGain);
    eq.lowHz      = param (ParamID::eqLowFreq);
    eq.midGainDb  = param (ParamID::eqMidGain);
    eq.midHz      = param (ParamID::eqMidFreq);
    eq.midQ       = param (ParamID::eqMidQ);
    eq.highGainDb = param (ParamID::eqHighGain);
    eq.highHz     = param (ParamID::eqHighFreq);

    settings.effects.order = Effects::decode (effectsChain.load());
    settings.effects.bpm   = hostBpm.load();
    settings.outputDb     = param (ParamID::output);
    settings.glideSeconds = param (ParamID::glide);
    settings.mono         = param (ParamID::mono) >= 0.5f;
    return settings;
}

Arp::Settings Synth2AudioProcessor::arpSettings() const noexcept
{
    const auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    Arp::Settings settings;
    settings.on    = param (ParamID::arpOn) >= 0.5f;
    settings.rate  = juce::roundToInt (param (ParamID::arpRate));
    settings.swing = param (ParamID::arpSwing);
    settings.latch = param (ParamID::arpLatch) >= 0.5f;
    settings.pattern.length = arpLength.load (std::memory_order_relaxed);
    for (int i = 0; i < Arp::maxSteps; ++i)
        settings.pattern[i] = Arp::decode (arpSteps[(size_t) i].load (std::memory_order_relaxed));
    return settings;
}

void Synth2AudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setSettings (currentSettings());
    engine.prepare (sampleRate);
    arp.prepare (sampleRate);
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

    // A synced delay and the arp follow the host's tempo (or keep the last
    // one it had), and the arp its beats while it plays.
    Arp::Transport transport;
    if (const auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm.store (*bpm);
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && position->getIsPlaying())
            {
                transport.playing = true;
                transport.ppq     = *ppq;
            }
        }
    transport.bpm = hostBpm.load();

    engine.setSettings (currentSettings());
    played.clear();
    keys.addTo (played);
    for (const auto event : midi)
        Arp::addIfRoom (played, event.data, event.numBytes, event.samplePosition);
    arp.process (played, arpMidi, buffer.getNumSamples(), arpSettings(), transport);
    engine.process (buffer, arpMidi);
    activeNotes.store (engine.getNumActiveNotes());

    for (size_t k = 0; k < noteActivity.size(); ++k)
    {
        const auto activity = engine.getActivity ((int) k);
        const auto sweeps   = engine.getSweepActivity ((int) k);
        for (size_t i = 0; i < activity.size(); ++i)
        {
            noteActivity[k][i].publish (activity[i]);
            sweepActivity[k][i].publish (sweeps[i]);
        }
    }

    keys.setHostHeld (engine.getHeldNotes());

    // An instrument makes no MIDI of its own.
    midi.clear();
}

// ---- The on-screen keyboard ---------------------------------------------------------

void Synth2AudioProcessor::OnScreenKeys::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (showingHostNotes)
        return;   // only lighting a key the host plays, not playing it again

    keysHeldByMouse.set ((size_t) midiNoteNumber);
    if (const auto scope = keyFifo.write (1); scope.blockSize1 > 0)
        keyEvents[(size_t) scope.startIndex1] = { midiNoteNumber, juce::jmax (0.01f, velocity) };
}

void Synth2AudioProcessor::OnScreenKeys::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (showingHostNotes)
        return;

    keysHeldByMouse.reset ((size_t) midiNoteNumber);
    if (const auto scope = keyFifo.write (1); scope.blockSize1 > 0)
        keyEvents[(size_t) scope.startIndex1] = { midiNoteNumber, 0.0f };
}

void Synth2AudioProcessor::OnScreenKeys::addTo (juce::MidiBuffer& midi) noexcept
{
    const auto scope = keyFifo.read (keyFifo.getNumReady());
    const auto play  = [this, &midi] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& key = keyEvents[(size_t) i];
            const auto message = key.velocity > 0.0f ? juce::MidiMessage::noteOn (keyboardChannel, key.note, key.velocity)
                                                      : juce::MidiMessage::noteOff (keyboardChannel, key.note);
            Arp::addIfRoom (midi, message.getRawData(), message.getRawDataSize(), 0);
        }
    };
    play (scope.startIndex1, scope.blockSize1);
    play (scope.startIndex2, scope.blockSize2);
}

void Synth2AudioProcessor::OnScreenKeys::setHostHeld (const std::array<std::uint64_t, 2>& held) noexcept
{
    hostHeldNotes[0].store (held[0], std::memory_order_relaxed);
    hostHeldNotes[1].store (held[1], std::memory_order_relaxed);
}

void Synth2AudioProcessor::OnScreenKeys::showHostNotes()
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

void Synth2AudioProcessor::showHostNotesOnKeyboard()
{
    keys.showHostNotes();
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
// and then the delay's repeats and the reverb's ring (of those in the chain).
double Synth2AudioProcessor::getTailLengthSeconds() const
{
    const auto settings = currentSettings();
    double tail = Synth2Engine::gateReleaseSeconds;
    for (const auto& oscillator : settings.oscillators)
    {
        if (! oscillator.on || ! oscillator.active)
            continue;
        double release = oscillator.envelope >= 0 ? (double) settings.envelopes[(size_t) oscillator.envelope].releaseSeconds
                                                  : Synth2Engine::gateReleaseSeconds;
        // A resonant filter rings on after the release, longest where its
        // sweep takes the cutoff lowest.
        if (oscillator.filter >= 0)
        {
            const auto& filter = settings.filters[(size_t) oscillator.filter];
            const float lowest = filter.cutoffHz * std::exp2 (juce::jmin (0.0f, filter.envAmount) / 12.0f);
            release += Filter::ringSeconds (filter.type, lowest, filter.resonance);
        }
        tail = juce::jmax (tail, release);
    }
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
    apvts.state.removeProperty (userPresetProperty, nullptr);
    follower.followBench();
    follower.followRack();
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

    restoreState (juce::ValueTree::fromXml (*xml), false);
    currentPreset.store (juce::jlimit (0, Presets::numPresets - 1, (int) apvts.state.getProperty (presetProperty, 0)));
}

void Synth2AudioProcessor::restoreState (const juce::ValueTree& restored, bool missingToDefaults)
{
    // An older session is brought up to date (see Bench::Migration::migrate and
    // Rack::Model::migrate).
    // (replaceState shares the tree and fills in the parameters it lacks,
    // so what was saved is read from a copy.)
    const auto saved = restored.createCopy();
    const auto arpBefore = apvts.state.getChildWithName (Arp::arpType).createCopy();
    apvts.replaceState (restored);

    // A session from before GLIDE and MONO, or the EQ, played without them,
    // whatever this instance was playing before it; and a preset sounds the
    // same whatever was playing before it, so all it doesn't say goes back
    // to its default.
    const auto toDefault = [&saved] (juce::RangedAudioParameter& parameter)
    {
        if (saved.getChildWithProperty ("id", parameter.getParameterID()).isValid())
            return;
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (parameter.getDefaultValue());
        parameter.endChangeGesture();
    };
    if (missingToDefaults)
    {
        for (auto* p : getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p))
                toDefault (*parameter);
    }
    else
    {
        for (const auto* id : { ParamID::glide, ParamID::mono })
            toDefault (*apvts.getParameter (id));
        for (const auto* id : ParamID::eqParameters)
            toDefault (*apvts.getParameter (id));
        for (const auto* id : ParamID::arpParameters)
            toDefault (*apvts.getParameter (id));
    }

    // One saved before the arp starts with its pattern; a preset saved
    // before it (its arp off) leaves the pattern drawn as it was.
    if (! saved.getChildWithName (Arp::arpType).isValid() && missingToDefaults && arpBefore.isValid())
        apvts.state.appendChild (arpBefore, nullptr);
    Arp::Model (apvts).ensureArp();

    Bench::Migration (apvts).migrate (saved);
    Rack::Model (apvts).migrate (saved);
    follower.followBench();
    follower.followRack();
    follower.followArp();
}

// ---- User presets ---------------------------------------------------------------------------

juce::String Synth2AudioProcessor::PresetLoader::getPresetName() const
{
    if (const auto user = owner.apvts.state.getProperty (userPresetProperty).toString(); user.isNotEmpty())
        return user;
    return Presets::factory[(size_t) juce::jlimit (0, Presets::numPresets - 1, owner.currentPreset.load())].name;
}

juce::String Synth2AudioProcessor::PresetLoader::getUserPresetName() const
{
    return owner.apvts.state.getProperty (userPresetProperty).toString();
}

bool Synth2AudioProcessor::PresetLoader::loadUserPreset (const UserPresets::Entry& entry)
{
    auto state = UserPresets::stateIn (entry.file);
    if (! state.hasType (owner.apvts.state.getType()))
        return false;

    // The host's program stays the factory preset it was.
    owner.restoreState (state, true);
    owner.apvts.state.setProperty (presetProperty, owner.currentPreset.load(), nullptr);
    owner.apvts.state.setProperty (userPresetProperty, entry.name, nullptr);
    return true;
}

std::optional<UserPresets::Entry> Synth2AudioProcessor::PresetLoader::saveUserPreset (const juce::String& name, const juce::String& detail,
                                                                                      Presets::Category category)
{
    auto state = owner.apvts.copyState();
    state.removeProperty (presetProperty, nullptr);
    state.removeProperty (userPresetProperty, nullptr);
    auto saved = store.save (name, detail, category, state);
    if (saved.has_value())
        owner.apvts.state.setProperty (userPresetProperty, saved->name, nullptr);
    return saved;
}

bool Synth2AudioProcessor::PresetLoader::deleteUserPreset (const UserPresets::Entry& entry)
{
    if (entry.name == getUserPresetName())
        owner.apvts.state.removeProperty (userPresetProperty, nullptr);
    return store.remove (entry);
}

void Synth2AudioProcessor::handleAsyncUpdate()
{
    if (const int index = pendingPreset.exchange (-1); index >= 0)
        loadPreset (index);
    follower.followBench();
    follower.followRack();
}

// ---- Following the bench and the effects ------------------------------------------------

Synth2AudioProcessor::Follower::Follower (Synth2AudioProcessor& processorToFollowFor) : owner (processorToFollowFor)
{
    auto& state = owner.apvts;
    Bench::Model (state).ensureBench();
    Rack::Model (state).ensureRack();
    Arp::Model (state).ensureArp();
    state.state.addListener (this);
    for (const auto& id : Bench::allModules)
        state.addParameterListener (Bench::parameterFor (id), this);
    for (const auto kind : Rack::allEffects)
        state.addParameterListener (Rack::inChainParameter (kind), this);
    followBench();
    followRack();
    followArp();
}

Synth2AudioProcessor::Follower::~Follower()
{
    auto& state = owner.apvts;
    for (const auto& id : Bench::allModules)
        state.removeParameterListener (Bench::parameterFor (id), this);
    for (const auto kind : Rack::allEffects)
        state.removeParameterListener (Rack::inChainParameter (kind), this);
    state.state.removeListener (this);
}

void Synth2AudioProcessor::Follower::followBench()
{
    const Bench::Model bench (owner.apvts);
    for (const auto flow : { SignalFlow::fixed, SignalFlow::layout, SignalFlow::cables })
    {
        const auto envelopes = bench.envelopeAssignment (flow);
        const auto filters   = bench.filterAssignment (flow);
        for (size_t k = 0; k < envelopes.size(); ++k)
        {
            owner.envelopeFor[(size_t) flow][k].store (envelopes[k], std::memory_order_relaxed);
            owner.filterFor[(size_t) flow][k].store (filters[k], std::memory_order_relaxed);
        }
    }
    const auto heard = bench.heardThroughCables();
    for (size_t k = 0; k < heard.size(); ++k)
        owner.setRoutedToOutput ((int) k, heard[k]);
}

void Synth2AudioProcessor::Follower::followRack()
{
    owner.effectsChain.store (Effects::encode (Rack::Model (owner.apvts).chain()));
}

void Synth2AudioProcessor::Follower::followArp()
{
    const auto pattern = Arp::Model (owner.apvts).pattern();
    for (int i = 0; i < Arp::maxSteps; ++i)
        owner.arpSteps[(size_t) i].store (Arp::encode (pattern[i]), std::memory_order_relaxed);
    owner.arpLength.store (pattern.length, std::memory_order_relaxed);
}

void Synth2AudioProcessor::Follower::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (Bench::benchType))
        followBench();
    else if (parent.hasType (Rack::rackType))
        followRack();
    else if (parent.hasType (Arp::arpType) || child.hasType (Arp::arpType))
        followArp();
}

void Synth2AudioProcessor::Follower::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (Bench::benchType))
        followBench();
    else if (parent.hasType (Rack::rackType))
        followRack();
}

void Synth2AudioProcessor::Follower::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (Bench::moduleType) || tree.hasType (Bench::cableType))
        followBench();
    else if (tree.hasType (Rack::effectType))
        followRack();
    else if (tree.hasType (Arp::arpType) || tree.hasType (Arp::stepType))
        followArp();
}

void Synth2AudioProcessor::Follower::valueTreeRedirected (juce::ValueTree&)
{
    followBench();
    followRack();
    followArp();
}

// A module came onto the bench or left it, or an effect into the chain or
// out of it. That usually happens on the message thread (the editor, a
// restored session); from anywhere else, both are followed there shortly
// after.
void Synth2AudioProcessor::Follower::parameterChanged (const juce::String&, float)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        followBench();
        followRack();
    }
    else
    {
        owner.triggerAsyncUpdate();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Synth2AudioProcessor();   // NOSONAR(cpp:S5025): JUCE's plugin entry point returns an owning raw pointer
}

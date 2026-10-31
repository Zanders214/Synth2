#pragma once

#include <array>
#include <cstddef>

/** Synth2's parameter IDs, shared by the processor, the editor and the tests. */
namespace ParamID
{
    // OSC 1's and ENV 1's IDs keep the plain names they had before there
    // were four of each, so older sessions and automation still find them.
    inline constexpr const char* osc1Wave    = "wave";
    inline constexpr const char* osc1Voices  = "voices";   // OSC 1's unison stack
    inline constexpr const char* osc1Detune  = "detune";
    inline constexpr const char* osc1Width   = "width";
    inline constexpr const char* env1Attack  = "attack";
    inline constexpr const char* env1Release = "release";
    inline constexpr const char* output  = "output";

    // The four oscillators, each with its own unison stack. OSC 1's shape
    // and stack are the IDs above.
    inline constexpr const char* osc1On    = "osc1On";
    inline constexpr const char* osc1Pitch = "osc1Pitch";
    inline constexpr const char* osc1Fine  = "osc1Fine";
    inline constexpr const char* osc1Level = "osc1Level";
    inline constexpr const char* osc2On    = "osc2On";
    inline constexpr const char* osc2Wave  = "osc2Wave";
    inline constexpr const char* osc2Pitch = "osc2Pitch";
    inline constexpr const char* osc2Fine  = "osc2Fine";
    inline constexpr const char* osc2Level = "osc2Level";
    inline constexpr const char* osc3On    = "osc3On";
    inline constexpr const char* osc3Wave  = "osc3Wave";
    inline constexpr const char* osc3Pitch = "osc3Pitch";
    inline constexpr const char* osc3Fine  = "osc3Fine";
    inline constexpr const char* osc3Level = "osc3Level";
    inline constexpr const char* osc4On    = "osc4On";
    inline constexpr const char* osc4Wave  = "osc4Wave";
    inline constexpr const char* osc4Pitch = "osc4Pitch";
    inline constexpr const char* osc4Fine  = "osc4Fine";
    inline constexpr const char* osc4Level = "osc4Level";
    inline constexpr const char* osc2Voices = "osc2Voices";
    inline constexpr const char* osc2Detune = "osc2Detune";
    inline constexpr const char* osc2Width  = "osc2Width";
    inline constexpr const char* osc3Voices = "osc3Voices";
    inline constexpr const char* osc3Detune = "osc3Detune";
    inline constexpr const char* osc3Width  = "osc3Width";
    inline constexpr const char* osc4Voices = "osc4Voices";
    inline constexpr const char* osc4Detune = "osc4Detune";
    inline constexpr const char* osc4Width  = "osc4Width";

    // Each oscillator's and envelope's power switch: switched off, an
    // oscillator falls silent and an envelope lets its oscillator through
    // unshaped, both staying on the bench.
    inline constexpr const char* osc1Active = "osc1Active";
    inline constexpr const char* osc2Active = "osc2Active";
    inline constexpr const char* osc3Active = "osc3Active";
    inline constexpr const char* osc4Active = "osc4Active";
    inline constexpr const char* env1Active = "env1Active";
    inline constexpr const char* env2Active = "env2Active";
    inline constexpr const char* env3Active = "env3Active";
    inline constexpr const char* env4Active = "env4Active";

    /** Oscillator `index`'s (0..3) parameters, by role. */
    struct Oscillator
    {
        const char* on;
        const char* wave;
        const char* pitch;
        const char* fine;
        const char* level;
        const char* voices;
        const char* detune;
        const char* width;
        const char* active;
    };
    inline constexpr std::array<Oscillator, 4> oscillators {{
        { osc1On, osc1Wave, osc1Pitch, osc1Fine, osc1Level, osc1Voices, osc1Detune, osc1Width,  osc1Active },
        { osc2On, osc2Wave, osc2Pitch, osc2Fine, osc2Level, osc2Voices, osc2Detune, osc2Width,  osc2Active },
        { osc3On, osc3Wave, osc3Pitch, osc3Fine, osc3Level, osc3Voices, osc3Detune, osc3Width,  osc3Active },
        { osc4On, osc4Wave, osc4Pitch, osc4Fine, osc4Level, osc4Voices, osc4Detune, osc4Width,  osc4Active } }};
    constexpr const Oscillator& oscillator (int index) { return oscillators[(size_t) index]; }

    // The four envelopes. ENV 1's attack and release are above.
    inline constexpr const char* env1On      = "env1On";
    inline constexpr const char* env1Delay   = "env1Delay";
    inline constexpr const char* env1Hold    = "env1Hold";
    inline constexpr const char* env1Decay   = "env1Decay";
    inline constexpr const char* env1Sustain = "env1Sustain";
    inline constexpr const char* env2On      = "env2On";
    inline constexpr const char* env2Delay   = "env2Delay";
    inline constexpr const char* env2Attack  = "env2Attack";
    inline constexpr const char* env2Hold    = "env2Hold";
    inline constexpr const char* env2Decay   = "env2Decay";
    inline constexpr const char* env2Sustain = "env2Sustain";
    inline constexpr const char* env2Release = "env2Release";
    inline constexpr const char* env3On      = "env3On";
    inline constexpr const char* env3Delay   = "env3Delay";
    inline constexpr const char* env3Attack  = "env3Attack";
    inline constexpr const char* env3Hold    = "env3Hold";
    inline constexpr const char* env3Decay   = "env3Decay";
    inline constexpr const char* env3Sustain = "env3Sustain";
    inline constexpr const char* env3Release = "env3Release";
    inline constexpr const char* env4On      = "env4On";
    inline constexpr const char* env4Delay   = "env4Delay";
    inline constexpr const char* env4Attack  = "env4Attack";
    inline constexpr const char* env4Hold    = "env4Hold";
    inline constexpr const char* env4Decay   = "env4Decay";
    inline constexpr const char* env4Sustain = "env4Sustain";
    inline constexpr const char* env4Release = "env4Release";

    // Each envelope's ramps' bends.
    inline constexpr const char* env1AttackCurve  = "env1AttackCurve";
    inline constexpr const char* env1DecayCurve   = "env1DecayCurve";
    inline constexpr const char* env1ReleaseCurve = "env1ReleaseCurve";
    inline constexpr const char* env2AttackCurve  = "env2AttackCurve";
    inline constexpr const char* env2DecayCurve   = "env2DecayCurve";
    inline constexpr const char* env2ReleaseCurve = "env2ReleaseCurve";
    inline constexpr const char* env3AttackCurve  = "env3AttackCurve";
    inline constexpr const char* env3DecayCurve   = "env3DecayCurve";
    inline constexpr const char* env3ReleaseCurve = "env3ReleaseCurve";
    inline constexpr const char* env4AttackCurve  = "env4AttackCurve";
    inline constexpr const char* env4DecayCurve   = "env4DecayCurve";
    inline constexpr const char* env4ReleaseCurve = "env4ReleaseCurve";

    /** Envelope `index`'s (0..3) parameters, by role. */
    struct Envelope
    {
        const char* on;
        const char* delay;
        const char* attack;
        const char* hold;
        const char* decay;
        const char* sustain;
        const char* release;
        const char* active;
        const char* attackCurve;
        const char* decayCurve;
        const char* releaseCurve;
    };
    inline constexpr std::array<Envelope, 4> envelopes {{
        { env1On, env1Delay, env1Attack, env1Hold, env1Decay, env1Sustain, env1Release, env1Active,
          env1AttackCurve, env1DecayCurve, env1ReleaseCurve },
        { env2On, env2Delay, env2Attack, env2Hold, env2Decay, env2Sustain, env2Release, env2Active,
          env2AttackCurve, env2DecayCurve, env2ReleaseCurve },
        { env3On, env3Delay, env3Attack, env3Hold, env3Decay, env3Sustain, env3Release, env3Active,
          env3AttackCurve, env3DecayCurve, env3ReleaseCurve },
        { env4On, env4Delay, env4Attack, env4Hold, env4Decay, env4Sustain, env4Release, env4Active,
          env4AttackCurve, env4DecayCurve, env4ReleaseCurve } }};
    constexpr const Envelope& envelope (int index) { return envelopes[(size_t) index]; }

    // How sound flows between the modules on the bench.
    inline constexpr const char* signalFlow = "signalFlow";

    // The effects after the mix (see Effects.h).
    inline constexpr const char* driveOn       = "driveOn";
    inline constexpr const char* driveAmount   = "driveAmount";
    inline constexpr const char* driveShape    = "driveShape";
    inline constexpr const char* driveTone     = "driveTone";
    inline constexpr const char* driveMix      = "driveMix";
    inline constexpr const char* delayOn       = "delayOn";
    inline constexpr const char* delaySync     = "delaySync";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delayNote     = "delayNote";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* delayTone     = "delayTone";
    inline constexpr const char* delayMix      = "delayMix";
    inline constexpr const char* delayPingPong = "delayPingPong";
    inline constexpr const char* reverbOn      = "reverbOn";
    inline constexpr const char* reverbSize    = "reverbSize";
    inline constexpr const char* reverbDamping = "reverbDamping";
    inline constexpr const char* reverbWidth   = "reverbWidth";
    inline constexpr const char* reverbMix     = "reverbMix";
    inline constexpr const char* reverbPreDelay = "reverbPreDelay";

    // Whether each effect is in the chain, on the EFFECTS tab (see Rack.h);
    // its ...On above is its power switch.
    inline constexpr const char* driveInChain  = "driveInChain";
    inline constexpr const char* delayInChain  = "delayInChain";
    inline constexpr const char* reverbInChain = "reverbInChain";

    // Retired with the first bench's shared unison and envelope modules; only
    // read from sessions saved with them, to carry them over.
    inline constexpr const char* unisonOn   = "unisonOn";
    inline constexpr const char* envelopeOn = "envelopeOn";

    // Retired when the effects could be put in any order (their order is now
    // where they sit: see Rack.h); only read from older sessions. 0: the
    // delay before the reverb, 1: the reverb first, the drive always first.
    inline constexpr const char* effectsOrder = "effectsOrder";
}

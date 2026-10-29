#pragma once

/** Synth2's parameter IDs, shared by the processor, the editor and the tests. */
namespace ParamID
{
    inline constexpr const char* wave    = "wave";      // OSC 1's shape
    inline constexpr const char* voices  = "voices";    // OSC 1's unison stack
    inline constexpr const char* detune  = "detune";
    inline constexpr const char* width   = "width";
    inline constexpr const char* attack  = "attack";    // ENV 1
    inline constexpr const char* release = "release";
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
    };
    inline constexpr Oscillator oscillators[] {
        { osc1On, wave,     osc1Pitch, osc1Fine, osc1Level, voices,     detune,     width },
        { osc2On, osc2Wave, osc2Pitch, osc2Fine, osc2Level, osc2Voices, osc2Detune, osc2Width },
        { osc3On, osc3Wave, osc3Pitch, osc3Fine, osc3Level, osc3Voices, osc3Detune, osc3Width },
        { osc4On, osc4Wave, osc4Pitch, osc4Fine, osc4Level, osc4Voices, osc4Detune, osc4Width } };

    // The four envelopes. ENV 1's attack and release are `attack` and
    // `release` above.
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
    };
    inline constexpr Envelope envelopes[] {
        { env1On, env1Delay, attack,     env1Hold, env1Decay, env1Sustain, release },
        { env2On, env2Delay, env2Attack, env2Hold, env2Decay, env2Sustain, env2Release },
        { env3On, env3Delay, env3Attack, env3Hold, env3Decay, env3Sustain, env3Release },
        { env4On, env4Delay, env4Attack, env4Hold, env4Decay, env4Sustain, env4Release } };

    // How sound flows between the modules on the bench.
    inline constexpr const char* signalFlow = "signalFlow";

    // Retired with the first bench's shared unison and envelope modules; only
    // read from sessions saved with them, to carry them over.
    inline constexpr const char* unisonOn   = "unisonOn";
    inline constexpr const char* envelopeOn = "envelopeOn";
}

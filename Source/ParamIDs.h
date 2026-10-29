#pragma once

/** Synth2's parameter IDs, shared by the processor, the editor and the tests. */
namespace ParamID
{
    inline constexpr const char* wave    = "wave";   // OSC 1's shape
    inline constexpr const char* voices  = "voices";
    inline constexpr const char* detune  = "detune";
    inline constexpr const char* width   = "width";
    inline constexpr const char* attack  = "attack";
    inline constexpr const char* release = "release";
    inline constexpr const char* output  = "output";

    // The four oscillators. OSC 1's shape is `wave` above.
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

    /** Oscillator `index`'s (0..3) parameters, by role. */
    struct Oscillator
    {
        const char* on;
        const char* wave;
        const char* pitch;
        const char* fine;
        const char* level;
    };
    inline constexpr Oscillator oscillators[] {
        { osc1On, wave,     osc1Pitch, osc1Fine, osc1Level },
        { osc2On, osc2Wave, osc2Pitch, osc2Fine, osc2Level },
        { osc3On, osc3Wave, osc3Pitch, osc3Fine, osc3Level },
        { osc4On, osc4Wave, osc4Pitch, osc4Fine, osc4Level } };

    // Whether the unison stack and the envelope are on the bench, and how
    // sound flows between the modules there.
    inline constexpr const char* unisonOn   = "unisonOn";
    inline constexpr const char* envelopeOn = "envelopeOn";
    inline constexpr const char* signalFlow = "signalFlow";
}

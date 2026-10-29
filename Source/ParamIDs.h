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

    // The two oscillators. OSC 1's shape is `wave` above.
    inline constexpr const char* osc1Pitch = "osc1Pitch";
    inline constexpr const char* osc1Fine  = "osc1Fine";
    inline constexpr const char* osc1Level = "osc1Level";
    inline constexpr const char* osc2On    = "osc2On";
    inline constexpr const char* osc2Wave  = "osc2Wave";
    inline constexpr const char* osc2Pitch = "osc2Pitch";
    inline constexpr const char* osc2Fine  = "osc2Fine";
    inline constexpr const char* osc2Level = "osc2Level";
}

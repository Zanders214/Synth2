#pragma once

// Real-time-safety attribute macro.
//
// Under the RealtimeSanitizer build (-DSYNTH2_RT_SANITIZE=ON) the CMake target
// defines SYNTH2_RT_NONBLOCKING as [[clang::nonblocking]], which makes clang's
// RTSan verify that the annotated function performs no allocation, lock, or
// syscall on the audio thread. In every other build (MSVC, normal release,
// SonarCloud analysis) it expands to nothing, so the code is unaffected.
#ifndef SYNTH2_RT_NONBLOCKING
#define SYNTH2_RT_NONBLOCKING
#endif

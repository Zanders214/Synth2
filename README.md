# ZandersSynth2

A simple polyphonic synth plugin for DAWs (VST3 / AU / Standalone), built with
[JUCE](https://juce.com). Pick one of the four basic wave shapes, stack up to 16
copies of it on every note, detune and spread them, and play chords: the classic
wide, detuned "supersaw" pad in a few turns.

```
note -> VOICES x oscillator -> DETUNE + WIDTH -> ATTACK / RELEASE -> OUTPUT
```

## Controls

| Control     | What it does                                                        |
|-------------|---------------------------------------------------------------------|
| **Wave**    | The four chips under the WAVE display: **SINE**, **TRI**, **SAW** (the default) or **SQUARE**. |
| **Voices**  | How many copies of the oscillator play each note, 1 to 16. One voice is a plain, centred oscillator. |
| **Detune**  | How far apart the voices are tuned: the outer two sit this many cents flat and sharp (up to ±50 ct) and the rest fall evenly between. Detuned voices beat against each other, which is what makes the sound thick. |
| **Width**   | How far the voices spread across the stereo field. 0% keeps them in the centre (mono); 100% puts the outer ones hard left and right. |
| **Attack**  | How long a note takes to fade in, 1 ms to 5 s.                       |
| **Release** | How long a note rings on after its key goes up, 5 ms to 10 s.        |
| **Output**  | Output level, -24 to +6 dB.                                          |

Every control is a parameter, so it's saved with your set and can be automated.

### How the voices spread

With more than one voice, the copies sit evenly across the DETUNE range. WIDTH pans
them in an interleaved order (the flattest hard left, the next hard right, then the
next pair a step in), so each side gets a mix of flat and sharp voices and the stack
stays balanced. The stack is scaled by 1 / sqrt(VOICES), so going from 1 voice to 16
keeps a chord at about the same loudness, and every voice starts at a random point
in its cycle so a chord doesn't thump on the attack. Turning VOICES while notes sound
fades the copies in and out rather than switching them, so it doesn't click.

### Playing it

- 16 notes of polyphony. A seventeenth note takes the voice that has been releasing
  longest (or, failing that, the oldest note).
- The **sustain pedal** (CC 64) holds released notes until it lifts. All Sound Off
  (CC 120) silences everything at once; All Notes Off (CC 123) releases everything.
- MIDI is played sample-accurately: a note starts on the exact sample the host sends it.
- The keyboard along the bottom (C3 to C6) plays the synth with the mouse, which is
  handy in the standalone app, and lights up the keys your DAW is playing.

The saw and square are band-limited (PolyBLEP), so high notes don't fill up with
aliasing; the sine and triangle are played as they are.

## Building

JUCE is pulled automatically via CMake `FetchContent` (pinned to `8.0.4`):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

In locked-down / offline environments, point at a local JUCE checkout instead:

```sh
cmake -B build -DJUCE_PATH=/path/to/JUCE
```

Built plugins land under `build/Synth2_artefacts/`.

## Tests

The DSP core (`Synth2Engine`) has a headless, framework-free test harness, the wave
shapes and unison spread have their own JUCE-free tests, and the processor and editor
are tested headlessly too: the real UI is rendered offscreen at 1x and 2x (on Linux
this runs under `xvfb-run`):

```sh
ctest --test-dir build --output-on-failure
```

## Layout

```
Source/
  PluginProcessor.{h,cpp}    thin host wrapper + parameters (APVTS); the on-screen keyboard's lock-free hand-over
  ParamIDs.h                 the parameter IDs
  Synth2Engine.h             host-agnostic DSP core: 16 voices, MIDI, smoothing, output
  SynthVoice.h               one note: up to 16 unison oscillators under the envelope
  Oscillator.h               the four wave shapes, and the PolyBLEP oscillator that plays them
  Unison.h                   where each unison voice sits: its detune, its pan, the stack's level
  PluginEditor.{h,cpp}       the Neon Plugins editor: wells, wave chips, dials, keyboard
  Synth2LookAndFeel.{h,cpp}  the dials (design-system Dial: arc + glowing core)
  WaveSelector.h             the SINE / TRI / SAW / SQUARE chips
  VoiceDisplays.h            the WAVE and UNISON wells
  NeonKeyboard.h             the on-screen keyboard
  Synth2Theme.h              shared palette, fonts, colour + value formatting
  Synth2VisualModel.h        pure wave / unison -> display math (uses the engine's own)
  RtSafety.h                 the RealtimeSanitizer annotation
Tests/
  Synth2EngineTests.cpp         framework-free DSP tests: pitch, envelope, unison, polyphony, pedal
  Synth2VisualModelTests.cpp    framework-free tests for the shapes, the spread and the displays' model
  PluginTests.cpp               processor + editor: params, buses, MIDI, state, offscreen renders
  rt_check.cpp                  RealtimeSanitizer driver
  perf_bench.cpp                nanobench micro-benchmark
```

## UI

The editor follows the **Neon Plugins** design system natively in JUCE (no WebView):
the dark radial panel, the `Zanders` + product wordmark (Synth2 in violet) with its
mode badge, two inset display wells (the WAVE shape in the spectrum gradient with a
faint copy per unison voice; the UNISON stack as a glowing dot per voice, by detune and
pan), the wave chips with the accent-gradient selection, a row of six Dials coloured
along the cyan → violet → pink → amber spectrum, and an ivory keyboard whose pressed
keys glow violet. The displays draw on the engine's own wave and unison maths
(`Synth2VisualModel`), so what they show never drifts from what you hear. The face is
authored at 480 px and scales proportionally (resizable, fixed aspect ratio).

Typography targets Space Grotesk (UI) + JetBrains Mono (numbers), used when the host
has them and falling back to the platform sans / monospaced face otherwise; bundling
the TTFs as `BinaryData` is a localized swap in `Synth2Theme.h`.

## CI

GitHub Actions builds on Linux/macOS/Windows, runs the unit tests, and validates the
VST3 with [`pluginval`](https://github.com/Tracktion/pluginval) at strictness level 5.
Alongside it: a RealtimeSanitizer gate (no allocation, lock or system call on the audio
thread), a performance benchmark with a trend dashboard, and SonarCloud analysis with
coverage (the upload waits until the repository has a `SONAR_TOKEN` secret).

When a pull request from `dev` is merged into `main`, the release workflow builds and
tests all three platforms and publishes a GitHub Release containing:

- a Windows x64 ZIP (VST3 and standalone app),
- a Linux x64 tarball (VST3 and standalone app), and
- a universal macOS installer package (Apple Silicon and Intel).

The macOS installer puts the VST3 in `/Library/Audio/Plug-Ins/VST3`, the AU in
`/Library/Audio/Plug-Ins/Components`, and the standalone app in `/Applications`.
After installation, restart your DAW so it scans the newly installed plug-in.

FL Studio, Ableton Live and most other DAWs load the VST3; Logic Pro and GarageBand
load the AU.

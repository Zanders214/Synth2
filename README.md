# ZandersSynth2

A simple polyphonic synth plugin for DAWs (VST3 / AU / Standalone), built with
[JUCE](https://juce.com). Pick one of the four basic wave shapes, add a second
oscillator if you like, stack up to 16 copies on every note, detune and spread
them, and play chords: the classic wide, detuned "supersaw" pad in a few turns.

```
note -> VOICES x (OSC 1 + OSC 2) -> DETUNE + WIDTH -> ATTACK / RELEASE -> OUTPUT
```

## Controls

| Control     | What it does                                                        |
|-------------|---------------------------------------------------------------------|
| **Wave**    | Each oscillator's shape: **SINE**, **TRIANGLE**, **SAW** (OSC 1's default) or **SQUARE** (OSC 2's), picked in its display (see below). |
| **OSC 2**   | Adds the second oscillator, or takes it away (off by default). |
| **Pitch**   | Transposes the oscillator, -24 to +24 semitones: +12 is an octave up, +7 a fifth. |
| **Fine**    | Tunes the oscillator by up to ±50 cents, for a slow beat against the other one. |
| **Level**   | The oscillator's level, 0 to 100%. |
| **Voices**  | How many copies of the oscillator play each note, 1 to 16. One voice is a plain, centred oscillator. |
| **Detune**  | How far apart the voices are tuned: the outer two sit this many cents flat and sharp (up to ±50 ct) and the rest fall evenly between. Detuned voices beat against each other, which is what makes the sound thick. |
| **Width**   | How far the voices spread across the stereo field. 0% keeps them in the centre (mono); 100% puts the outer ones hard left and right. |
| **Attack**  | How long a note takes to fade in, 1 ms to 5 s.                       |
| **Release** | How long a note rings on after its key goes up, 5 ms to 10 s.        |
| **Output**  | Output level, -24 to +6 dB.                                          |

Every control is a parameter, so it's saved with your set and can be automated.

### Two oscillators

OSC 1 plays on its own, across the whole top row, until you add OSC 2 with the
**+ OSC 2** chip in its display. OSC 1 then moves over to the left half and OSC 2
fades in on the right, with its own display and dials; its glowing **OSC 2** chip
switches it off again, and it goes back out of sight (its settings are kept for next
time). It comes set up as a square an octave below, at 70%, a classic way to add
weight. Each oscillator has
its own shape, PITCH, FINE and LEVEL; both share the unison stack (every copy plays
both, so DETUNE and WIDTH spread both) and the envelope. Switching OSC 2 on or off,
and moving either LEVEL, fades rather than clicks, and while OSC 2 is off it costs
nothing.

### Picking the wave

Each oscillator's display is also where its shape is picked. The pill in its top right
corner shows the current shape: its arrows step to the previous and next one
(wrapping round), and the scroll wheel anywhere over the display flips through
them. Click the display anywhere else and a tray of the four shapes drops open
under it, with the rest of the panel dimmed: hover a tile to preview it in the
display (the sound doesn't change until you pick), click it to keep it, or
click outside (or press Escape) to leave things as they were. The arrow keys
and Return work in the tray too. Each change morphs the drawing from the old
shape to the new one rather than jumping.

### Seeing what they do

Each group of dials has a display right above it, drawn in the dials' colours:

- **OSC 1** and **OSC 2** (above their PITCH / FINE / LEVEL): the oscillator's shape,
  with a faint copy for every unison voice, shifted by its detune. PITCH squeezes or
  stretches it (two cycles at 0, four an octave up, one an octave down), LEVEL sets its
  height.
- **UNISON** (above VOICES / DETUNE / WIDTH): a glowing dot per voice, placed left to
  right by its detune (on a fixed -50..+50 cent scale) and top to bottom by its place in
  the stereo field. DETUNE fans the dots out sideways, WIDTH up and down.
- **AMP** (above ATTACK / RELEASE / OUTPUT): the envelope every note follows: the rise
  (ATTACK), the top while the key is down (its height is OUTPUT, against a dashed 0 dB
  line) and the fall (RELEASE). Every note you play is a dot riding it: up the attack,
  resting on the top while held, down the release once you let go.

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
  Synth2Engine.h             host-agnostic DSP core: 16 voices, two oscillators, MIDI, smoothing, output
  SynthVoice.h               one note: up to 16 unison copies of both oscillators under the envelope
  Oscillator.h               the four wave shapes, and the PolyBLEP oscillator that plays them
  Unison.h                   where each unison voice sits: its detune, its pan, the stack's level
  PluginEditor.{h,cpp}       the Neon Plugins editor: wells, wave picker, dials, keyboard
  Synth2LookAndFeel.{h,cpp}  the dials (design-system Dial: arc + glowing core)
  WavePicker.h               an oscillator well's picker (pill, arrows, wheel, OSC 2's switch), its tray of shapes, the + OSC 2 chip
  VoiceDisplays.h            the OSC, UNISON and AMP wells
  NeonKeyboard.h             the on-screen keyboard
  Synth2Theme.h              shared palette, fonts, colour + value formatting
  Synth2VisualModel.h        pure wave / unison / envelope -> display math (uses the engine's own), the morph and its ease
  RtSafety.h                 the RealtimeSanitizer annotation
Tests/
  Synth2EngineTests.cpp         framework-free DSP tests: pitch, envelope, unison, the oscillators, polyphony, pedal
  Synth2VisualModelTests.cpp    framework-free tests for the shapes, the spread and the displays' model
  PluginTests.cpp               processor + editor: params, buses, MIDI, state, offscreen renders
  rt_check.cpp                  RealtimeSanitizer driver
  perf_bench.cpp                nanobench micro-benchmark
```

## UI

The editor follows the **Neon Plugins** design system natively in JUCE (no WebView):
the dark radial panel, the `Zanders` + product wordmark (Synth2 in violet) with its
mode badge, then four groups of three Dials in two rows, each row coloured along the
cyan → violet → pink → amber spectrum and each group under the display it drives:
OSC 1 and OSC 2 (the shape in the spectrum gradient with a faint copy per unison
voice, each with its picker pill and a drop-open tray of glowing shape tiles, the
chosen one ringed in the accent; OSC 1 alone across the row, with a "+ OSC 2" chip,
until OSC 2 is added with its glowing power chip), UNISON (a
glowing dot per voice, by detune and pan) and AMP (the envelope in its dials'
colours, with a live dot per sounding note); and an ivory keyboard whose
pressed keys glow violet. The displays draw on the engine's own wave, unison and
envelope maths (`Synth2VisualModel`), so what they show never drifts from what you
hear. The face is authored at 480 px and scales proportionally (resizable, fixed aspect
ratio).

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

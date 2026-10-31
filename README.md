# ZandersSynth2

A simple polyphonic synth plugin for DAWs (VST3 / AU / Standalone), built with
[JUCE](https://juce.com), laid out as a workbench: it opens empty, and you put
on it the modules you want, where you want them. Add up to four oscillators,
each with one of the four basic wave shapes and its own stack of up to 16
detuned, spread copies on every note, give each its own envelope and its own
swept filter, and play chords: the classic wide, detuned "supersaw" pad in a
few turns. Then send the
lot through a drive, a delay and a reverb, in any order, on the EFFECTS tab,
laid out the same way.

```
note -> OSC n (VOICES x shape, DETUNE + WIDTH) -> FILTER n (TYPE CUTOFF RESONANCE, swept) -> ENV n (DELAY ATTACK HOLD DECAY SUSTAIN RELEASE) -> + -> DRIVE / DELAY / REVERB (any order) -> OUTPUT
```

## The workbench

A new instance opens on an empty bench (and makes no sound). Click anywhere on it
and a tray offers what can go there:

| Module         | What it holds                                                             |
|----------------|---------------------------------------------------------------------------|
| **Oscillator** | One shape at its own pitch and level, WAVE, PITCH, FINE, LEVEL, over its own unison stack, VOICES, DETUNE, WIDTH. Up to four (OSC 1 to OSC 4). |
| **Filter**     | One oscillator's tone, each note filtered on its own: TYPE (LOW 24, LOW 12, BAND, HIGH 12, HIGH 24, NOTCH), CUTOFF, RESONANCE, and its own SWEEP envelope that moves the cutoff as each note plays, ENV AMT semitones at its top. Up to four (FILTER 1 to FILTER 4). |
| **Envelope**   | How one oscillator's notes rise and fall: DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE. Up to four (ENV 1 to ENV 4). An oscillator without one starts and stops with the key, with a short click-free fade. |

The module lands where you clicked. Drag a module by its header to move it anywhere
on the bench; drop it on another and it slides off to the nearest free spot, so
modules never stack. The cross in its header takes it off the bench (its settings are
kept for when it comes back).

The **power switch** at the left of every module's header bypasses it without taking
it off the bench: the module greys out and keeps its place, its cables and its
settings. An oscillator switched off falls silent (fading out over 20 ms, so held
notes don't click); an envelope switched off lets its oscillator through unshaped,
starting and stopping with the key as if it had no envelope; a filter switched off
lets its oscillator through unfiltered, fading over 5 ms rather than clicking. The switches are
automatable, so a DAW can bring a layer in and out during a song. What is on the bench, where, and any cables are saved
with your set. A set saved before the bench existed opens with OSC 1 (and OSC 2, if it
was on) along the top and an envelope under each, sounding as it did: the unison stack
and the envelope they used to share are copied into each. A set from the first bench,
with its single Unison and Envelope modules, is brought over the same way.

The keyboard and the OUTPUT dial stay in a strip along the bottom, whichever tab is
showing.

### Zoom and pan

The bench is a window onto a bigger canvas: room for three rows of six cards, where
the window shows two rows of four at 100%. To get around it:

- **Zoom** with Cmd + scroll (Ctrl + scroll on Windows) or a trackpad pinch, round the
  pointer, from the whole canvas (about 66%) up to 150%. Cards are redrawn at each
  zoom, so they stay sharp.
- **Pan** with the scroll wheel or two fingers, or drag any empty spot. A click that
  doesn't drag still opens the add tray, and the module lands where you clicked on
  the canvas. Scrolling over a dial still turns the dial.
- The **pill** on the bench's bottom edge has minus and plus (66%, 80%, 100%, 125%,
  150%), the zoom, and **Fit**, which frames the cards on the bench (no closer than
  100%), or the whole canvas when it is empty.

The zoom and pan are saved with your set. A set saved before the bench zoomed opens at
100%, every card where it was. Loading a preset keeps the zoom, unless its cards
wouldn't all show; then it fits them. With patch cables, a cable to a card scrolled out
of sight leaves at the bench's edge.

## Effects

The **BENCH | EFFECTS** tabs in the header swap the bench for the effects. Every note
goes through them together, after the oscillators are mixed and before OUTPUT.

The EFFECTS tab works like the bench. It opens empty: click anywhere on it and a tray
offers a **Drive**, a **Delay** and a **Reverb** (one of each; one already in the chain
is greyed out). The effect lands where you clicked, switched on. Drag a card by its
header to move it anywhere on the tab; drop it with its middle on another card and the
two swap places. The cross in its header takes it out of the chain (its settings are
kept for when it comes back).

The chain is read like a page: left to right, row by row. Each card's header shows its
place, the line over the tab spells the chain out ("THE NOTES → REVERB → DELAY →
OUTPUT"), and arrows join the cards that follow one another. So moving a card moves its
effect along the chain: a reverb into a delay smears each repeat, a delay into a reverb
sends the repeats into the room, and a drive after the reverb distorts the room along
with the notes (before it, the echoes and the room stay clean).

Each effect has the same power switch at the left of its header as the bench's modules:
switched off, it greys out and keeps its place in the chain, silent. The switches are
automatable; being in the chain is not. Each effect also has a **MIX**: up to the middle the dry sound stays at full level and the effect
is added underneath; from there the dry sound fades out until only the effect is left.
Switching an effect on or off, or taking it out of the chain, fades it over 20 ms; once
it is off it costs nothing, and switching it back on starts clean. A set saved before
effects could be moved opens with all three on the tab, in the order they played.

| Control       | What it does                                                        |
|---------------|---------------------------------------------------------------------|
| **Drive**     | How hard the sound is pushed into the curve, +0 to +36 dB. The level is made up afterwards, so turning it up adds grit, not volume. |
| **Shape**     | **WARM** rounds the peaks off smoothly (odd harmonics, like a driven op-amp); **TUBE** leans to one side, adding even harmonics too, like a valve; **HARD** clips them flat for a buzzier edge. |
| **Tone**      | How bright the driven sound stays: a lowpass after the curve, from 1 kHz up to 18 kHz. |
| **Time**      | How long the delay waits before each repeat, 10 ms to 2 s. With **SYNC** on it picks a note length at your DAW's tempo instead, from 1/32 to a whole note, dotted (D) or triplet (T). Turning it while repeats ring bends their pitch, like tape. |
| **Feedback**  | How loud each repeat comes back against the one before, 0 to 95%.    |
| **Tone**      | How bright the repeats stay: each one passes a lowpass from 1 kHz (low) up to 18 kHz (high), so they darken as they fade. |
| **Ping-Pong** | The repeats bounce from left to right.                               |
| **Size**      | How long the reverb rings before it falls 60 dB: 0.3 s for a small room, about 2 s in the middle, 12 s for a hall. The dial reads the time. |
| **Pre-Delay** | How long the room waits before it starts, 0 to 250 ms, so each note's attack stays clear of it. |
| **Damping**   | How quickly the room's highs die away.                               |
| **Width**     | How wide the room spreads: 0% is the same on both sides.             |

The DRIVE card's well draws the curve itself: the dashed diagonal is the clean sound,
the glowing line what comes out at the current DRIVE, SHAPE and MIX. The curve is
applied with first-order antiderivative anti-aliasing, so even HARD on high notes
stays free of the whistling, out-of-tune aliasing a plain clipper adds, and no DC
offset is left behind by TUBE's lean.

Like tape, each repeat of the delay also loses its deepest lows (so they don't pile up
into mud), rounds off gently when driven hard, and wavers very slightly in pitch, so
the repeats sit behind the sound rather than copying it.

The DELAY card's well shows the dry sound and each repeat where it falls across two
seconds, the left side above the line and the right below; the REVERB card's shows the
room ringing away over six seconds, and how long it rings to -60 dB. Your DAW is told
how long the effects ring on after the notes stop, so a bounce isn't cut short.

The reverb is a feedback delay network rather than the classic Freeverb, whose few
comb filters ring metallic on held synth chords: a low cut and the pre-delay, then four
steps of diffusion that multiply each sound into a dense cloud, then eight feedback
lines mixed into one another, each drifting very slightly in length so no pitch rings
out. Its tail is levelled, so a long room is longer rather than louder.

### Presets

The pill in the middle of the header holds the factory presets: its arrows step to the
previous and next one, and a click on its name opens a tray of them all. Your DAW lists
the same presets as the plugin's programs. Loading one puts its modules on the bench
(oscillators along the top, each with its own unison stack, its filter, if it has
one, under it, and its DAHDSR envelope under that, in a fixed order with no cables),
puts just its effects
on the EFFECTS tab, side by side and switched on (most pads and keys have a reverb,
some a delay on the beat), and sets everything
it doesn't use back to its default; picking it again undoes your edits. Which preset
you loaded is saved with your set.

| Preset            | What it is                                              |
|-------------------|---------------------------------------------------------|
| **Empty Bench**   | Nothing on it: where a new instance starts              |
| **Supersaw Pad**  | Nine wide saws, an octave blooming in on top            |
| **Warm Strings**  | A soft saw section with a low triangle                  |
| **Slow Swell**    | A shimmering stack, its layers delayed to arrive in turn |
| **Trance Lead**   | A bright detuned saw and square                         |
| **8-Bit Lead**    | Bare squares, the octave a short blip                   |
| **Hollow Flute**  | A breathy triangle, its octave breathing in late        |
| **Saw Pluck**     | A short, bright pluck for arpeggios                     |
| **Wood Mallet**   | A soft mallet that dies away                            |
| **Chord Stab**    | Held for a beat, then gone: house chords                |
| **Drawbar Organ** | Sines at 16', 8' and 4', with a percussive click        |
| **Glass Keys**    | Struck sines, the overtones dying first                 |
| **Brass Section** | Biting saws that settle, a square swelling in below     |
| **Fifth Stack**   | Root, fifth and sub, for big chords                     |
| **Reese Bass**    | Two beating saws an octave down                         |
| **Sub Bass**      | A clean sine sub with a plucked edge                    |
| **Filter Pluck**  | A saw whose low-pass snaps shut as it rings             |
| **Acid Bass**     | A squelchy square, its resonant filter swept short      |

### Signal flow

The gear at the top right picks how sound flows between the modules (and, below,
how the cards show themselves: see [Views](#views)):

- **Fixed order** (the default): by number, wherever the modules sit: ENV 1 shapes
  OSC 1 and FILTER 1 filters it, ENV 2 and FILTER 2 take OSC 2, and so on. Each envelope
  and filter shows the oscillator it takes in its header ("> OSC 1"); one whose
  oscillator isn't on the bench is greyed out.
- **Follow layout**: in reading order, row by row and left to right. Each module shows
  its place in the order in its header, and each envelope shapes the first oscillator
  before it that has no envelope yet (each filter, likewise, the first with no filter
  yet), so an envelope or filter placed after (or under) an oscillator takes it. Moving
  a module renumbers them and can hand an envelope or a filter to another oscillator.
- **Patch cables**: each oscillator gets an **OUT** jack in its header, each filter and
  envelope an **IN** and an **OUT**, and OUTPUT an **IN**. Drag from an OUT jack to an
  IN jack to patch a cable. An oscillator's sound runs along its cables through at most
  one filter and one envelope, in either order, to the output: OSC -> FILTER -> ENV ->
  OUT, OSC -> ENV -> FILTER -> OUT, or straight OSC -> OUT (heard on/off with the key).
  Each filter and envelope takes one cable in and sends one out; a patch that would put
  two filters or two envelopes in one chain, or loop round, doesn't take. Only what
  reaches the output is heard, and the rest is greyed out. Grab a cable by either end,
  or drop it anywhere else, to unplug it.

The oscillators, each through its own filter and under its own envelope, are summed,
so the three ways differ only in which filter and envelope take which oscillator
(the filter is the same either side of the envelope: both work on each note on its
own). The effects follow the
sum, on their own tab (see [Effects](#effects)).

## Controls

| Control     | What it does                                                        |
|-------------|---------------------------------------------------------------------|
| **Wave**    | Each oscillator's shape: **SINE**, **TRIANGLE**, **SAW** or **SQUARE**, picked in its display (see below). |
| **Pitch**   | Transposes the oscillator, -24 to +24 semitones: +12 is an octave up, +7 a fifth. |
| **Fine**    | Tunes the oscillator by up to ±50 cents, for a slow beat against the others. |
| **Level**   | The oscillator's level, 0 to 100%. |
| **Voices**  | How many copies of the oscillator play each note, 1 to 16. One voice is a plain, centred oscillator. |
| **Detune**  | How far apart the voices are tuned: the outer two sit this many cents flat and sharp (up to ±50 ct) and the rest fall evenly between. Detuned voices beat against each other, which is what makes the sound thick. |
| **Width**   | How far the voices spread across the stereo field. 0% keeps them in the centre (mono); 100% puts the outer ones hard left and right. |
| **Delay**   | How long the envelope waits, silent, after the key goes down, 0 to 5 s. |
| **Attack**  | How long its oscillator then takes to fade in, 1 ms to 5 s.          |
| **Hold**    | How long it stays at the top before decaying, 0 to 5 s.              |
| **Decay**   | How long it takes to fall from the top to the sustain level, 1 ms to 10 s. |
| **Sustain** | The level it stays at while the key is held, 0 to 100% of the top. At 100% (the default) there is no decay; turned down, notes hit and then settle, for plucks and keys. Turning it while a note is held glides there. |
| **Release** | How long it rings on after its key goes up, 5 ms to 10 s, falling from wherever it was. A note lasts until the slowest of its oscillators has faded. |
| **Type**    | A filter's kind: **LOW 24** and **LOW 12** let the lows through (24 or 12 dB an octave), **HIGH 24** and **HIGH 12** the highs, **BAND** a band around the cutoff, and **NOTCH** takes one out. Stepped with the arrows in its display. |
| **Cutoff**  | Where the filter bites, 20 Hz to 20 kHz, evenly by octave across the dial. |
| **Resonance** | How much the filter rings at its cutoff, 0 to 100%: a peak there that grows to a whistle near the top, but never runs away. |
| **Env Amt** | How far the filter's SWEEP envelope moves its cutoff, -48 to +48 semitones at the top of the sweep (+24, the default, two octaves up). Below 0 the sweep closes the filter instead. Its sweep has the same six stages and three bends as an envelope, edited on its SWEEP graph. |
| **Output**  | Output level, -24 to +6 dB.                                          |

Every control is a parameter, so it's saved with your set and can be automated.
Which modules are on the bench, and the signal flow, are saved too but not automated.

### Four oscillators

Each oscillator comes set up for layering: OSC 1 a saw at the note, OSC 2 a square an
octave below at 70%, OSC 3 a saw a fifth up at 50%, and OSC 4 a triangle an octave up
at 40%. Each has its own shape, PITCH, FINE and LEVEL, its own unison stack (VOICES,
DETUNE and WIDTH spread only its copies, so a thick detuned saw can sit over a single
clean sub-octave square), its own filter and its own envelope. Adding, removing or unpatching an
oscillator, and moving its LEVEL, fades rather than clicks, and an oscillator that
isn't heard costs nothing.

### Picking the wave

Each oscillator's display is also where its shape is picked. The pill in its top right
corner shows the current shape: its arrows step to the previous and next one
(wrapping round), and the scroll wheel anywhere over the display flips through
them. Click the display anywhere else and a tray of the four shapes drops open
under it, with the rest of the bench dimmed: hover a tile to preview it in the
display (the sound doesn't change until you pick), click it to keep it, or
click outside (or press Escape) to leave things as they were. The arrow keys
and Return work in the tray too. Each change morphs the drawing from the old
shape to the new one rather than jumping.

### Seeing what they do

Each module has a display right above its dials, drawn in the dials' colours:

- **OSC 1** to **OSC 4** (above their dials): the oscillator's shape, with a faint copy
  for every one of its unison voices, shifted by its detune. PITCH squeezes or
  stretches it (two cycles at 0, four an octave up, one an octave down), LEVEL sets its
  height.
- **AMP**, in each envelope (above its six dials): the envelope its oscillator's notes
  follow, each stage in its dial's colour: the wait (DELAY), the rise (ATTACK), the top
  (HOLD), the fall (DECAY) to the SUSTAIN level, held while the key is down, and the
  fall to silence (RELEASE). The shape always fills the graph, edge to edge and up to the
  top, so it looks the same however big the graph is (OUTPUT's level shows in its
  corner). Every note you play is a dot riding it through the stages, resting on the
  sustain while held.

  You can also shape the envelope right on the graph: drag a corner sideways to
  stretch its stage (the delay's end, the top of the rise, the end of the hold, the end
  of the release), and the corner where the fall meets the sustain sideways for DECAY
  and up or down for SUSTAIN. The corner stays under the pointer, and the readout names
  the stage and its value. Hold Shift for fine moves; double-click a corner to put its
  stage back to its default. With HOLD at 0 its corner sits on ATTACK's: a press on it
  takes ATTACK, and a press just to its right pulls HOLD out. Each drag is one move for
  your DAW, so it records and undoes like a turn of the dial.

  Halfway along each ramp (the attack, the decay and the release) sits a small dot. Drag
  it up or down to bend the ramp, and the dot follows the pointer: bent up, the ramp
  bulges above a straight line, and bent down, it sags below it. An attack bent up leaps
  up and eases into the top. A decay or release bent down drops quickly and then tails
  off, the way a plucked string or a struck bell fades. The bend changes only the ramp's
  shape, never its time, and you hear it as well as see it. Double-click a dot to
  straighten its ramp. The bends are also parameters (Attack Curve, Decay Curve and
  Release Curve), so your DAW can automate them. A dot shows only while its ramp has
  room to bend: a decay to a SUSTAIN at the top has nothing to fall.
- **The response**, in each filter (above its three dials): how loud the filter leaves
  each frequency, 20 Hz at the left to 20 kHz at the right, with 0 dB marked. The arrows
  either side of the type's name step TYPE. The handle at the cutoff drags sideways for
  CUTOFF and up and down for RESONANCE, following the pointer (Shift for fine moves,
  double-click to reset both). Every note you play is a faint line where its sweep has
  its cutoff right now, so you can watch a pluck's filter close.
- **SWEEP**, in each filter (under its dials): the envelope that moves its cutoff, drawn
  and edited just like an AMP graph, with a dot per note riding it; its corner reads
  ENV AMT.

### Views

Below the signal flow, the gear menu picks how the cards show themselves, for every
module at once, saved with your set (presets leave it alone):

| Setting         | Choice      | What the card shows                                         |
|-----------------|-------------|-------------------------------------------------------------|
| **Envelopes**   | Knobs       | Just the six dials, on a shorter card                       |
|                 | Graph       | A big AMP graph to drag, filling the card, with no dials     |
|                 | Both        | The graph over the dials (the default)                      |
| **Oscillators** | Full        | The live wave over the dials (the default)                  |
|                 | Compact     | Just a small pill with the shape's icon and name, over the dials; its arrows, the wheel and a click (for the wave menu) still pick the shape |

A shorter card keeps its place: the bench still leaves room for the full-size card,
so switching views never moves anything.

### How the voices spread

Each oscillator spreads its own stack. With more than one voice, its copies sit evenly
across its DETUNE range. WIDTH pans
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
- The keyboard along the bottom (C3 to C6) plays the synth with the mouse, and lights
  up the keys your DAW is playing.

### In the standalone app

The standalone app (ZandersSynth2.app) plays on its own, with no DAW:

- **A MIDI keyboard** plugged in plays it straight away: the app listens to every MIDI
  input it finds, including ones plugged in while it runs. Options > Audio/MIDI
  Settings picks the sound output.
- **The computer's keys** play it too, laid out like a piano: the middle row from
  **A** to **;** plays the white keys from C, the row above (**W E T Y U O P**) the
  black keys, and **Z** / **X** shift down and up an octave (C1 to C7). A small "A" on
  the keyboard marks the key A plays (middle C to start with). Shortcuts with Cmd or
  Ctrl play nothing.

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

Set `SYNTH2_SNAPSHOTS` to a folder to have the editor tests save PNGs of the faces they
render (the empty bench, the trays, a full bench of four oscillators and four envelopes,
the flow modes, the cables, the bench zoomed in and out, the effects, a FILTER card) for a look by eye.

## Layout

```
Source/
  PluginProcessor.{h,cpp}    thin host wrapper + parameters (APVTS); the on-screen keyboard's lock-free hand-over; cables -> routing
  ParamIDs.h                 the parameter IDs
  Presets.h                  the factory presets, and loading one onto the bench
  Synth2Engine.h             host-agnostic DSP core: 16 voices, up to four oscillators, MIDI, smoothing, effects, output
  Effects.h                  the drive, the delay and the reverb (in any order), after the mix
  Room.h                     the reverb's room: pre-delay, diffusion and an eight-line feedback delay network
  EffectSettings.h           the effects' settings and sums (note lengths, tails, MIX), shared with the displays
  SynthVoice.h               one note: each oscillator's unison copies, through its filter (swept by its own envelope), under its envelope
  Envelope.h                 the delay / attack / hold / decay / sustain / release envelope
  Filter.h                   the filter: a state-variable filter (two stages for 24 dB) and its response, for drawing
  Oscillator.h               the four wave shapes, and the PolyBLEP oscillator that plays them
  Unison.h                   where each unison voice sits: its detune, its pan, the stack's level
  Bench.h                    the workbench model: modules, where they sit, cables and chains, which envelope and filter take which oscillator, older sessions
  BenchView.h                the bench itself: the dotted canvas the modules sit and are dragged on, its zoom and pan, the zoom pill
  BenchModules.h             the oscillator, filter and envelope modules, and the add / settings trays
  Rack.h                     the EFFECTS tab's model: which effects are in the chain, where their cards sit, the order
  EffectsView.h              the EFFECTS tab: the area, the drive, delay and reverb cards, their wells and switches
  PluginEditor.{h,cpp}       the Neon Plugins editor: header, tabs and gear, bench or effects, cables, keyboard, OUTPUT
  Synth2LookAndFeel.{h,cpp}  the dials (design-system Dial: arc + glowing core)
  WavePicker.h               an oscillator well's picker (pill, arrows, wheel) and its tray of shapes
  VoiceDisplays.h            the OSC, AMP (and SWEEP) and filter response wells
  NeonKeyboard.h             the on-screen keyboard, and playing it from the computer's keys
  StandaloneApp.cpp          the standalone app: JUCE's, but opening every MIDI keyboard plugged in
  Synth2Theme.h              shared palette, fonts, colour + value formatting
  Synth2VisualModel.h        pure wave / unison / envelope / effects -> display math (uses the engine's own), the morph and its ease
  RtSafety.h                 the RealtimeSanitizer annotation
Tests/
  Synth2EngineTests.cpp         framework-free DSP tests: pitch, envelopes, filters, unison, the oscillators, polyphony, pedal, effects
  Synth2VisualModelTests.cpp    framework-free tests for the shapes, the spread and the displays' model
  PluginTests.cpp               processor + editor: params, buses, MIDI, state, the bench, offscreen renders
  rt_check.cpp                  RealtimeSanitizer driver
  perf_bench.cpp                nanobench micro-benchmark
```

## UI

The editor follows the **Neon Plugins** design system natively in JUCE (no WebView):
the dark radial panel, the `Zanders` + product wordmark (Synth2 in violet), the
BENCH | EFFECTS tabs, the preset picker, the mode badge and the settings gear, then the bench: a dark, faintly dotted area where
the modules sit as cards, each with a header (its name, its place or OUT jack, a
cross, and on an envelope the oscillator it shapes) over its display and its Dials,
coloured along the cyan → violet → pink → amber spectrum. The oscillators show their
shape in the spectrum gradient with a faint copy per unison voice, with a picker pill
and a drop-open tray of glowing shape tiles; each envelope's AMP well the envelope in
its dials' colours, with a live dot per sounding note. Patch cables hang between the
jacks in the colour of the module they come from. Along the bottom, an ivory keyboard
whose pressed keys glow violet, and OUTPUT. The displays draw on the engine's own
wave, unison and envelope maths (`Synth2VisualModel`), so what they show never drifts
from what you hear. The face is authored at 1040 px and scales proportionally
(resizable from 75% to 200%, fixed aspect ratio).

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

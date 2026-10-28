/*
    Synth2Engine unit tests.

    Headless and framework-free: plain check() asserts plus an int main() that
    returns non-zero on failure, so it runs on any platform without a display and
    plugs straight into CTest. Notes go in as real MIDI through process(), the
    way a host plays them, and the tests listen to what comes out: pitch, level,
    the unison stack's spread, the envelope, polyphony and the sustain pedal.
*/

#include "Synth2Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const std::string& message)
{
    std::printf ("  [%s] %s\n", condition ? "PASS" : "FAIL", message.c_str());
    if (! condition)
        ++failures;
}

bool approx (float a, float b, float tolerance) { return std::abs (a - b) <= tolerance; }

constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

Synth2Engine::Settings defaults (Waveform wave = Waveform::saw, int voices = 1)
{
    Synth2Engine::Settings s;
    s.wave   = wave;
    s.voices = voices;
    return s;
}

std::unique_ptr<Synth2Engine> makeEngine (const Synth2Engine::Settings& settings)
{
    auto engine = std::make_unique<Synth2Engine>();
    engine->setSettings (settings);
    engine->prepare (sampleRate);
    return engine;
}

/** Renders `numSamples` in blocks; `events` are (sample, message) pairs in the
    whole render's time. */
juce::AudioBuffer<float> run (Synth2Engine& engine, int numSamples,
                              const std::vector<std::pair<int, juce::MidiMessage>>& events = {},
                              int numChannels = 2)
{
    juce::AudioBuffer<float> out (numChannels, numSamples);
    juce::AudioBuffer<float> block (numChannels, blockSize);

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int length = juce::jmin (blockSize, numSamples - start);
        block.setSize (numChannels, length, false, false, true);

        juce::MidiBuffer midi;
        for (const auto& [when, message] : events)
            if (when >= start && when < start + length)
                midi.addEvent (message, when - start);

        engine.process (block, midi);
        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom (ch, start, block, ch, 0, length);
    }
    return out;
}

juce::MidiMessage on (int note, float velocity = 1.0f) { return juce::MidiMessage::noteOn (1, note, velocity); }
juce::MidiMessage off (int note)                       { return juce::MidiMessage::noteOff (1, note); }
juce::MidiMessage pedal (bool down)                    { return juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0); }

float rms (const juce::AudioBuffer<float>& buffer, int channel, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += (double) buffer.getSample (channel, i) * buffer.getSample (channel, i);
    return (float) std::sqrt (sum / juce::jmax (1, end - start));
}

float peak (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    float p = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = start; i < end; ++i)
            p = juce::jmax (p, std::abs (buffer.getSample (ch, i)));
    return p;
}

/** The frequency of a steady tone, from its upward zero crossings. */
float frequencyOf (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    int crossings = 0, first = -1, last = -1;
    for (int i = start + 1; i < end; ++i)
        if (buffer.getSample (0, i - 1) < 0.0f && buffer.getSample (0, i) >= 0.0f)
        {
            ++crossings;
            if (first < 0) first = i;
            last = i;
        }
    if (crossings < 2)
        return 0.0f;
    return (float) ((crossings - 1) * sampleRate / (last - first));
}

// ---- Test 1: silence until a note, then the right pitch for every shape. ---
void testPitch()
{
    std::printf ("Pitch:\n");

    auto engine = makeEngine (defaults());
    const auto silent = run (*engine, blockSize * 4);
    check (peak (silent, 0, silent.getNumSamples()) == 0.0f, "no notes, no sound");

    static const char* names[] = { "sine", "triangle", "saw", "square" };
    for (int w = 0; w < 4; ++w)
    {
        auto e = makeEngine (defaults ((Waveform) w));
        const auto out = run (*e, (int) sampleRate, { { 0, on (69) } });
        const float hz = frequencyOf (out, 4800, out.getNumSamples());
        check (approx (hz, 440.0f, 0.5f), std::string (names[w]) + ": A4 plays at 440 Hz (got " + std::to_string (hz) + ")");
    }

    auto e = makeEngine (defaults());
    const auto c5 = run (*e, (int) sampleRate, { { 0, on (72) } });
    check (approx (frequencyOf (c5, 4800, c5.getNumSamples()), 523.25f, 0.6f), "C5 plays at 523.25 Hz");
}

// ---- Test 2: MIDI lands on the sample it was sent for. ------------------------
void testSampleAccurate()
{
    std::printf ("Sample-accurate MIDI:\n");

    auto engine = makeEngine (defaults());
    const auto out = run (*engine, blockSize * 2, { { 700, on (60) } });
    check (peak (out, 0, 700) == 0.0f, "silent before the note's sample");
    check (peak (out, 700, 760) > 0.0f, "sounding right after it, mid-block");
}

// ---- Test 3: the envelope rises, holds and releases to silence. --------------
void testEnvelope()
{
    std::printf ("Envelope:\n");

    auto settings = defaults();
    settings.attackSeconds  = 0.1f;
    settings.releaseSeconds = 0.1f;
    auto engine = makeEngine (settings);

    const int second = (int) sampleRate;
    const auto out = run (*engine, second, { { 0, on (60) }, { second / 2, off (60) } });

    const float early   = rms (out, 0, 0, 480);              // first 10 ms of a 100 ms attack
    const float held    = rms (out, 0, 9600, 19200);         // 200..400 ms
    const float tail    = rms (out, 0, second / 2 + 2400, second / 2 + 3600);
    const float silence = peak (out, second / 2 + 6000, second);

    check (early < 0.3f * held, "the attack fades in");
    check (held > 0.05f,        "the note holds at its level");
    check (tail < held && tail > 0.0f, "the release fades out after the key goes up");
    check (silence == 0.0f,     "and reaches silence after the release time");
    check (engine->getNumActiveNotes() == 0, "a finished note frees its voice");
}

// ---- Test 4: the unison stack: level, detune, width. ---------------------------
void testUnison()
{
    std::printf ("Unison:\n");

    const auto levelWith = [] (int voices, float detune, float width)
    {
        auto settings = defaults (Waveform::saw, voices);
        settings.detuneCents = detune;
        settings.width       = width;
        auto engine = makeEngine (settings);
        const auto out = run (*engine, (int) sampleRate, { { 0, on (57) } });
        return std::make_pair (0.5f * (rms (out, 0, 4800, out.getNumSamples()) + rms (out, 1, 4800, out.getNumSamples())), out);
    };

    const auto [one,   oneOut]   = levelWith (1, 15.0f, 0.5f);
    const auto [seven, sevenOut] = levelWith (7, 15.0f, 0.5f);
    const auto [sixteen, ignored] = levelWith (16, 15.0f, 0.5f);
    const float sevenDb   = juce::Decibels::gainToDecibels (seven / one);
    const float sixteenDb = juce::Decibels::gainToDecibels (sixteen / one);
    check (std::abs (sevenDb) < 3.0f && std::abs (sixteenDb) < 3.0f,
           "a stack of 7 or 16 voices is within 3 dB of a single voice ("
               + std::to_string (sevenDb) + " / " + std::to_string (sixteenDb) + " dB)");

    // One voice is mono, and centred.
    float sideOne = 0.0f;
    for (int i = 0; i < oneOut.getNumSamples(); ++i)
        sideOne = juce::jmax (sideOne, std::abs (oneOut.getSample (0, i) - oneOut.getSample (1, i)));
    check (sideOne < 1.0e-6f, "a single voice plays dead centre");

    // WIDTH 0 folds the stack to mono; WIDTH 1 spreads it.
    const auto [narrowLevel, narrow] = levelWith (6, 20.0f, 0.0f);
    const auto [wideLevel,   wide]   = levelWith (6, 20.0f, 1.0f);
    const auto sideOf = [] (const juce::AudioBuffer<float>& b)
    {
        double side = 0.0;
        for (int i = 4800; i < b.getNumSamples(); ++i)
        {
            const double d = (double) b.getSample (0, i) - b.getSample (1, i);
            side += d * d;
        }
        return (float) std::sqrt (side / (b.getNumSamples() - 4800));
    };
    check (sideOf (narrow) < 1.0e-5f, "WIDTH 0 keeps the stack in the centre");
    check (sideOf (wide) > 0.1f * wideLevel, "WIDTH 100% spreads the stack across left and right");
    juce::ignoreUnused (narrowLevel);

    // DETUNE makes the stack beat: its short-term level swells and dips, where
    // a stack tuned to unison holds steady.
    const auto wobble = [] (const juce::AudioBuffer<float>& b)
    {
        float lo = 1.0e9f, hi = 0.0f;
        for (int start = 4800; start + 2400 <= b.getNumSamples(); start += 2400)
        {
            const float level = rms (b, 0, start, start + 2400);
            lo = juce::jmin (lo, level);
            hi = juce::jmax (hi, level);
        }
        return hi / juce::jmax (1.0e-9f, lo);
    };
    const auto [tunedLevel,   tuned]   = levelWith (2, 0.0f,  0.0f);
    const auto [detunedLevel, detuned] = levelWith (2, 25.0f, 0.0f);
    juce::ignoreUnused (tunedLevel, detunedLevel);
    check (wobble (tuned) < 1.05f,  "with no DETUNE the voices hold together");
    check (wobble (detuned) > 1.5f, "DETUNE makes the voices beat against each other");

    // The beat comes at the voices' frequency difference.
    auto settings = defaults (Waveform::sine, 2);
    settings.detuneCents = 25.0f;
    settings.width       = 0.0f;
    auto engine = makeEngine (settings);
    const auto out = run (*engine, (int) sampleRate * 2, { { 0, on (69) } });
    const double expectedBeat = 440.0 * (Unison::ratio (25.0f) - Unison::ratio (-25.0f));
    // Count the envelope's dips: windows quieter than the ones either side.
    std::vector<float> env;
    for (int start = 4800; start + 480 <= out.getNumSamples(); start += 480)
        env.push_back (rms (out, 0, start, start + 480));
    int dips = 0;
    for (size_t i = 1; i + 1 < env.size(); ++i)
        if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < 0.3f * *std::max_element (env.begin(), env.end()))
            ++dips;
    const double seconds = (out.getNumSamples() - 4800) / sampleRate;
    check (std::abs (dips / seconds - expectedBeat) < 2.0,
           "two sine voices beat at their frequency difference (" + std::to_string (dips / seconds) + " vs "
               + std::to_string (expectedBeat) + " Hz)");
}

// ---- Test 5: changing VOICES mid-note fades rather than clicks. --------------
void testVoicesChangeMidNote()
{
    std::printf ("VOICES while a note plays:\n");

    auto settings = defaults (Waveform::sine, 1);
    auto engine = makeEngine (settings);
    auto out = run (*engine, blockSize * 20, { { 0, on (60) } });

    // Now jump to 16 voices and back, watching the largest sample-to-sample step.
    float largestStep = 0.0f;
    float previous = out.getSample (0, out.getNumSamples() - 1);
    for (const int voices : { 16, 1, 8 })
    {
        settings.voices = voices;
        engine->setSettings (settings);
        const auto more = run (*engine, blockSize * 8);
        for (int i = 0; i < more.getNumSamples(); ++i)
        {
            largestStep = juce::jmax (largestStep, std::abs (more.getSample (0, i) - previous));
            previous = more.getSample (0, i);
        }
    }
    // A 261 Hz sine at this level moves at most ~0.009 per sample; a switched
    // (unfaded) voice would jump by up to its full level, ~0.25.
    check (largestStep < 0.05f, "the stack grows and shrinks without a click (largest step "
                                    + std::to_string (largestStep) + ")");
}

// ---- Test 6: polyphony, stealing, retriggering. --------------------------------
void testPolyphony()
{
    std::printf ("Polyphony:\n");

    auto engine = makeEngine (defaults (Waveform::saw, 4));

    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n = 0; n < Synth2Engine::maxNotes; ++n)
        chord.push_back ({ n * 10, on (48 + n) });
    run (*engine, blockSize * 2, chord);
    check (engine->getNumActiveNotes() == Synth2Engine::maxNotes, "sixteen notes play at once");

    run (*engine, blockSize, { { 0, on (100) } });
    check (engine->getNumActiveNotes() == Synth2Engine::maxNotes, "a seventeenth note takes over a voice");
    check (engine->isNoteSounding (100) && ! engine->isNoteSounding (48) && engine->isNoteSounding (49),
           "with every key held, it takes the oldest note's");

    // The same key twice uses the same voice.
    auto single = makeEngine (defaults());
    run (*single, blockSize, { { 0, on (60) }, { 100, on (60) } });
    check (single->getNumActiveNotes() == 1, "playing a key again reuses its voice");

    // A note-on with velocity 0 is a note-off.
    run (*single, blockSize, { { 0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 0) } });
    run (*single, (int) sampleRate);
    check (single->getNumActiveNotes() == 0, "a note-on with velocity 0 releases the note");

    // Stealing prefers a releasing voice over a held one.
    auto steal = makeEngine (defaults());
    std::vector<std::pair<int, juce::MidiMessage>> held;
    for (int n = 0; n < Synth2Engine::maxNotes; ++n)
        held.push_back ({ n, on (40 + n) });
    held.push_back ({ 100, off (50) });   // one voice releasing...
    held.push_back ({ 200, on (90) });    // ...when a new note needs one
    run (*steal, blockSize, held);
    check (steal->isNoteSounding (90) && ! steal->isNoteSounding (50) && steal->isNoteSounding (40),
           "it takes a releasing note's voice before the oldest held one");

    // A big detuned chord stays finite and bounded.
    auto loud = makeEngine (defaults (Waveform::square, 16));
    std::vector<std::pair<int, juce::MidiMessage>> all;
    for (int n = 0; n < Synth2Engine::maxNotes; ++n)
        all.push_back ({ 0, on (36 + n * 3) });
    const auto big = run (*loud, (int) sampleRate, all);
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < big.getNumSamples(); ++i)
            finite = finite && std::isfinite (big.getSample (ch, i));
    check (finite, "sixteen notes of sixteen square voices stay finite");
    // The ceiling: every voice of every note at the top of its cycle, panned hard.
    const float ceiling = (float) Synth2Engine::maxNotes * SynthVoice::noteLevel
                        * Unison::stackGain ((float) Unison::maxVoices) * (float) Unison::maxVoices * std::sqrt (2.0f);
    check (peak (big, 0, big.getNumSamples()) < ceiling, "and bounded (peak " + std::to_string (peak (big, 0, big.getNumSamples())) + ")");
}

// ---- Test 7: the sustain pedal and the panic messages. -------------------------
void testPedalAndPanic()
{
    std::printf ("Sustain pedal and All Notes Off:\n");

    auto settings = defaults();
    settings.releaseSeconds = 0.05f;
    auto engine = makeEngine (settings);

    run (*engine, blockSize, { { 0, pedal (true) }, { 10, on (60) }, { 20, on (64) }, { 100, off (60) }, { 110, off (64) } });
    run (*engine, (int) sampleRate / 2);
    check (engine->getNumActiveNotes() == 2, "with the pedal down, released keys keep sounding");

    run (*engine, blockSize, { { 0, pedal (false) } });
    run (*engine, (int) sampleRate / 2);
    check (engine->getNumActiveNotes() == 0, "lifting the pedal releases them");

    run (*engine, blockSize, { { 0, on (60) }, { 0, on (67) } });
    run (*engine, blockSize, { { 0, juce::MidiMessage::allNotesOff (1) } });
    run (*engine, (int) sampleRate / 2);
    check (engine->getNumActiveNotes() == 0, "All Notes Off releases every note");

    run (*engine, blockSize, { { 0, on (60) } });
    const auto out = run (*engine, blockSize, { { 0, juce::MidiMessage::allSoundOff (1) } });
    check (engine->getNumActiveNotes() == 0 && peak (out, 0, out.getNumSamples()) == 0.0f, "All Sound Off silences at once");
}

// ---- Test 8: output gain and a mono bus. ----------------------------------------
void testOutputAndMono()
{
    std::printf ("Output and mono:\n");

    auto settings = defaults (Waveform::sine, 3);
    auto a = makeEngine (settings);
    settings.outputDb = -6.0f;
    auto b = makeEngine (settings);
    const auto loud  = run (*a, (int) sampleRate / 2, { { 0, on (60) } });
    const auto quiet = run (*b, (int) sampleRate / 2, { { 0, on (60) } });
    const float ratio = rms (quiet, 0, 4800, quiet.getNumSamples()) / rms (loud, 0, 4800, loud.getNumSamples());
    check (approx (juce::Decibels::gainToDecibels (ratio), -6.0f, 0.1f), "OUTPUT -6 dB is 6 dB quieter");

    auto mono = makeEngine (defaults (Waveform::saw, 5));
    const auto out = run (*mono, (int) sampleRate / 2, { { 0, on (60) } }, 1);
    check (rms (out, 0, 4800, out.getNumSamples()) > 0.05f, "a mono bus gets the whole stack");
}
} // namespace

int main()
{
    std::printf ("Synth2Engine tests\n==================\n");

    testPitch();
    testSampleAccurate();
    testEnvelope();
    testUnison();
    testVoicesChangeMidNote();
    testPolyphony();
    testPedalAndPanic();
    testOutputAndMono();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

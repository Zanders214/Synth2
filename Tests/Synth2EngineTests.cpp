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
    s.oscillators[0].wave = wave;
    s.oscillators[0].voices = voices;
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
    settings.envelopes[0].attackSeconds  = 0.1f;
    settings.envelopes[0].releaseSeconds = 0.1f;
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

// ---- Test 3b: the envelope's stages, on their own and in a note. -------------
void testEnvelopeStages()
{
    std::printf ("Envelope stages:\n");

    // The envelope itself, at 1 kHz so a millisecond is a sample.
    using Stage = Envelope::Stage;
    Envelope e;
    e.setSampleRate (1000.0);
    e.setParameters ({ 0.010f, 0.020f, 0.010f, 0.020f, 0.5f, 0.040f });
    const auto step = [&e] (int samples) { for (int i = 0; i < samples; ++i) e.getNextSample(); };
    check (! e.isActive() && approx (e.getLevel(), 0.0f, 1e-6f), "an envelope starts silent");
    e.noteOn();
    step (5);
    check (e.getStage() == Stage::delay && approx (e.getLevel(), 0.0f, 1e-6f) && approx (e.getProgress(), 0.5f, 1e-6f),
           "the key down, it waits out DELAY at silence");
    step (15);
    check (e.getStage() == Stage::attack && approx (e.getLevel(), 0.5f, 1e-4f), "then rises over ATTACK (halfway after half of it)");
    step (10);
    check (e.getStage() == Stage::hold && approx (e.getLevel(), 1.0f, 1e-6f), "stays at the top for HOLD");
    step (20);
    check (e.getStage() == Stage::decay && approx (e.getLevel(), 0.75f, 1e-4f), "falls over DECAY");
    step (20);
    check (e.getStage() == Stage::sustain && approx (e.getLevel(), 0.5f, 1e-4f), "to the SUSTAIN level, where it stays");
    step (500);
    check (e.getStage() == Stage::sustain && approx (e.getLevel(), 0.5f, 1e-6f), "for as long as the key is down");

    auto lower = e.getParameters();
    lower.sustain = 0.25f;
    e.setParameters (lower);
    e.getNextSample();
    check (e.getLevel() < 0.5f && e.getLevel() > 0.4f, "turning SUSTAIN down glides there rather than stepping");
    step (40);
    check (approx (e.getLevel(), 0.25f, 1e-6f), "and settles on it");

    e.noteOff();
    step (20);
    check (e.getStage() == Stage::release && approx (e.getLevel(), 0.125f, 1e-4f) && approx (e.getProgress(), 0.5f, 1e-3f),
           "the key up, it falls from where it was over RELEASE");
    step (21);
    check (! e.isActive() && approx (e.getLevel(), 0.0f, 1e-6f), "to silence");

    e.setParameters ({ 0.0f, 0.020f, 0.0f, 0.020f, 1.0f, 0.040f });
    e.noteOn();
    e.getNextSample();
    check (e.getStage() == Stage::attack && e.getLevel() > 0.0f, "with no DELAY, it rises at once");
    step (25);
    check (e.getStage() == Stage::sustain && approx (e.getLevel(), 1.0f, 1e-6f), "and with no HOLD and full SUSTAIN, it stays at the top");
    e.noteOff();
    step (20);
    const float caught = e.getLevel();
    e.noteOn();
    e.getNextSample();
    check (e.getLevel() > caught, "played again mid-release, it climbs from where it was rather than from silence");

    // A long, shallow decay still gets there: its steps are far finer than a
    // float can resolve near the top.
    Envelope slow;
    slow.setSampleRate (44100.0);
    slow.setParameters ({ 0.0f, 0.001f, 0.0f, 10.0f, 0.99f, 0.3f });
    slow.noteOn();
    for (int i = 0; i < 44100 * 5; ++i)
        slow.getNextSample();
    check (slow.getStage() == Stage::decay && slow.getProgress() > 0.45f && slow.getProgress() < 0.55f,
           "a 10 s DECAY to 99% is halfway after 5 s");
    for (int i = 0; i < 44100 * 6; ++i)
        slow.getNextSample();
    check (slow.getStage() == Stage::sustain && approx (slow.getLevel(), 0.99f, 1e-6f), "and reaches SUSTAIN after 10 s");

    // In a note: DELAY holds it back, and a low SUSTAIN lets it fall after the hit.
    auto settings = defaults();
    auto& shape = settings.envelopes[0];
    shape.delaySeconds  = 0.05f;
    shape.attackSeconds = 0.005f;
    shape.decaySeconds  = 0.05f;
    shape.sustainLevel  = 0.25f;
    auto engine = makeEngine (settings);
    const auto out = run (*engine, (int) sampleRate / 2, { { 0, on (60) } });
    const int ms = (int) (sampleRate / 1000.0);
    const float waiting = peak (out, 0, 45 * ms);
    const float hit     = peak (out, 54 * ms, 57 * ms);
    const float settled = peak (out, 200 * ms, 300 * ms);
    check (waiting == 0.0f, "a note waits out its DELAY in silence");
    check (hit > 0.05f, "then sounds");
    check (approx (settled / hit, 0.25f, 0.03f),
           "and falls to a quarter of its peak with SUSTAIN at 25% (got " + std::to_string (settled / hit) + ")");
}

// ---- Test 4: the unison stack: level, detune, width. ---------------------------
void testUnison()
{
    std::printf ("Unison:\n");

    const auto levelWith = [] (int voices, float detune, float width)
    {
        auto settings = defaults (Waveform::saw, voices);
        settings.oscillators[0].detuneCents = detune;
        settings.oscillators[0].width       = width;
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
    settings.oscillators[0].detuneCents = 25.0f;
    settings.oscillators[0].width       = 0.0f;
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
        settings.oscillators[0].voices = voices;
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

// ---- The two oscillators: pitch, fine, level, and OSC 2 on and off. ---------
void testOscillators()
{
    std::printf ("Oscillators:\n");

    const float c4 = 261.626f;
    const auto steadyPitch = [] (const Synth2Engine::Settings& settings)
    {
        auto engine = makeEngine (settings);
        const auto out = run (*engine, blockSize * 16, { { 0, on (60) } });
        return frequencyOf (out, blockSize * 4, out.getNumSamples());
    };
    const auto steadyLevel = [] (const Synth2Engine::Settings& settings)
    {
        auto engine = makeEngine (settings);
        const auto out = run (*engine, blockSize * 16, { { 0, on (60) } });
        return rms (out, 0, blockSize * 4, out.getNumSamples());
    };

    auto settings = defaults (Waveform::sine);
    check (! settings.oscillators[1].on && ! settings.oscillators[2].on && ! settings.oscillators[3].on, "only OSC 1 starts switched on");
    check (approx (steadyPitch (settings), c4, 1.0f), "OSC 1 alone plays the note");

    auto up = settings;
    up.oscillators[0].semitones = 12;
    check (approx (steadyPitch (up), 2.0f * c4, 2.0f), "OSC 1's PITCH +12 plays an octave up");
    up.oscillators[0].semitones = -7;
    check (approx (steadyPitch (up), c4 * std::exp2 (-7.0f / 12.0f), 1.0f), "and -7 a fifth down");
    up.oscillators[0].semitones = 0;
    up.oscillators[0].fineCents = 50.0f;
    check (approx (steadyPitch (up), c4 * std::exp2 (0.5f / 12.0f), 1.0f), "FINE +50 tunes it a quarter tone sharp");

    auto half = settings;
    half.oscillators[0].level = 0.5f;
    check (approx (steadyLevel (half) / steadyLevel (settings), 0.5f, 0.02f), "OSC 1 at 50% LEVEL is half as loud");

    // OSC 2 on its own: OSC 1 turned all the way down.
    auto second = settings;
    second.oscillators[0].level = 0.0f;
    second.oscillators[1].wave = Waveform::sine;
    second.oscillators[1].semitones = 12;
    second.oscillators[1].level = 1.0f;
    check (steadyLevel (second) < 1e-6f, "switched off, OSC 2 makes no sound");
    second.oscillators[1].on = true;
    check (approx (steadyPitch (second), 2.0f * c4, 2.0f), "switched on, it plays at its own PITCH");
    check (approx (steadyLevel (second), steadyLevel (settings), steadyLevel (settings) * 0.02f),
           "at the same LEVEL, as loud as OSC 1");

    // Both, an octave apart: the octave shows up alongside the note.
    auto both = second;
    both.oscillators[0].level = 1.0f;
    check (steadyLevel (both) > 1.2f * steadyLevel (settings), "together they're louder than OSC 1 alone");

    // Each oscillator has its own unison stack: OSC 1's sixteen wide copies
    // leave OSC 2 a single centred voice, and OSC 2 spreads by its own.
    const auto spreadOf = [] (const Synth2Engine::Settings& stack)
    {
        auto engine = makeEngine (stack);
        const auto out = run (*engine, blockSize * 16, { { 0, on (60) } });
        double difference = 0.0;
        for (int i = blockSize * 4; i < out.getNumSamples(); ++i)
            difference += std::abs (out.getSample (0, i) - out.getSample (1, i));
        return difference;
    };
    auto stacked = second;
    stacked.oscillators[0].voices      = 16;
    stacked.oscillators[0].detuneCents = 30.0f;
    stacked.oscillators[0].width       = 1.0f;
    check (spreadOf (stacked) < 1e-3, "OSC 1's unison stack doesn't spread OSC 2");
    stacked.oscillators[1].voices      = 16;
    stacked.oscillators[1].detuneCents = 30.0f;
    stacked.oscillators[1].width       = 1.0f;
    check (spreadOf (stacked) > 1.0, "OSC 2's own does");

    // Switching OSC 2 on and off, and moving the levels, while a note plays.
    auto live = both;
    live.oscillators[1].on = false;
    auto engine = makeEngine (live);
    auto out = run (*engine, blockSize * 8, { { 0, on (60) } });
    float largestStep = 0.0f;
    float previous = out.getSample (0, out.getNumSamples() - 1);
    for (int change = 0; change < 6; ++change)
    {
        live.oscillators[1].on = change % 2 == 0;
        live.oscillators[0].level = change == 4 ? 0.0f : 1.0f;
        engine->setSettings (live);
        const auto more = run (*engine, blockSize * 4);
        for (int i = 0; i < more.getNumSamples(); ++i)
        {
            largestStep = juce::jmax (largestStep, std::abs (more.getSample (0, i) - previous));
            previous = more.getSample (0, i);
        }
    }
    // An octave-up sine at this level moves at most ~0.02 per sample; switching
    // an oscillator in or out unfaded would jump by up to its full level, ~0.25.
    check (largestStep < 0.05f, "OSC 2 fades in and out, and the levels glide, without a click (largest step "
                                    + std::to_string (largestStep) + ")");
}

// ---- Test 5b: the bench's modules: four oscillators, routing, unison and envelope off.
void testModules()
{
    std::printf ("Modules:\n");

    const auto levelOf = [] (const Synth2Engine::Settings& settings, int start = blockSize * 4, int length = blockSize * 16)
    {
        auto engine = makeEngine (settings);
        const auto out = run (*engine, length, { { 0, on (60) } });
        return rms (out, 0, start, out.getNumSamples());
    };

    auto settings = defaults (Waveform::sine);
    const float one = levelOf (settings);

    auto none = settings;
    none.oscillators[0].on = false;
    check (levelOf (none) < 1e-6f, "with every oscillator off, a note makes no sound");

    // Each of OSC 2..4 alone, as a sine an octave apart from the others, at
    // full level, plays as loud as OSC 1 did.
    bool eachPlays = true;
    for (size_t k = 1; k < (size_t) Synth2Engine::numOscillators; ++k)
    {
        auto alone = none;
        alone.oscillators[k] = { true, true, Waveform::sine, 0, 0.0f, 1.0f };
        eachPlays = eachPlays && approx (levelOf (alone), one, one * 0.02f);
    }
    check (eachPlays, "OSC 2, 3 and 4 each play on their own");

    // Four sines at different octaves add like independent signals: about
    // twice OSC 1's level.
    auto four = settings;
    for (size_t k = 0; k < (size_t) Synth2Engine::numOscillators; ++k)
        four.oscillators[k] = { true, true, Waveform::sine, (int) k * 12 - 12, 0.0f, 1.0f };
    check (approx (levelOf (four), 2.0f * one, 0.1f * one), "all four play together");

    auto unrouted = four;
    for (auto& oscillator : unrouted.oscillators)
        oscillator.routed = false;
    unrouted.oscillators[2].routed = true;
    check (approx (levelOf (unrouted), one, one * 0.02f), "an oscillator not routed to the output is silent");

    // No envelope: the note opens in a few milliseconds and stops soon after
    // the key goes up, whatever the envelopes say.
    auto gate = settings;
    gate.envelopes[0].attackSeconds  = 2.0f;
    gate.envelopes[0].releaseSeconds = 5.0f;
    gate.oscillators[0].envelope     = -1;
    {
        auto engine = makeEngine (gate);
        const auto out = run (*engine, blockSize * 8, { { 0, on (60) }, { blockSize * 4, off (60) } });
        const int afterAttack  = (int) (0.01 * sampleRate);
        const int afterRelease = blockSize * 4 + (int) (0.05 * sampleRate);
        check (rms (out, 0, afterAttack, afterAttack + 256) > 0.5f * one, "with no envelope, a note opens at once");
        check (rms (out, 0, afterRelease, out.getNumSamples()) < 1e-4f && engine->getNumActiveNotes() == 0,
               "and is gone a few milliseconds after the key goes up");
    }

    // Each oscillator follows its own envelope: OSC 1 under a 2 s attack is
    // still near silent while OSC 2, with none, plays at once.
    auto shaped = settings;
    shaped.envelopes[0].attackSeconds = 2.0f;
    shaped.envelopes[0].releaseSeconds = 2.0f;
    shaped.oscillators[1] = shaped.oscillators[0];
    shaped.oscillators[1].envelope = -1;
    auto alone = shaped;
    alone.oscillators[0].on = false;
    {
        auto engine = makeEngine (shaped);
        auto reference = makeEngine (alone);
        const std::vector<std::pair<int, juce::MidiMessage>> press { { 0, on (60) }, { blockSize * 8, off (60) } };
        const auto out  = run (*engine, blockSize * 16, press);
        const auto only = run (*reference, blockSize * 16, press);
        const int early = (int) (0.02 * sampleRate);
        check (approx (rms (out, 0, early, early + 512), rms (only, 0, early, early + 512), 0.05f * one),
               "early on, only the oscillator without an envelope is heard");
        check (engine->getActivity (0)[0].stage == NoteActivity::Stage::release && engine->getActivity (1)[0].stage == NoteActivity::Stage::idle,
               "after the key goes up, OSC 1's envelope releases, and OSC 2's short gate is already over");
        check (engine->getNumActiveNotes() == 1 && reference->getNumActiveNotes() == 0,
               "and the note lasts until its slowest envelope has finished");
    }
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
    settings.envelopes[0].releaseSeconds = 0.05f;
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

// ---- Test 8: every MIDI channel keeps its own notes and pedal. -----------------
void testChannels()
{
    std::printf ("MIDI channels:\n");

    auto settings = defaults();
    settings.envelopes[0].releaseSeconds = 0.05f;
    auto engine = makeEngine (settings);
    const auto onCh  = [] (int ch, int note) { return juce::MidiMessage::noteOn (ch, note, 1.0f); };
    const auto offCh = [] (int ch, int note) { return juce::MidiMessage::noteOff (ch, note); };

    run (*engine, blockSize, { { 0, onCh (1, 60) }, { 10, onCh (2, 60) } });
    check (engine->getNumActiveNotes() == 2, "the same key on two channels plays two voices");

    run (*engine, blockSize, { { 0, offCh (1, 60) } });
    run (*engine, (int) sampleRate / 2);
    check (! engine->isNoteSounding (1, 60) && engine->isNoteSounding (2, 60),
           "letting go on channel 1 leaves channel 2's note held");

    run (*engine, blockSize, { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 10, offCh (2, 60) } });
    run (*engine, (int) sampleRate / 2);
    check (engine->isSustainPedalDown (1) && ! engine->isSustainPedalDown (2) && ! engine->isNoteSounding (2, 60),
           "channel 1's sustain pedal doesn't hold channel 2's notes");

    run (*engine, blockSize, { { 0, onCh (1, 64) }, { 0, onCh (3, 67) }, { 100, offCh (1, 64) } });
    run (*engine, blockSize, { { 0, juce::MidiMessage::allSoundOff (3) } });
    check (engine->isNoteSounding (1, 64) && ! engine->isNoteSounding (3, 67),
           "All Sound Off on channel 3 leaves channel 1's (sustained) note alone");

    run (*engine, blockSize, { { 0, juce::MidiMessage::allNotesOff (1) } });
    run (*engine, (int) sampleRate / 2);
    check (engine->getNumActiveNotes() == 0 && ! engine->isSustainPedalDown (1),
           "All Notes Off on channel 1 lifts its pedal and releases its notes");
}

// ---- Test 9: output gain and a mono bus. ----------------------------------------
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

// ---- Effects ----------------------------------------------------------------

/** Runs `in` (a stereo pair) through a chain set up with `settings`, in the
    engine's 32-sample chunks, after `warmUp` samples of silence. */
std::vector<std::vector<float>> throughEffects (const Effects::Settings& settings, std::vector<std::vector<float>> in, int warmUp = 0)
{
    Effects::Chain chain;
    chain.set (settings);
    chain.prepare (sampleRate);

    std::array<float, 32> silenceL {};
    std::array<float, 32> silenceR {};
    for (int done = 0; done < warmUp; done += 32)
    {
        silenceL.fill (0.0f);
        silenceR.fill (0.0f);
        chain.process (silenceL.data(), silenceR.data(), 32);
    }

    const int numSamples = (int) in[0].size();
    for (int done = 0; done < numSamples; done += 32)
        chain.process (in[0].data() + done, in[1].data() + done, std::min (32, numSamples - done));
    return in;
}

std::vector<std::vector<float>> impulse (int numSamples, float left = 1.0f, float right = 1.0f)
{
    std::vector<std::vector<float>> in (2, std::vector<float> ((size_t) numSamples, 0.0f));
    in[0][0] = left;
    in[1][0] = right;
    return in;
}

/** A 2 ms burst of 1 kHz at `level` on both sides, for timing and levels
    that don't hang on a single sample (the delay reads between samples). */
std::vector<std::vector<float>> blip (int numSamples, float level)
{
    std::vector<std::vector<float>> in (2, std::vector<float> ((size_t) numSamples, 0.0f));
    for (size_t i = 0; i < 96; ++i)
    {
        const double t = (double) i / 96.0;
        in[0][i] = in[1][i] = level * (float) (std::sin (juce::MathConstants<double>::pi * t) * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / sampleRate));
    }
    return in;
}

/** Where the loudest sample in [from, to) is, and how loud. */
std::pair<int, float> loudest (const std::vector<float>& x, int from, int to)
{
    int   at   = from;
    float best = 0.0f;
    for (int i = from; i < to; ++i)
        if (std::abs (x[(size_t) i]) > best)
        {
            best = std::abs (x[(size_t) i]);
            at   = i;
        }
    return { at, best };
}

/** Where the sound in [from, to) is centred, weighted by its energy. */
double centre (const std::vector<float>& x, int from, int to)
{
    double weighted = 0.0;
    double total    = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double e = (double) x[(size_t) i] * x[(size_t) i];
        weighted += e * i;
        total    += e;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

float energy (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return (float) sum;
}

void testDelay()
{
    std::printf ("Delay:\n");

    Effects::Settings settings;
    settings.delay.on       = true;
    settings.delay.timeMs   = 375.0f;
    settings.delay.feedback = 0.5f;
    settings.delay.tone     = 1.0f;
    settings.delay.mix      = 0.5f;
    const int echo = (int) (0.375 * sampleRate);   // 18000 samples
    // The tape's waver, in samples, and a couple more for the filters' own lag.
    const int wow  = (int) std::ceil (Effects::Delay::wowMs * 0.001 * sampleRate) + 2;
    const float hit = 0.25f;   // an ordinary level, below where the repeats start to round off

    const auto out = throughEffects (settings, blip (3 * echo, hit));
    const auto dry = loudest (out[0], 0, 96);
    check (approx (dry.second, loudest (blip (96, hit)[0], 0, 96).second, 1.0e-6f), "at half MIX the dry sound passes at full level");
    const auto first  = loudest (out[0], 200, echo + echo / 2);
    const auto second = loudest (out[0], echo + echo / 2, 2 * echo + echo / 2);
    const double start = centre (out[0], 0, 200);
    const double late  = centre (out[0], 200, echo + echo / 2) - start - echo;
    check (std::abs (late) <= wow, "the first repeat comes TIME later, give or take the waver (" + std::to_string (late) + " samples)");
    check (std::abs (centre (out[0], echo + echo / 2, 2 * echo + echo / 2) - start - 2 * echo) <= 2 * wow, "and the next TIME after that");
    check (first.second > 0.8f * dry.second, "the first repeat is near full level at the brightest TONE");
    check (approx (second.second / first.second, 0.5f, 0.1f), "each repeat is FEEDBACK as loud as the last");
    check (energy (out[0], 200, echo - 200) < 1.0e-12f, "nothing sounds between the repeats");

    auto dark = settings;
    dark.delay.tone = 0.0f;
    const auto darker = throughEffects (dark, impulse (2 * echo, hit, hit));
    const auto bright = throughEffects (settings, impulse (2 * echo, hit, hit));
    check (energy (darker[0], 200, echo + echo / 2) < 0.3f * energy (bright[0], 200, echo + echo / 2), "a low TONE dulls the repeats");

    auto wet = settings;
    wet.delay.mix = 1.0f;
    const auto allWet = throughEffects (wet, impulse (2 * echo, hit, hit));
    check (std::abs (allWet[0][0]) < 1.0e-6f, "at full MIX only the repeats are left");

    auto pingPong = settings;
    pingPong.delay.pingPong = true;
    const auto bounced = throughEffects (pingPong, blip (3 * echo, hit));
    check (loudest (bounced[0], 200, echo + echo / 2).second > 0.8f * dry.second
               && loudest (bounced[1], 200, echo + echo / 2).second < 1.0e-6f,
           "PING-PONG: the first repeat is on the left");
    check (loudest (bounced[1], echo + echo / 2, 2 * echo + echo / 2).second > 0.3f * dry.second
               && loudest (bounced[0], echo + echo / 2, 2 * echo + echo / 2).second < 1.0e-6f,
           "and the next on the right");

    auto synced = settings;
    synced.delay.sync     = true;
    synced.delay.division = 8;   // 1/4
    synced.bpm            = 100.0;
    const int beat = (int) (0.6 * sampleRate);
    const auto onTheBeat = throughEffects (synced, blip (beat + beat / 2, hit));
    const double onBeat = centre (onTheBeat[0], 200, beat + beat / 2) - start - beat;
    check (std::abs (onBeat) <= wow, "SYNC: a quarter note at 100 BPM waits 600 ms (" + std::to_string (onBeat) + " samples off)");
    check (std::abs (Effects::delaySeconds (synced.delay, 140.0) - 60.0 / 140.0) < 1.0e-9, "and follows the tempo");
    synced.delay.division = 11;   // 1/1
    check (Effects::delaySeconds (synced.delay, 30.0) <= Effects::maxSeconds, "but never longer than the line");

    // Like tape: the lows thin out pass by pass, and a hard hit is rounded off.
    auto low = settings;
    low.delay.mix = 1.0f;
    std::vector<std::vector<float>> hum (2, std::vector<float> ((size_t) (3 * echo)));
    for (size_t i = 0; i < hum[0].size(); ++i)
        hum[0][i] = hum[1][i] = i < (size_t) echo / 2 ? 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * (double) i / sampleRate) : 0.0f;
    const auto hummed = throughEffects (low, hum);
    const float firstHum  = loudest (hummed[0], echo, echo + echo / 2).second;
    const float secondHum = loudest (hummed[0], 2 * echo, 2 * echo + echo / 2).second;
    check (secondHum < 0.4f * firstHum, "a 50 Hz hum fades faster than FEEDBACK alone would take it");
    const auto loud = throughEffects (wet, impulse (2 * echo, 3.0f, 3.0f));
    check (loudest (loud[0], 1, echo + echo / 2).second < 2.2f, "and a hard hit comes back rounded, not clipped");
}

void testReverb()
{
    std::printf ("Reverb:\n");

    Effects::Settings settings;
    settings.reverb.on  = true;
    settings.reverb.mix = 1.0f;
    const int second = (int) sampleRate;
    const int warmUp = 4800;   // the reverb's own gains glide in from silence

    auto small = settings;
    small.reverb.size = 0.3f;
    auto large = settings;
    large.reverb.size = 0.9f;
    const auto smallRoom = throughEffects (small, impulse (2 * second), warmUp);
    const auto largeRoom = throughEffects (large, impulse (2 * second), warmUp);
    check (energy (smallRoom[0], 0, second / 2) > 1.0e-4f, "an impulse sets the room ringing");
    check (energy (largeRoom[0], second, 2 * second) > 10.0f * energy (smallRoom[0], second, 2 * second),
           "a bigger SIZE rings on for longer");

    // Noise in, at full MIX: the room should sit near the dry sound's level.
    juce::Random random (7);
    std::vector<std::vector<float>> noise (2, std::vector<float> ((size_t) second));
    for (auto& side : noise)
        for (auto& x : side)
            x = random.nextFloat() * 2.0f - 1.0f;
    const auto room = throughEffects (settings, noise, warmUp);
    const float level = std::sqrt (energy (room[0], second / 2, second) / energy (noise[0], second / 2, second));
    check (level > 0.25f && level < 4.0f, "the room sits near the dry level (" + std::to_string (level) + "x)");

    auto narrow = settings;
    narrow.reverb.width = 0.0f;
    const auto mono = throughEffects (narrow, impulse (second / 2, 1.0f, 0.0f), warmUp);
    float difference = 0.0f;
    for (size_t i = 0; i < mono[0].size(); ++i)
        difference = std::max (difference, std::abs (mono[0][i] - mono[1][i]));
    check (difference < 1.0e-5f, "at no WIDTH the room is the same on both sides");

    check (Effects::reverbSeconds (0.9f) > 2.0 * Effects::reverbSeconds (0.3f), "the tail estimate grows with SIZE");

    // How long it really rings: the time from -5 to -35 dB, doubled, against
    // what SIZE says (with no DAMPING to shorten the highs).
    for (const float size : { 0.3f, 0.5f, 0.7f })
    {
        auto ring = settings;
        ring.reverb.size     = size;
        ring.reverb.damping  = 0.0f;
        ring.reverb.preDelayMs = 0.0f;
        const double expected = Effects::reverbSeconds (size);
        const int length = (int) ((expected + 0.5) * sampleRate);
        const auto rang = throughEffects (ring, impulse (length), warmUp);

        // The energy left from each 10 ms on (backward integration).
        const int window = 480;
        std::vector<double> left ((size_t) (length / window), 0.0);
        double total = 0.0;
        for (int w = (int) left.size() - 1; w >= 0; --w)
        {
            total += energy (rang[0], w * window, (w + 1) * window) + energy (rang[1], w * window, (w + 1) * window);
            left[(size_t) w] = total;
        }
        const auto when = [&left] (double db)
        {
            for (size_t w = 0; w < left.size(); ++w)
                if (10.0 * std::log10 (left[w] / left[0]) < db)
                    return (double) w * 0.01;
            return 1.0e9;
        };
        const double measured = 2.0 * (when (-35.0) - when (-5.0));
        check (std::abs (measured / expected - 1.0) < 0.25,
               "SIZE " + std::to_string (size) + " rings " + std::to_string (expected) + " s (measured " + std::to_string (measured) + " s)");
    }

    // Not metallic: in the tail, no echo lines up with itself (a strong
    // repeat in the autocorrelation is the comb ring Freeverb is known for).
    auto pad = settings;
    pad.reverb.size    = 0.8f;
    pad.reverb.damping = 0.3f;
    const auto tail = throughEffects (pad, impulse (second), warmUp);
    const int from = second / 4;
    const int to   = second / 2;
    const double self = energy (tail[0], from, to);
    double strongest = 0.0;
    for (int lag = 48; lag < 4800; ++lag)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += (double) tail[0][(size_t) i] * tail[0][(size_t) (i + lag)];
        strongest = std::max (strongest, std::abs (sum) / self);
    }
    check (strongest < 0.3, "the tail has no ringing repeat in it (strongest " + std::to_string (strongest) + ")");

    // PRE-DELAY: silence first, then the room.
    auto waits = settings;
    waits.reverb.preDelayMs = 100.0f;
    const auto waited = throughEffects (waits, impulse (second / 2), warmUp);
    check (energy (waited[0], 0, 4500) < 1.0e-10f && energy (waited[0], 4800, 9600) > 1.0e-4f, "PRE-DELAY holds the room back");
}

void testEffectsChain()
{
    std::printf ("Effects chain:\n");

    // Off, with everything else turned: untouched.
    Effects::Settings off;
    off.delay.timeMs   = 120.0f;
    off.delay.feedback = 0.9f;
    off.delay.mix      = 1.0f;
    off.reverb.mix     = 1.0f;
    juce::Random random (11);
    std::vector<std::vector<float>> noise (2, std::vector<float> (4800));
    for (auto& side : noise)
        for (auto& x : side)
            x = random.nextFloat() - 0.5f;
    check (throughEffects (off, noise) == noise, "both effects off leave the sound untouched");

    Effects::Settings both;
    both.delay.on  = true;
    both.delay.mix = 1.0f;
    both.reverb.on = true;
    auto reversed = both;
    reversed.reverbFirst = true;
    check (throughEffects (both, impulse (24000), 4800) != throughEffects (reversed, impulse (24000), 4800),
           "the order matters");

    // Switching the delay off fades it out and forgets its repeats.
    Effects::Settings delay;
    delay.delay.on       = true;
    delay.delay.timeMs   = 100.0f;
    delay.delay.feedback = 0.9f;
    Effects::Chain chain;
    chain.set (delay);
    chain.prepare (sampleRate);
    auto in = impulse (9600);
    for (int done = 0; done < 9600; done += 32)
        chain.process (in[0].data() + done, in[1].data() + done, 32);
    delay.delay.on = false;
    chain.set (delay);
    std::vector<float> l (4800, 0.0f);
    std::vector<float> r (4800, 0.0f);
    for (int done = 0; done < 4800; done += 32)
        chain.process (l.data() + done, r.data() + done, 32);
    check (energy (l, 1920, 4800) < 1.0e-12f, "switched off, the repeats stop within the fade");
    delay.delay.on = true;
    chain.set (delay);
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    for (int done = 0; done < 4800; done += 32)
        chain.process (l.data() + done, r.data() + done, 32);
    check (energy (l, 0, 4800) < 1.0e-12f, "and switched on again, it starts clean");

    Effects::Settings tails;
    check (Effects::tailSeconds (tails) < 1.0e-9, "with both off there is no tail");
    tails.delay.on       = true;
    tails.delay.timeMs   = 500.0f;
    tails.delay.feedback = 0.5f;
    check (std::abs (Effects::tailSeconds (tails) - 0.5 * (1.0 + std::log (0.001) / std::log (0.5))) < 1.0e-6,
           "the delay's tail lasts until its repeats fall 60 dB");
    tails.reverb.on = true;
    check (Effects::tailSeconds (tails) > 5.5, "and the reverb's ring follows it");
}

void testEngineEffects()
{
    std::printf ("Engine effects:\n");

    // A short note, then silence: the delay's repeats ring on after it.
    auto settings = defaults (Waveform::sine);
    settings.envelopes[0].releaseSeconds = 0.005f;
    settings.effects.delay.on       = true;
    settings.effects.delay.timeMs   = 250.0f;
    settings.effects.delay.feedback = 0.4f;
    auto engine = makeEngine (settings);
    const int note = (int) (0.05 * sampleRate);
    const auto out = run (*engine, (int) sampleRate, { { 0, on (69) }, { note, off (69) } });
    const int after = (int) (0.25 * sampleRate);
    check (rms (out, 0, note + 2400, after) < 1.0e-6f, "silence between the note and its repeat");
    check (rms (out, 0, after, after + note) > 0.05f, "the note comes back TIME later");

    auto dry = defaults (Waveform::sine);
    dry.envelopes[0].releaseSeconds = 0.005f;
    auto quiet = makeEngine (dry);
    const auto plain = run (*quiet, (int) sampleRate, { { 0, on (69) }, { note, off (69) } });
    check (rms (plain, 0, after, after + note) < 1.0e-6f, "with the effects off the note just stops");

    // All Sound Off silences the repeats too.
    auto panic = makeEngine (settings);
    const auto stopped = run (*panic, (int) sampleRate,
                              { { 0, on (69) }, { note, off (69) }, { note + 4800, juce::MidiMessage::allSoundOff (1) } });
    check (rms (stopped, 0, after, after + note) < 1.0e-6f, "All Sound Off silences the repeats");
}
} // namespace

int main()
{
    std::printf ("Synth2Engine tests\n==================\n");

    testPitch();
    testSampleAccurate();
    testEnvelope();
    testEnvelopeStages();
    testUnison();
    testVoicesChangeMidNote();
    testOscillators();
    testModules();
    testPolyphony();
    testPedalAndPanic();
    testChannels();
    testOutputAndMono();
    testDelay();
    testReverb();
    testEffectsChain();
    testEngineEffects();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

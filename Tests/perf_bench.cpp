// nanobench micro-benchmark for Synth2's audio callback.
//
// Measures the per-block cost of a held chord at 48 kHz / 512 and emits a
// github-action-benchmark "customSmallerIsBetter" JSON file so the perf workflow
// can chart the trend and alert on regressions. Two loads: a typical one (an
// 8-note chord of 7 detuned saws) and the worst case (16 notes of 16 voices).
// The headline metric is the "DSP load %" = ns_per_block / (blockSize / sampleRate);
// < 100% = real-time capable.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include <cstdio>

namespace
{
double benchChord (const char* name, int notes, int voices, ankerl::nanobench::Bench& bench, bool bothOscillators = false)
{
    const double sr = 48000.0;
    const int    bs = 512;

    Synth2AudioProcessor proc;
    proc.prepareToPlay (sr, bs);
    auto* v = proc.getState().getParameter (ParamID::voices);
    v->setValueNotifyingHost (v->convertTo0to1 ((float) voices));
    auto* r = proc.getState().getParameter (ParamID::release);
    r->setValueNotifyingHost (r->convertTo0to1 (10.0f));
    proc.getState().getParameter (ParamID::osc2On)->setValueNotifyingHost (bothOscillators ? 1.0f : 0.0f);

    juce::AudioBuffer<float> buffer (2, bs);
    juce::MidiBuffer chord;
    for (int n = 0; n < notes; ++n)
        chord.addEvent (juce::MidiMessage::noteOn (1, 48 + n * 3, 0.8f), 0);
    proc.processBlock (buffer, chord);   // the notes are held from here on

    bench.run (name, [&]
    {
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        ankerl::nanobench::doNotOptimizeAway (buffer.getReadPointer (0)[0]);
    });

    // GOTCHA: the Measure enum is nested in Result, not the namespace.
    return bench.results().back().median (ankerl::nanobench::Result::Measure::elapsed) * 1.0e9;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int    bs = 512;

    ankerl::nanobench::Bench bench;
    bench.title ("DSP @48k/512").unit ("block").warmup (20).minEpochIterations (200);

    const double typicalNs = benchChord ("processBlock 8 notes x 7 voices",   8,  7, bench);
    const double worstNs   = benchChord ("processBlock 16 notes x 16 voices", 16, 16, bench);
    const double bothNs    = benchChord ("processBlock 16 notes x 16 voices, OSC 1 + OSC 2", 16, 16, bench, true);

    const double budgetNs = (double) bs / sr * 1.0e9;          // 10.67 ms @48k/512
    const double typicalPct = typicalNs / budgetNs * 100.0;
    const double worstPct   = worstNs   / budgetNs * 100.0;
    const double bothPct    = bothNs    / budgetNs * 100.0;

    // github-action-benchmark "customSmallerIsBetter" schema:
    juce::String json;
    json << "[\n"
         << "  { \"name\": \"processBlock 8x7\",         \"unit\": \"ns/block\", \"value\": " << juce::String (typicalNs, 3)  << " },\n"
         << "  { \"name\": \"DSP load 8x7 @48k/512\",    \"unit\": \"%\",        \"value\": " << juce::String (typicalPct, 3) << " },\n"
         << "  { \"name\": \"processBlock 16x16\",       \"unit\": \"ns/block\", \"value\": " << juce::String (worstNs, 3)    << " },\n"
         << "  { \"name\": \"DSP load 16x16 @48k/512\",  \"unit\": \"%\",        \"value\": " << juce::String (worstPct, 3)   << " },\n"
         << "  { \"name\": \"processBlock 16x16 2-osc\", \"unit\": \"ns/block\", \"value\": " << juce::String (bothNs, 3)     << " },\n"
         << "  { \"name\": \"DSP load 16x16 2-osc @48k/512\", \"unit\": \"%\",  \"value\": " << juce::String (bothPct, 3)    << " }\n"
         << "]\n";
    const juce::String out = (argc > 1) ? juce::String (argv[1]) : juce::String ("bench_result.json");
    juce::File::getCurrentWorkingDirectory().getChildFile (out).replaceWithText (json);
    std::printf ("8x7:   %.0f ns (%.1f%% RT load)\n16x16: %.0f ns (%.1f%% RT load)\n16x16, both oscillators: %.0f ns (%.1f%% RT load)\n",
                 typicalNs, typicalPct, worstNs, worstPct, bothNs, bothPct);
    return 0;
}

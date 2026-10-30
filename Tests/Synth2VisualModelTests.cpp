/*
    Synth2VisualModel unit tests.

    Framework-free (no JUCE, no display): the wave shapes, the unison spread and
    the editor's numeric model are pure, so we can pin their math directly. The
    engine-side checks (that what plays matches this) live in
    Synth2EngineTests.cpp.
*/

#include "Oscillator.h"
#include "Synth2VisualModel.h"
#include "Unison.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void check (bool condition, const std::string& message)
{
    std::printf ("  [%s] %s\n", condition ? "PASS" : "FAIL", message.c_str());
    if (! condition)
        ++failures;
}

bool approx (float a, float b, float tol) { return std::fabs (a - b) <= tol; }

void testShapes()
{
    std::printf ("Wave shapes:\n");
    namespace O = Oscillator;

    check (approx (O::naive (Waveform::sine, 0.0),  0.0f, 1e-6f) && approx (O::naive (Waveform::sine, 0.25), 1.0f, 1e-6f),
           "sine starts at 0 and peaks a quarter cycle in");
    check (approx (O::naive (Waveform::triangle, 0.0),  0.0f, 1e-6f)
               && approx (O::naive (Waveform::triangle, 0.25), 1.0f, 1e-6f)
               && approx (O::naive (Waveform::triangle, 0.75), -1.0f, 1e-6f),
           "triangle starts at 0, peaks at 1/4 and bottoms at 3/4");
    check (approx (O::naive (Waveform::saw, 0.0), 0.0f, 1e-6f)
               && O::naive (Waveform::saw, 0.49) > 0.9f && O::naive (Waveform::saw, 0.51) < -0.9f,
           "saw starts at 0, rises to the top and drops half a cycle in");
    check (approx (O::naive (Waveform::square, 0.1), 1.0f, 1e-6f) && approx (O::naive (Waveform::square, 0.6), -1.0f, 1e-6f),
           "square is high for the first half cycle, low for the second");

    bool bounded = true, periodic = true;
    for (int w = 0; w < 4; ++w)
        for (int i = 0; i < 1000; ++i)
        {
            const double t = i / 1000.0;
            const float v = O::naive ((Waveform) w, t);
            bounded  = bounded && v >= -1.0f && v <= 1.0f;
            periodic = periodic && approx (v, O::naive ((Waveform) w, t + 3.0), 1e-4f);
        }
    check (bounded,  "every shape stays within -1..1");
    check (periodic, "every shape repeats each cycle");
}

void testPolyBlep()
{
    std::printf ("PolyBLEP:\n");
    const double dt = 0.01;
    check (Oscillator::polyBlep (0.5, dt) == 0.0, "no correction away from the jump");
    check (approx ((float) Oscillator::polyBlep (0.0, dt), -1.0f, 1e-9f) && approx ((float) Oscillator::polyBlep (1.0 - 1e-12, dt), 1.0f, 1e-6f),
           "the correction meets the jump's two sides (-1 just after, +1 just before)");

    // A band-limited saw stays within a hair of the naive one's range and
    // matches it away from the jump.
    BlepOscillator osc;
    osc.reset (0.0);
    osc.setIncrement (dt);
    bool close = true, bounded = true;
    for (int i = 0; i < 1000; ++i)
    {
        const double phase = osc.getPhase();
        const float v = osc.next (Waveform::saw);
        bounded = bounded && std::fabs (v) <= 1.05f;
        const double fromJump = std::fabs (Oscillator::wrap (phase + 0.5) - 0.5);
        if (fromJump < 0.5 - 2.0 * dt)
            close = close && approx (v, Oscillator::naive (Waveform::saw, phase), 1e-5f);
    }
    check (bounded, "the band-limited saw stays bounded");
    check (close,   "the band-limited saw matches the naive saw away from its jump");

    osc.setIncrement (0.9);
    check (osc.getIncrement() < 0.5, "an oscillator never steps past Nyquist");
}

void testBlockRender()
{
    std::printf ("Block render:\n");

    // The engine plays its oscillators a block at a time, several side by
    // side. That must be the very same samples, to the bit, as stepping each
    // one on its own, and stepping on its own the same as the shape's maths
    // (with the full floor() wrap the fast path leaves out).
    constexpr int numOscillators = 7;   // a group of four, a pair and one
    constexpr int numSamples     = 32;
    float levels[numSamples];
    for (int n = 0; n < numSamples; ++n)
        levels[n] = 0.25f + 0.02f * (float) n;

    bool sameAsNext = true, sameAsMaths = true;
    for (int w = 0; w < 4; ++w)
    {
        const auto wave = (Waveform) w;
        BlepOscillator together[numOscillators], alone[numOscillators];
        BlepOscillator* pointers[numOscillators];
        float rows[numOscillators][numSamples];
        float* rowPointers[numOscillators];
        for (int k = 0; k < numOscillators; ++k)
        {
            for (auto* osc : { &together[k], &alone[k] })
            {
                osc->reset (0.13 * k);
                osc->setIncrement (0.003 + 0.071 * k);
            }
            pointers[k]    = &together[k];
            rowPointers[k] = rows[k];
        }

        for (int pass = 0; pass < 20; ++pass)
        {
            const bool add = pass % 2 == 1;   // OSC 2 mixes in on top of OSC 1
            if (! add)
                for (auto& row : rows)
                    for (auto& value : row)
                        value = 0.0f;
            float expected[numOscillators][numSamples];
            for (int k = 0; k < numOscillators; ++k)
                for (int n = 0; n < numSamples; ++n)
                    expected[k][n] = rows[k][n];

            BlepOscillator::renderAll (pointers, rowPointers, numOscillators, wave, levels, numSamples, add);

            for (int k = 0; k < numOscillators; ++k)
                for (int n = 0; n < numSamples; ++n)
                {
                    const double phase = alone[k].getPhase();
                    const double dt    = alone[k].getIncrement();
                    const float  value = alone[k].next (wave);
                    if (add)
                        expected[k][n] += value * levels[n];
                    else
                        expected[k][n] = value * levels[n];
                    sameAsNext = sameAsNext && rows[k][n] == expected[k][n];

                    float maths = Oscillator::naive (wave, phase);
                    if (wave == Waveform::saw)
                    {
                        const double u = Oscillator::wrap (phase + 0.5);
                        maths = (float) (2.0 * u - 1.0 - Oscillator::polyBlep (u, dt));
                    }
                    else if (wave == Waveform::square)
                    {
                        maths = (float) ((phase < 0.5 ? 1.0 : -1.0)
                                         + Oscillator::polyBlep (phase, dt)
                                         - Oscillator::polyBlep (Oscillator::wrap (phase + 0.5), dt));
                    }
                    sameAsMaths = sameAsMaths && value == maths;
                }
        }
    }
    check (sameAsNext,  "rendering oscillators together gives each one's own samples, to the bit");
    check (sameAsMaths, "every shape plays exactly its maths, wrapped phase and all");
}

void testUnisonSpread()
{
    std::printf ("Unison spread:\n");

    check (Unison::position (0, 1) == 0.0f && Unison::detuneCents (0, 1, 50.0f) == 0.0f,
           "a single voice plays in tune");
    check (approx (Unison::detuneCents (0, 5, 20.0f), -20.0f, 1e-5f) && approx (Unison::detuneCents (4, 5, 20.0f), 20.0f, 1e-5f),
           "the outer voices sit DETUNE cents flat and sharp");
    check (approx (Unison::detuneCents (2, 5, 20.0f), 0.0f, 1e-5f), "an odd stack keeps a voice in tune in the middle");

    bool symmetric = true, evenlySpaced = true;
    for (int n = 2; n <= Unison::maxVoices; ++n)
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            sum += Unison::detuneCents (i, n, 30.0f);
            if (i > 0)
                evenlySpaced = evenlySpaced && approx (Unison::detuneCents (i, n, 30.0f) - Unison::detuneCents (i - 1, n, 30.0f),
                                                       60.0f / (float) (n - 1), 1e-4f);
        }
        symmetric = symmetric && approx (sum, 0.0f, 1e-3f);
    }
    check (symmetric,    "every stack is centred on the note (detunes sum to zero)");
    check (evenlySpaced, "voices are evenly spaced across the detune range");

    // Pans: a permutation of the same evenly spaced places, alternating sides.
    bool balanced = true, alternating = true, fills = true;
    for (int n = 2; n <= Unison::maxVoices; ++n)
    {
        float sum = 0.0f;
        bool  usedLeft = false, usedRight = false;
        for (int i = 0; i < n; ++i)
        {
            const float p = Unison::pan (i, n, 1.0f);
            sum += p;
            usedLeft  = usedLeft  || approx (p, -1.0f, 1e-5f);
            usedRight = usedRight || approx (p,  1.0f, 1e-5f);
            if (i + 1 < n && (i % 2 == 0))
                alternating = alternating && p <= 0.0f && Unison::pan (i + 1, n, 1.0f) >= 0.0f;
        }
        balanced = balanced && approx (sum, 0.0f, 1e-3f);
        fills    = fills && usedLeft && usedRight;
    }
    check (balanced,    "the stack is balanced left to right");
    check (fills,       "at full WIDTH the stack reaches hard left and hard right");
    check (alternating, "voices alternate sides, flattest on the left");
    check (Unison::pan (0, 2, 1.0f) == -1.0f && Unison::pan (1, 2, 1.0f) == 1.0f, "two voices go hard left and right");
    check (Unison::pan (0, 8, 0.0f) == 0.0f && Unison::pan (7, 8, 0.0f) == 0.0f, "WIDTH 0 keeps every voice in the centre");

    const auto centre = Unison::panGains (0.0f);
    const auto left   = Unison::panGains (-1.0f);
    check (approx (centre.left, 1.0f, 1e-5f) && approx (centre.right, 1.0f, 1e-5f), "the centre is 0 dB on both sides");
    check (approx (left.right, 0.0f, 1e-5f) && approx (left.left * left.left + left.right * left.right, 2.0f, 1e-4f),
           "panning keeps equal power");

    check (approx (Unison::stackGain (4.0f), 0.5f, 1e-6f) && approx (Unison::stackGain (0.0f), 1.0f, 1e-6f),
           "the stack scales by 1 / sqrt (voices)");
    check (std::abs (Unison::ratio (1200.0f) - 2.0) < 1e-12 && std::abs (Unison::ratio (0.0f) - 1.0) < 1e-12,
           "1200 cents is an octave");
    check (Unison::clampVoices (0) == 1 && Unison::clampVoices (99) == Unison::maxVoices, "VOICES is kept to 1..16");
}

void testVisualModel()
{
    std::printf ("Visual model:\n");
    using M = Synth2VisualModel;

    check (approx (M::waveAt (Waveform::saw, 0.25f), Oscillator::naive (Waveform::saw, 0.5), 1e-6f),
           "the WAVE well shows two cycles of the engine's own shape");
    check (M::voicePhaseOffset (0, 1, 50.0f) == 0.0f, "a single voice's copy isn't shifted");
    check (approx (M::voicePhaseOffset (0, 3, 50.0f), -0.125f, 1e-6f) && approx (M::voicePhaseOffset (2, 3, 50.0f), 0.125f, 1e-6f),
           "the outer copies shift by up to an eighth of a cycle at full DETUNE");

    const auto single = M::marker (0, 1, 30.0f, 1.0f);
    check (approx (single.x, 0.5f, 1e-6f) && approx (single.pan, 0.0f, 1e-6f), "one voice is a dot in the centre");

    const auto flat  = M::marker (0, 4, 50.0f, 1.0f);
    const auto sharp = M::marker (3, 4, 50.0f, 1.0f);
    check (approx (flat.x, 0.0f, 1e-6f) && approx (sharp.x, 1.0f, 1e-6f), "full DETUNE reaches the well's edges");
    check (approx (flat.pan, Unison::pan (0, 4, 1.0f), 1e-6f) && approx (sharp.pan, Unison::pan (3, 4, 1.0f), 1e-6f),
           "the dots sit where the engine pans the voices");
    check (flat.tint < sharp.tint, "flat voices are tinted cool, sharp ones warm");

    const auto halfway = M::marker (3, 4, 25.0f, 1.0f);
    check (approx (halfway.x, 0.75f, 1e-6f), "the detune scale is fixed, so half the DETUNE reaches halfway out");
}
void testEnvelope()
{
    std::printf ("AMP envelope:\n");
    using M = Synth2VisualModel;

    M::EnvelopeDials none, all;
    none.sustain = 0.5f;
    all          = { 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f };
    const auto shortest = M::envelope (none, 0.0f);
    const auto longest  = M::envelope (all, 0.0f);
    check (approx (shortest.delayEnd, 0.0f, 1e-6f) && shortest.attackEnd > 0.0f && approx (shortest.holdEnd, shortest.attackEnd, 1e-6f)
               && shortest.holdEnd < shortest.decayEnd && shortest.decayEnd < shortest.sustainEnd && shortest.sustainEnd < shortest.releaseEnd,
           "the stages come in order, with no delay or hold at their shortest");
    check (longest.releaseEnd <= 1.0f + 1e-6f, "at their longest the stages still fit the well");
    check (longest.delayEnd > 0.0f && longest.holdEnd > longest.attackEnd, "turning DELAY and HOLD up opens a wait and a stretch at the top");
    check (longest.attackEnd - longest.delayEnd > shortest.attackEnd - shortest.delayEnd
               && longest.decayEnd - longest.holdEnd > shortest.decayEnd - shortest.holdEnd
               && longest.releaseEnd - longest.sustainEnd > shortest.releaseEnd - shortest.sustainEnd,
           "turning ATTACK, DECAY and RELEASE up stretches their ramps");
    check (approx (longest.sustainEnd - longest.decayEnd, shortest.sustainEnd - shortest.decayEnd, 1e-6f),
           "the sustain stretch keeps its length");
    check (approx (shortest.sustain, 0.5f * shortest.peak, 1e-6f), "SUSTAIN sets its stretch's height, as a share of the top");

    check (approx (M::heightForDb (-24.0f), 0.0f, 1e-6f) && approx (M::heightForDb (6.0f), 1.0f, 1e-6f)
               && approx (M::heightForDb (0.0f), 0.8f, 1e-6f),
           "OUTPUT sets the top's height on a -24..+6 dB scale");
    check (M::envelope (none, -12.0f).peak < M::envelope (none, 0.0f).peak, "turning OUTPUT down lowers the top");

    using Stage = Envelope::Stage;
    const auto e = M::envelope (all, 0.0f);
    const auto waiting = M::noteDot (e, Stage::delay, 0.0f, 0.5f);
    const auto rising  = M::noteDot (e, Stage::attack, 0.5f, 0.5f);
    const auto holding = M::noteDot (e, Stage::hold, 1.0f, 0.5f);
    const auto falling = M::noteDot (e, Stage::decay, 0.75f, 0.5f);
    const auto resting = M::noteDot (e, Stage::sustain, 0.5f, 0.5f);
    const auto leaving = M::noteDot (e, Stage::release, 0.25f, 0.5f);
    check (approx (waiting.x, 0.5f * e.delayEnd, 1e-6f) && approx (waiting.y, 0.0f, 1e-6f), "halfway through the delay, a note's dot waits halfway along it");
    check (approx (rising.x, 0.5f * (e.delayEnd + e.attackEnd), 1e-6f) && approx (rising.y, 0.5f * e.peak, 1e-6f),
           "halfway up the attack, it is halfway up the rise");
    check (approx (holding.x, 0.5f * (e.attackEnd + e.holdEnd), 1e-6f) && approx (holding.y, e.peak, 1e-6f), "holding, it walks the top");
    check (falling.x > e.holdEnd && falling.x < e.decayEnd && approx (falling.y, 0.75f * e.peak, 1e-6f), "decaying, it walks down to the sustain level");
    check (approx (resting.x, 0.5f * (e.decayEnd + e.sustainEnd), 1e-6f) && approx (resting.y, e.sustain, 1e-6f),
           "sustaining, it rests on the sustain stretch");
    check (leaving.x > e.sustainEnd && leaving.x < e.releaseEnd && approx (leaving.y, 0.25f * e.peak, 1e-6f), "releasing, it walks down the fall");

    // The corners that can be dragged.
    using Handle = M::Handle;
    const auto at = [] (const M::Envelope& env, Handle h) { return M::handlePosition (env, h); };
    check (approx (at (e, Handle::attack).x, e.attackEnd, 1e-6f) && approx (at (e, Handle::attack).y, e.peak, 1e-6f)
               && approx (at (e, Handle::decay).y, e.sustain, 1e-6f) && approx (at (e, Handle::release).x, e.releaseEnd, 1e-6f),
           "each handle sits on its stage's corner");

    constexpr float width = 200.0f, height = 80.0f, radius = 9.0f;
    const auto near = [&] (const M::Envelope& env, Handle h, float dxPixels)
    {
        const auto p = at (env, h);
        return M::handleAt (env, { p.x + dxPixels / width, p.y }, width, height, radius);
    };
    check (near (e, Handle::decay, 2.0f) == (int) Handle::decay && near (e, Handle::release, -3.0f) == (int) Handle::release,
           "a press near a corner takes its handle");
    check (M::handleAt (e, { 0.5f, 0.95f }, width, height, radius) == -1, "a press away from them takes none");
    check (near (shortest, Handle::attack, 0.0f) == (int) Handle::attack && near (shortest, Handle::attack, -2.0f) == (int) Handle::attack,
           "with no HOLD, a press on the shared corner takes ATTACK");
    check (near (shortest, Handle::attack, 4.0f) == (int) Handle::hold, "and a press just right of it takes HOLD, to pull it out");

    check (approx (M::dragDial (0.2f, 0.5f * M::handleSpans[1], Handle::attack), 0.7f, 1e-6f)
               && approx (M::dragDial (0.9f, 0.5f, Handle::release), 1.0f, 1e-6f) && approx (M::dragDial (0.1f, -0.5f, Handle::hold), 0.0f, 1e-6f),
           "a drag moves a time dial by how much of its stage's width it covers, kept within 0..1");
    M::EnvelopeDials moved = none;
    moved.attack = M::dragDial (none.attack, 0.05f, Handle::attack);
    check (approx (M::envelope (moved, 0.0f).attackEnd - shortest.attackEnd, 0.05f, 1e-6f), "so the corner follows the pointer");
    check (approx (M::sustainAt (e, 0.5f * e.peak), 0.5f, 1e-6f) && approx (M::sustainAt (e, 2.0f), 1.0f, 1e-6f)
               && approx (M::sustainAt (e, -1.0f), 0.0f, 1e-6f),
           "DECAY's corner dragged up and down sets SUSTAIN as a share of the top");
}
} // namespace

void testWavePicking()
{
    std::printf ("Wave picking:\n");
    using M = Synth2VisualModel;

    check (M::stepWave (2, 1) == 3 && M::stepWave (2, -1) == 1, "the arrows step to the next and previous shape");
    check (M::stepWave (3, 1) == 0 && M::stepWave (0, -1) == 3, "and wrap round at either end");
    check (M::stepWave (1, 9) == 2 && M::stepWave (1, -9) == 0, "any number of steps lands on a shape");

    check (approx (M::ease (0.0f), 0.0f, 1e-4f) && approx (M::ease (1.0f), 1.0f, 1e-4f), "the ease starts at 0 and ends at 1");
    bool rising = true;
    for (int i = 1; i <= 100; ++i)
        rising = rising && M::ease ((float) i / 100.0f) >= M::ease ((float) (i - 1) / 100.0f);
    check (rising, "and only ever moves forwards");
    check (M::ease (0.5f) > 0.7f && M::ease (0.5f) < 0.85f, "half the time in, it is most of the way there (it settles, no bounce)");
    check (approx (M::ease (-1.0f), 0.0f, 1e-4f) && approx (M::ease (2.0f), 1.0f, 1e-4f), "and holds still outside 0..1");

    bool ends = true;
    bool between = true;
    for (int p = 0; p <= 64; ++p)
    {
        const float x = (float) p / 64.0f;
        const float a = M::waveAt (Waveform::saw, x);
        const float b = M::waveAt (Waveform::square, x);
        ends = ends && approx (M::morphAt (Waveform::saw, Waveform::square, 0.0f, x), a, 1e-6f)
                    && approx (M::morphAt (Waveform::saw, Waveform::square, 1.0f, x), b, 1e-6f);
        const float mid = M::morphAt (Waveform::saw, Waveform::square, 0.5f, x);
        between = between && mid >= std::min (a, b) - 1e-6f && mid <= std::max (a, b) + 1e-6f;
    }
    check (ends, "a morph starts on the old shape and ends on the new");

    check (approx (M::cyclesFor (0, 0.0f), 2.0f, 1e-5f), "at PITCH 0 an oscillator's well shows two cycles");
    check (approx (M::cyclesFor (12, 0.0f), 4.0f, 1e-4f) && approx (M::cyclesFor (-12, 0.0f), 1.0f, 1e-4f),
           "an octave up twice as many, an octave down half as many");
    check (M::cyclesFor (0, 50.0f) > 2.0f && M::cyclesFor (0, -50.0f) < 2.0f, "FINE squeezes or stretches it a little");
    check (approx (M::cyclesFor (24, 50.0f), 8.0f, 1e-5f) && approx (M::cyclesFor (-24, -50.0f), 0.5f, 1e-5f),
           "and it stays between half a cycle and eight");
    check (approx (M::waveAt (Waveform::saw, 0.25f, 0.0f, 4.0f), M::waveAt (Waveform::saw, 0.5f), 1e-5f),
           "a well showing four cycles is the two-cycle shape squeezed");
    check (between, "and passes between them on the way");
}


void testEffects()
{
    std::printf ("Effects:\n");

    using VM = Synth2VisualModel;
    check (approx (Effects::dryGain (0.25f), 1.0f, 1e-6f) && approx (Effects::wetGain (0.25f), 0.5f, 1e-6f),
           "MIX below the middle keeps the dry sound whole and adds the effect");
    check (approx (Effects::dryGain (0.75f), 0.5f, 1e-6f) && approx (Effects::wetGain (0.75f), 1.0f, 1e-6f),
           "and above it fades the dry sound out under the whole effect");

    const auto e = VM::echoes (0.5, 0.5f, 1.0f, 0.5f, false);
    check (e.count == 5 && approx (e.taps[0].x, 0.0f, 1e-6f) && approx (e.taps[1].x, 0.25f, 1e-6f) && approx (e.taps[4].x, 1.0f, 1e-6f),
           "the dry sound, then a repeat every TIME across the two-second well");
    check (approx (e.taps[1].left, 1.0f, 1e-6f) && approx (e.taps[2].left, 0.5f, 1e-6f) && approx (e.taps[3].right, 0.25f, 1e-6f),
           "each FEEDBACK as loud as the last");
    const auto dark = VM::echoes (0.5, 0.5f, 0.0f, 0.5f, false);
    check (dark.taps[3].brightness < dark.taps[1].brightness && dark.taps[1].brightness < e.taps[1].brightness,
           "a low TONE dims the repeats, each more than the last");
    const auto bounce = VM::echoes (0.5, 0.5f, 1.0f, 0.5f, true);
    check (bounce.taps[1].right <= 0.0f && bounce.taps[1].left > 0.0f && bounce.taps[2].left <= 0.0f && bounce.taps[2].right > 0.0f,
           "PING-PONG: left, then right");
    check (VM::echoes (0.01, 0.95f, 1.0f, 1.0f, false).count == VM::maxEchoes, "a short TIME fills the well, but no further");
    check (VM::echoes (0.375, 0.0f, 1.0f, 0.5f, false).count == 2, "with no FEEDBACK, one repeat");

    check (VM::roomHeight (0.9f, 0.5f, 0.3f) > VM::roomHeight (0.3f, 0.5f, 0.3f), "a bigger room stands taller for longer");
    check (approx (VM::roomHeight (0.5f, 0.0f, 0.1f), 0.0f, 1e-6f), "at no MIX there is no room");
    check (approx (VM::roomHeight (0.5f, 1.0f, 0.0f), 0.0f, 1e-6f) && VM::roomHeight (0.5f, 1.0f, 0.01f) > 0.5f, "it swells in quickly");
    check (VM::roomHeight (0.0f, 1.0f, 0.5f) <= 0.0f, "and the smallest room is gone within three seconds");
    check (VM::roomBrightness (0.5f, 1.0f, 0.1f) < VM::roomBrightness (0.5f, 0.0f, 0.1f), "DAMPING dulls it sooner");
    check (approx (VM::roomHeight (0.5f, 1.0f, 0.01f, 100.0f), 0.0f, 1e-6f) && VM::roomHeight (0.5f, 1.0f, 0.03f, 100.0f) > 0.5f,
           "PRE-DELAY holds the room back, then it swells in");
    check (std::abs (Effects::reverbSeconds (0.5f) - 1.897) < 0.01 && std::abs (Effects::reverbSeconds (1.0f) - 12.0) < 1e-6,
           "SIZE rings about 2 s in the middle, 12 s at the top");

    Effects::DelaySettings synced;
    synced.sync     = true;
    synced.division = 8;
    check (std::abs (Effects::delaySeconds (synced, 120.0) - 0.5) < 1e-9, "a synced quarter note at 120 BPM is 500 ms");
    check (std::abs (Effects::delaySeconds (Effects::DelaySettings {}, 77.0) - 0.375) < 1e-9, "unsynced, TIME ignores the tempo");
}

int main()
{
    std::printf ("Synth2VisualModel tests\n=======================\n");

    testShapes();
    testPolyBlep();
    testBlockRender();
    testUnisonSpread();
    testVisualModel();
    testEnvelope();
    testWavePicking();
    testEffects();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

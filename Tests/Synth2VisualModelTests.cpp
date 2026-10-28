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

    const auto shortest = M::envelope (0.0f, 0.0f, 0.0f);
    const auto longest  = M::envelope (1.0f, 1.0f, 0.0f);
    check (shortest.attackEnd > 0.0f && shortest.attackEnd < shortest.holdEnd && shortest.holdEnd < shortest.releaseEnd,
           "the rise, the top and the fall come in order");
    check (longest.releaseEnd <= 1.0f + 1e-6f, "at their longest the ramps still fit the well");
    check (longest.attackEnd > shortest.attackEnd && approx (longest.holdEnd - longest.attackEnd, shortest.holdEnd - shortest.attackEnd, 1e-6f),
           "turning ATTACK up stretches the rise; the top keeps its length");
    check (longest.releaseEnd - longest.holdEnd > shortest.releaseEnd - shortest.holdEnd, "turning RELEASE up stretches the fall");

    check (approx (M::heightForDb (-24.0f), 0.0f, 1e-6f) && approx (M::heightForDb (6.0f), 1.0f, 1e-6f)
               && approx (M::heightForDb (0.0f), 0.8f, 1e-6f),
           "OUTPUT sets the top's height on a -24..+6 dB scale");
    check (M::envelope (0.5f, 0.5f, -12.0f).peak < M::envelope (0.5f, 0.5f, 0.0f).peak, "turning OUTPUT down lowers the top");

    const auto e = M::envelope (0.5f, 0.5f, 0.0f);
    const auto rising  = M::noteDot (e, 1, 0.5f, 1.0f);
    const auto holding = M::noteDot (e, 2, 1.0f, 1.0f);
    const auto falling = M::noteDot (e, 3, 0.25f, 1.0f);
    check (approx (rising.x, 0.5f * e.attackEnd, 1e-6f) && approx (rising.y, 0.5f * e.peak, 1e-6f),
           "halfway up the attack, a note's dot is halfway up the rise");
    check (holding.x > e.attackEnd && holding.x < e.holdEnd && approx (holding.y, e.peak, 1e-6f),
           "a held note rests on the top");
    check (falling.x > e.holdEnd && falling.x < e.releaseEnd && approx (falling.y, 0.25f * e.peak, 1e-6f),
           "a releasing note walks down the fall");
    const auto letGoEarly = M::noteDot (e, 3, 0.4f, 0.4f);
    check (approx (letGoEarly.x, e.holdEnd, 1e-6f), "a note let go mid-attack starts its fall at the release's top");
}
} // namespace

int main()
{
    std::printf ("Synth2VisualModel tests\n=======================\n");

    testShapes();
    testPolyBlep();
    testUnisonSpread();
    testVisualModel();
    testEnvelope();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "EffectSettings.h"
#include "Envelope.h"
#include "Oscillator.h"
#include "Unison.h"

/**
    Synth2VisualModel

    Pure, framework-free numbers behind the editor's displays: the
    oscillators' WAVE wells (the shape at its pitch and level, and a faint copy
    per unison voice, each placed and tinted by its detune and pan) and the
    envelopes' AMP wells (the envelope's shape, filling the well, and a dot per
    sounding note riding it), and the effects' wells (the delay's repeats and
    the reverb's ring).

    It draws on the very same Oscillator::naive and Unison maths the engine
    plays, so what the displays show never drifts from what you hear. The
    voices' offsets in the WAVE well are only a picture of the detune: the
    real voices start at random points in their cycles and drift apart at the
    detune's beat rate.

    Kept header-only and dependency-free so it can be unit-tested on any
    platform without JUCE or a display.
*/
struct Synth2VisualModel
{
    static float clamp01 (float x) noexcept { return std::min (1.0f, std::max (0.0f, x)); }

    /** An oscillator well's shape at x (0..1 across the well). At PITCH 0 it
        shows two cycles. */
    static constexpr float cyclesShown = 2.0f;

    static float waveAt (Waveform wave, float x, float phaseOffset = 0.0f, float cycles = cyclesShown) noexcept
    {
        return Oscillator::naive (wave, (double) (x * cycles + phaseOffset));
    }

    /** How many cycles an oscillator's well shows: two at PITCH 0, twice as
        many an octave up, half as many an octave down (so its PITCH and FINE
        show as a squeeze or a stretch), kept between half a cycle and eight. */
    static float cyclesFor (int semitones, float fineCents) noexcept
    {
        const float cycles = cyclesShown * std::exp2 (((float) semitones + fineCents / 100.0f) / 12.0f);
        return std::min (8.0f, std::max (0.5f, cycles));
    }

    /** The wave `delta` steps on from `index` (0..3), wrapping round: one
        past SQUARE is SINE again. */
    static int stepWave (int index, int delta) noexcept
    {
        constexpr int numWaves = 4;
        return ((index + delta) % numWaves + numWaves) % numWaves;
    }

    /** How long the WAVE well takes to morph from one shape to the next (the
        design system's --dur-base). */
    static constexpr float morphSeconds = 0.15f;

    /** The design system's ease, cubic-bezier(0.4, 0, 0.2, 1): quick out of
        the start, settling into the end. t and the result run 0..1. */
    static float ease (float t) noexcept
    {
        t = clamp01 (t);
        // The curve's x(u) and y(u) for its control points; find the u whose
        // x is t (x rises monotonically, so bisection is enough), then take y.
        const auto bezier = [] (float u, float p1, float p2)
        {
            const float v = 1.0f - u;
            return 3.0f * v * v * u * p1 + 3.0f * v * u * u * p2 + u * u * u;
        };
        float lo = 0.0f;
        float hi = 1.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (bezier (mid, 0.4f, 0.2f) < t ? lo : hi) = mid;
        }
        return bezier (0.5f * (lo + hi), 0.0f, 1.0f);
    }

    /** The WAVE well's shape `amount` (0..1) of the way from `from` to `to`. */
    static float morphAt (Waveform from, Waveform to, float amount, float x, float phaseOffset = 0.0f,
                          float cycles = cyclesShown) noexcept
    {
        const float a = waveAt (from, x, phaseOffset, cycles);
        const float b = waveAt (to, x, phaseOffset, cycles);
        return a + (b - a) * clamp01 (amount);
    }

    /** How far (in cycles) a unison voice's copy is drawn shifted: its detune
        as a share of the largest detune, up to an eighth of a cycle each way. */
    static float voicePhaseOffset (int index, int numVoices, float detuneCents) noexcept
    {
        return Unison::detuneCents (index, numVoices, detuneCents) / Unison::maxDetuneCents * 0.125f;
    }

    /** Where one unison voice sits: its detune, its pan and its tint. */
    struct Marker
    {
        float x     = 0.5f;   // 0..1: its detune on a fixed -50..+50 cent scale
        float pan   = 0.0f;   // -1..1: where it sits left to right
        float tint  = 0.5f;   // 0..1: where it sits on the spectrum (flat -> sharp)
    };

    static Marker marker (int index, int numVoices, float detuneCents, float width) noexcept
    {
        const float cents = Unison::detuneCents (index, numVoices, detuneCents);
        Marker m;
        m.x    = clamp01 (0.5f + 0.5f * cents / Unison::maxDetuneCents);
        m.pan  = Unison::pan (index, numVoices, width);
        m.tint = clamp01 (0.5f + 0.5f * Unison::position (index, numVoices));
        return m;
    }

    // ---- AMP ---------------------------------------------------------------

    /** The AMP well's envelope, as fractions of the well (x 0..1 left to right,
        level 0..1 bottom to top): a wait at silence over DELAY, a rise over
        ATTACK, a stretch at the top over HOLD, a fall over DECAY to the
        SUSTAIN level, a stretch there while the key is down, and a fall to
        silence over RELEASE. Each stage's length follows its dial's position
        (the dial's own, skewed, 0..1), so a turn of the dial visibly
        stretches it; the times themselves are in the readout.

        The envelope fills the well, whatever its size: the stages are
        scaled to span its whole width (see naturalWidth), and the top is
        the top of the well (OUTPUT's level is in the readout), so the shape
        looks the same in a small well as in a tall one.

        The attack, decay and release ramps bend as their curves have them
        (see ::Envelope::travel): rampLevel gives their heights. */
    struct Envelope
    {
        float delayEnd   = 0.0f;   // x where the rise starts
        float attackEnd  = 0.0f;   // x where the rise reaches the top
        float holdEnd    = 0.0f;   // x where the decay starts
        float decayEnd   = 0.0f;   // x where it reaches the sustain level
        float sustainEnd = 0.0f;   // x where the release starts
        float releaseEnd = 0.0f;   // x where it reaches silence
        float peak       = 1.0f;   // the top's height: the top of the well
        float sustain    = 0.0f;   // the sustain level's height
        double attackK   = 0.0;    // each ramp's steepness (see ::Envelope::travel)
        double decayK    = 0.0;
        double releaseK  = 0.0;
    };

    /** The envelope's dials: each time dial's position (0..1), SUSTAIN's
        level, and each ramp's bend (-1..1, 0 straight). */
    struct EnvelopeDials
    {
        float delay        = 0.0f;
        float attack       = 0.0f;
        float hold         = 0.0f;
        float decay        = 0.0f;
        float sustain      = 1.0f;
        float release      = 0.0f;
        float attackCurve  = 0.0f;
        float decayCurve   = 0.0f;
        float releaseCurve = 0.0f;
    };

    /** What can be dragged on the AMP well's envelope: its corners, each
        stretching its stage sideways (DECAY's corner also sets SUSTAIN up
        and down), then a dot halfway along each ramp, dragged up and down
        to bend it. */
    enum class Handle { delay, attack, hold, decay, release, attackCurve, decayCurve, releaseCurve };
    static constexpr int numCorners = 5;
    static constexpr int numHandles = 8;

    static constexpr bool isCurve (Handle h) noexcept { return (int) h >= numCorners; }

    /** How long each corner's stage grows, from its dial at 0 to its dial
        at 1, in the units the stages are laid out in before they are scaled
        to fit the well (see naturalWidth). */
    static constexpr std::array<float, numCorners> handleSpans { 0.10f, 0.17f, 0.08f, 0.15f, 0.20f };

    // The least a ramp takes (so a time of 0 still slopes), and the sustain
    // stretch, in the same units.
    static constexpr float minRamp      = 0.03f;
    static constexpr float sustainWidth = 0.14f;

    /** How long the whole envelope is laid out, before it is scaled to fit
        the well: the stretch `envelope` fits to the well's width. */
    static float naturalWidth (const EnvelopeDials& dials) noexcept
    {
        float width = 3.0f * minRamp + sustainWidth;
        const std::array<float, numCorners> dial { dials.delay, dials.attack, dials.hold, dials.decay, dials.release };
        for (size_t h = 0; h < handleSpans.size(); ++h)
            width += handleSpans[h] * clamp01 (dial[h]);
        return width;
    }

    /** The envelope for `dials`, `fullWidth` of its natural length spanning
        the well (by default its own, so it fills the well exactly). */
    static Envelope envelope (const EnvelopeDials& dials, float fullWidth) noexcept
    {
        const auto span  = [] (Handle h) { return handleSpans[(size_t) h]; };
        const float to01 = 1.0f / std::max (fullWidth, 1.0e-6f);
        Envelope e;
        e.delayEnd   = to01 * span (Handle::delay) * clamp01 (dials.delay);
        e.attackEnd  = e.delayEnd + to01 * (minRamp + span (Handle::attack) * clamp01 (dials.attack));
        e.holdEnd    = e.attackEnd + to01 * span (Handle::hold) * clamp01 (dials.hold);
        e.decayEnd   = e.holdEnd + to01 * (minRamp + span (Handle::decay) * clamp01 (dials.decay));
        e.sustainEnd = e.decayEnd + to01 * sustainWidth;
        e.releaseEnd = e.sustainEnd + to01 * (minRamp + span (Handle::release) * clamp01 (dials.release));
        e.sustain    = e.peak * clamp01 (dials.sustain);
        e.attackK    = ::Envelope::risingSteepness (dials.attackCurve);
        e.decayK     = ::Envelope::fallingSteepness (dials.decayCurve);
        e.releaseK   = ::Envelope::fallingSteepness (dials.releaseCurve);
        return e;
    }

    /** A ramp's (the attack's, decay's or release's) height once `time`
        (0..1) of it has passed, the release falling from the sustain level. */
    static float rampLevel (const Envelope& e, ::Envelope::Stage ramp, float time) noexcept
    {
        using Stage = ::Envelope::Stage;
        const auto along = [t = (double) clamp01 (time)] (double k) { return (float) ::Envelope::travel (t, k); };
        switch (ramp)
        {
            case Stage::attack:  return e.peak * along (e.attackK);
            case Stage::decay:   return e.peak - (e.peak - e.sustain) * along (e.decayK);
            case Stage::release: return e.sustain * (1.0f - along (e.releaseK));
            case Stage::idle:
            case Stage::delay:
            case Stage::hold:
            case Stage::sustain: break;
        }
        return 0.0f;
    }

    static Envelope envelope (const EnvelopeDials& dials) noexcept { return envelope (dials, naturalWidth (dials)); }

    /** Where a sounding note's dot rides the envelope: x across the well,
        y its height (both 0..1). It walks each stage's stretch as far as the
        note is through that stage, at the envelope's level, and while the
        note sustains it rests in the middle of the sustain stretch. */
    struct Dot
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    static Dot noteDot (const Envelope& e, ::Envelope::Stage stage, float level, float progress) noexcept
    {
        using Stage = ::Envelope::Stage;
        const auto along = [p = clamp01 (progress)] (float from, float to) { return from + p * (to - from); };
        Dot d;
        d.y = clamp01 (level) * e.peak;
        switch (stage)
        {
            case Stage::delay:   d.x = along (0.0f, e.delayEnd); break;
            case Stage::attack:  d.x = along (e.delayEnd, e.attackEnd); break;
            case Stage::hold:    d.x = along (e.attackEnd, e.holdEnd); break;
            case Stage::decay:   d.x = along (e.holdEnd, e.decayEnd); break;
            case Stage::sustain: d.x = 0.5f * (e.decayEnd + e.sustainEnd); break;
            case Stage::release: d.x = along (e.sustainEnd, e.releaseEnd); break;
            case Stage::idle:    break;
        }
        d.x = clamp01 (d.x);
        return d;
    }

    /** Where handle `h` sits, 0..1 across and up the well. */
    static Dot handlePosition (const Envelope& e, Handle h) noexcept
    {
        switch (h)
        {
            case Handle::delay:   return { e.delayEnd, 0.0f };
            case Handle::attack:  return { e.attackEnd, e.peak };
            case Handle::hold:    return { e.holdEnd, e.peak };
            case Handle::decay:   return { e.decayEnd, e.sustain };
            case Handle::release: return { e.releaseEnd, 0.0f };
            case Handle::attackCurve:  return { 0.5f * (e.delayEnd + e.attackEnd), rampLevel (e, ::Envelope::Stage::attack, 0.5f) };
            case Handle::decayCurve:   return { 0.5f * (e.holdEnd + e.decayEnd), rampLevel (e, ::Envelope::Stage::decay, 0.5f) };
            case Handle::releaseCurve: return { 0.5f * (e.sustainEnd + e.releaseEnd), rampLevel (e, ::Envelope::Stage::release, 0.5f) };
        }
        return {};
    }

    /** Whether handle `h` is there to drag in a well `width` by `height`
        pixels, its dots `radius` across: every corner is, and a ramp's dot
        while its ramp is long enough to bend (a decay to a SUSTAIN at the
        top, or a release from silence, has no fall to bend) and the dot,
        wherever its bend has taken it, stays clear enough of every corner
        to be pressed (see handleAt). */
    static bool isShown (const Envelope& e, Handle h, float width, float height, float radius) noexcept
    {
        const auto longEnough = [&] (float x0, float y0, float x1, float y1)
        {
            return std::hypot ((x1 - x0) * width, (y1 - y0) * height) >= 3.0f * radius && std::abs (y1 - y0) * height >= radius;
        };
        bool canBend = true;
        switch (h)
        {
            case Handle::attackCurve:  canBend = longEnough (e.delayEnd, 0.0f, e.attackEnd, e.peak); break;
            case Handle::decayCurve:   canBend = longEnough (e.holdEnd, e.peak, e.decayEnd, e.sustain); break;
            case Handle::releaseCurve: canBend = longEnough (e.sustainEnd, e.sustain, e.releaseEnd, 0.0f); break;
            case Handle::delay:
            case Handle::attack:
            case Handle::hold:
            case Handle::decay:
            case Handle::release:      return true;
        }
        const auto dot = handlePosition (e, h);
        for (int corner = 0; corner < numCorners && canBend; ++corner)
        {
            const auto p = handlePosition (e, (Handle) corner);
            canBend = std::hypot ((dot.x - p.x) * width, (dot.y - p.y) * height) >= minDotGap;
        }
        return canBend;
    }

    /** How near (in pixels) a ramp's dot may come to a corner and still be
        pressed: a press takes the dot when it is nearer to it than to the
        corner by a pixel. */
    static constexpr float minDotGap = 2.0f;

    /** The handle under a point (0..1 across and up a well `width` by
        `height` pixels), within `radius` pixels, or -1 for none. Where two
        handles sit on each other (a HOLD of 0 puts its corner on ATTACK's), a
        press on them takes the earlier one, and a press a few pixels to
        their right the later one, so it can be pulled out. A ramp's dot,
        however far its bend has carried it towards a corner, takes a press
        nearer to it than to the corner (by a pixel, so a tie goes to the
        corner). */
    static int handleAt (const Envelope& e, Dot at, float width, float height, float radius) noexcept
    {
        int   best     = -1;
        float bestDist = radius;
        for (int h = 0; h < numHandles; ++h)
        {
            if (isCurve ((Handle) h) && ! isShown (e, (Handle) h, width, height, radius))
                continue;
            const auto  p    = handlePosition (e, (Handle) h);
            const float dist = std::hypot ((at.x - p.x) * width, (at.y - p.y) * height);
            const bool  tied = best >= 0 && std::abs (dist - bestDist) < 1.0f && (at.x - p.x) * width >= 3.0f;
            if (dist <= radius && (dist < bestDist - 1.0f || tied))
            {
                best     = h;
                bestDist = std::min (dist, bestDist);
            }
        }
        return best;
    }

    /** The time dial handle `h` moves. */
    static float& dialOf (EnvelopeDials& dials, Handle h) noexcept
    {
        switch (h)
        {
            case Handle::delay:   return dials.delay;
            case Handle::attack:  return dials.attack;
            case Handle::hold:    return dials.hold;
            case Handle::decay:   return dials.decay;
            case Handle::release:
            case Handle::attackCurve:
            case Handle::decayCurve:
            case Handle::releaseCurve: break;
        }
        return dials.release;
    }

    /** The bend a ramp's dot (handle `h`) sets. */
    static float& curveOf (EnvelopeDials& dials, Handle h) noexcept
    {
        switch (h)
        {
            case Handle::attackCurve: return dials.attackCurve;
            case Handle::decayCurve:  return dials.decayCurve;
            case Handle::delay:
            case Handle::attack:
            case Handle::hold:
            case Handle::decay:
            case Handle::release:
            case Handle::releaseCurve: break;
        }
        return dials.releaseCurve;
    }

    /** The bend that puts ramp dot `h` at height `y` (0..1 up the well), so
        it follows the pointer up and down: halfway through its time the
        ramp is `share` of its way along, and a share that far bent comes
        straight from travel's form, 1 / (1 + e^(k/2)). */
    static float curveAt (const Envelope& e, Handle h, float y) noexcept
    {
        float share = 0.5f;
        if (h == Handle::attackCurve && e.peak > 0.0f)
            share = y / e.peak;
        else if (h == Handle::decayCurve && e.peak > e.sustain)
            share = (e.peak - y) / (e.peak - e.sustain);
        else if (h == Handle::releaseCurve && e.sustain > 0.0f)
            share = 1.0f - y / e.sustain;
        const double s = std::clamp ((double) share, 1.0e-3, 1.0 - 1.0e-3);
        const double k = 2.0 * std::log ((1.0 - s) / s);
        const double bend = h == Handle::attackCurve ? -k / ::Envelope::maxSteepness : k / ::Envelope::maxSteepness;
        return (float) std::clamp (bend, -1.0, 1.0);
    }

    /** The dial that puts handle `h`'s corner at `x` (0..1 across the well)
        in the envelope fitted to the well, the other dials as in `dials`:
        so a dragged corner follows the pointer, the stages after it making
        room. Not for RELEASE, whose corner is always the right edge (see
        dragDial). */
    static float dialAt (const EnvelopeDials& dials, Handle h, float x) noexcept
    {
        auto at0 = dials;
        const float current = std::exchange (dialOf (at0, h), 0.0f);
        const float before  = handlePosition (envelope (at0, 1.0f), h).x;   // up to the corner, with the dial at 0
        const float after   = naturalWidth (at0) - before;                  // from the corner on
        if (after <= 0.0f)
            return current;
        const float to      = std::clamp (x, 0.0f, 0.999f);
        return clamp01 ((to * after / (1.0f - to) - before) / handleSpans[(size_t) h]);
    }

    /** A time dial's position after its handle moved `dx` of the well's
        width, with `fullWidth` of the natural length spanning the well (see
        envelope) when the drag began. RELEASE's corner stays on the right
        edge, so this is how it is dragged: the stages before it make room. */
    static float dragDial (float startDial, float dx, Handle h, float fullWidth) noexcept
    {
        return clamp01 (startDial + dx * fullWidth / handleSpans[(size_t) h]);
    }

    /** The SUSTAIN level for DECAY's handle dragged to height `y` (0..1 up
        the well). */
    static float sustainAt (const Envelope& e, float y) noexcept { return clamp01 (y / e.peak); }

    // ---- EFFECTS -----------------------------------------------------------

    /** One spike in the DELAY well: where it falls (x 0..1 across the well),
        how loud it is on each side, and how bright it still is (TONE darkens
        each repeat a little more). */
    struct Echo
    {
        float x          = 0.0f;
        float left       = 0.0f;
        float right      = 0.0f;
        float brightness = 1.0f;
    };

    static constexpr float echoSpanSeconds = 2.0f;   // the DELAY well's width, the longest TIME
    static constexpr int   maxEchoes       = 64;

    struct Echoes
    {
        std::array<Echo, maxEchoes> taps {};
        int count = 0;
    };

    /** The DELAY well: the dry sound at the left, then each repeat `seconds`
        after the last, FEEDBACK as loud, until they fall below -40 dB or off
        the well's two seconds. With PING-PONG they take turns, left first. */
    static Echoes echoes (double seconds, float feedback, float tone, float mix, bool pingPong) noexcept
    {
        Echoes e;
        const float dry = Effects::dryGain (mix);
        e.taps[0] = { 0.0f, dry, dry, 1.0f };
        e.count   = 1;

        const float repeat = std::min (Effects::maxFeedback, std::max (0.0f, feedback));
        const float darken = 0.45f + 0.55f * clamp01 (tone);
        float level      = Effects::wetGain (mix);
        float brightness = 1.0f;
        for (int k = 1; e.count < maxEchoes; ++k)
        {
            const auto x = (float) ((double) k * seconds / (double) echoSpanSeconds);
            if (x > 1.0f || level < 0.01f)
                break;
            brightness *= darken;
            Echo echo { x, level, level, brightness };
            if (pingPong && k % 2 == 1)
                echo.right = 0.0f;
            else if (pingPong)
                echo.left = 0.0f;
            e.taps[(size_t) e.count++] = echo;
            level *= repeat;
        }
        return e;
    }

    static constexpr float roomSpanSeconds = 6.0f;   // the REVERB well's width

    /** How high the REVERB well's room stands at x (0..1 across its six
        seconds), on a 60 dB scale: silent through PRE-DELAY, it swells in
        over 40 ms, then falls away in a straight line (in dB) over its ring,
        scaled by MIX. */
    static float roomHeight (float size, float mix, float x, float preDelayMs = 0.0f) noexcept
    {
        const double seconds = (double) clamp01 (x) * (double) roomSpanSeconds - (double) std::max (0.0f, preDelayMs) / 1000.0;
        if (seconds <= 0.0)
            return 0.0f;
        const double ring    = Effects::reverbSeconds (size);
        const double swell   = std::min (1.0, seconds / 0.04);
        return (float) (swell * std::max (0.0, 1.0 - seconds / ring)) * Effects::wetGain (mix);
    }

    /** How bright the room still is at x (0..1): DAMPING dulls it sooner. */
    static float roomBrightness (float size, float damping, float x) noexcept
    {
        const double seconds = (double) clamp01 (x) * (double) roomSpanSeconds;
        const double dulled  = Effects::reverbSeconds (size) * (1.05 - 0.85 * (double) clamp01 (damping));
        return (float) std::max (0.0, 1.0 - seconds / dulled);
    }
};

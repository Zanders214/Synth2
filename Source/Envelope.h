#pragma once

#include <algorithm>
#include <cmath>

// Kept out of line: a stage starting is rare, and its exp and log would
// otherwise swell the per-sample step past what the compiler will inline.
#if defined (_MSC_VER)
 #define SYNTH2_ENVELOPE_COLD __declspec (noinline)
#else
 #define SYNTH2_ENVELOPE_COLD __attribute__ ((noinline, cold))
#endif

/**
    Envelope

    A note's DAHDSR envelope, framework-free so the engine, the editor's AMP
    display and the tests share it. Once the key goes down it waits DELAY,
    rises over ATTACK, stays at the top for HOLD, falls over DECAY to the
    SUSTAIN level and stays there while the key is held; once the key goes up
    it falls to silence over RELEASE.

    Each ramp (the attack, the decay and the release) can bend: at 0 it is
    a straight line, bent up (towards 1) it bulges above that line, and bent
    down (towards -1) it sags below it. So an attack bent up leaps up and
    eases into the top, and a decay or release bent down drops quickly and
    tails off, as a natural sound does. A bend only changes the ramp's
    shape: it still takes its stage's time.

    A note played again while it still sounds starts over from where it is
    rather than from silence, so it never jumps: through the delay it keeps
    its level, and the attack climbs from there at its usual rate. Likewise a
    release falls from wherever the key went up, and turning SUSTAIN while a
    note sits on it glides there rather than stepping.

    The level runs in double precision: a long DECAY to a high SUSTAIN (10 s
    to 99%, say) steps by less than half a float's resolution near the top,
    so in floats the level would never move.
*/
class Envelope
{
public:
    enum class Stage { idle, delay, attack, hold, decay, sustain, release };

    /** The times in seconds; SUSTAIN is a level, 0..1. */
    struct Parameters
    {
        float delay   = 0.0f;
        float attack  = 0.005f;
        float hold    = 0.0f;
        float decay   = 0.3f;
        float sustain = 1.0f;
        float release = 0.03f;

        // Each ramp's bend, -1..1 (0 is a straight line).
        float attackCurve  = 0.0f;
        float decayCurve   = 0.0f;
        float releaseCurve = 0.0f;
    };

    /** How steep a ramp bent all the way (a bend of 1) is. */
    static constexpr double maxSteepness = 8.0;

    /** How far along its way (0..1) a ramp is once `time` (0..1) of it has
        passed, with steepness `k` (0 for a straight line; above 0 it starts
        slowly and finishes quickly). */
    static double travel (double time, double k) noexcept
    {
        return isStraight (k) ? time : std::expm1 (k * time) / std::expm1 (k);
    }

    /** How much of its time (0..1) a ramp of steepness `k` takes to go
        `distance` (0..1) of its way: travel's inverse. */
    static double timeFor (double distance, double k) noexcept
    {
        return isStraight (k) ? distance : std::log1p (distance * std::expm1 (k)) / k;
    }

    /** The steepness of a rising ramp (the attack) bent by `bend`. */
    static double risingSteepness (float bend) noexcept  { return -maxSteepness * (double) std::clamp (bend, -1.0f, 1.0f); }

    /** The steepness of a falling ramp (the decay or release) bent by `bend`. */
    static double fallingSteepness (float bend) noexcept { return maxSteepness * (double) std::clamp (bend, -1.0f, 1.0f); }

    /** How long a change of SUSTAIN takes to glide all the way, top to bottom. */
    static constexpr double sustainGlideSeconds = 0.02;

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        recalculate();
    }

    void setParameters (const Parameters& newParameters) noexcept
    {
        parameters = newParameters;
        parameters.sustain = std::clamp (parameters.sustain, 0.0f, 1.0f);
        recalculate();
    }

    const Parameters& getParameters() const noexcept { return parameters; }

    /** The key went down. */
    void noteOn() noexcept
    {
        elapsed = 0.0;
        if (delaySamples > 0.0)
            stage = Stage::delay;
        else
            startAttack();
    }

    /** The key went up: fall to silence over RELEASE, from where it is now. */
    void noteOff() noexcept
    {
        if (stage == Stage::idle)
            return;
        releaseFrom = level;
        stage       = Stage::release;
        ramp.start (0.0, releaseK, releaseStep);
    }

    /** Silence, at once. */
    void reset() noexcept
    {
        stage   = Stage::idle;
        level   = 0.0;
        elapsed = 0.0;
    }

    bool  isActive() const noexcept { return stage != Stage::idle; }
    Stage getStage() const noexcept { return stage; }
    float getLevel() const noexcept { return (float) level; }

    /** How far through its stage's time the envelope is, 0..1 (0.5 while it sustains). */
    float getProgress() const noexcept
    {
        switch (stage)
        {
            case Stage::delay:   return fraction (elapsed, delaySamples);
            case Stage::attack:
            case Stage::decay:
            case Stage::release: return (float) std::clamp (ramp.time, 0.0, 1.0);
            case Stage::hold:    return fraction (elapsed, holdSamples);
            case Stage::sustain: return 0.5f;
            case Stage::idle:    break;
        }
        return 0.0f;
    }

    /** Steps one sample on and returns the level, 0..1. */
    float getNextSample() noexcept
    {
        switch (stage)
        {
            case Stage::idle:    break;
            case Stage::delay:   waitToAttack(); break;
            case Stage::attack:  rise(); break;
            case Stage::hold:    wait (holdSamples, Stage::decay); break;
            case Stage::decay:   fall(); break;
            case Stage::sustain: settle(); break;
            case Stage::release: fade(); break;
        }
        return (float) level;
    }

    /** Steps `samples` on without wanting the levels, for an oscillator that
        isn't playing: once the envelope has settled, silent or on its
        SUSTAIN level, there is nothing left to step. */
    void advance (int samples) noexcept
    {
        for (int i = 0; i < samples && ! isSettled(); ++i)
            getNextSample();
    }

    /** Whether stepping on would change nothing: silent, or resting on its
        SUSTAIN level, until the key goes up or a parameter changes. */
    bool isSettled() const noexcept
    {
        return stage == Stage::idle
            || (stage == Stage::sustain && ! (level < sustain()) && ! (level > sustain()));
    }

private:
    double sustain() const noexcept { return (double) parameters.sustain; }

    static bool isStraight (double k) noexcept { return std::abs (k) < 1.0e-4; }

    static float fraction (double done, double total) noexcept
    {
        return total > 0.0 ? (float) std::clamp (done / total, 0.0, 1.0) : 1.0f;
    }

    /** Where a ramp is: how much of its time has passed, stepped on a sample
        at a time, and how far along its way that has got it. A bent ramp's
        exponential is carried from sample to sample by one multiply. */
    struct Ramp
    {
        double time     = 0.0;   // 0..1 of the stage's time
        double timeStep = 0.0;   // per sample
        double k        = 0.0;   // its steepness
        double power    = 1.0;   // e^(k * time)
        double factor   = 1.0;   // e^(k * timeStep)
        double scale    = 1.0;   // 1 / (e^k - 1)

        void start (double startTime, double steepness, double step) noexcept
        {
            k        = steepness;
            time     = startTime;
            timeStep = step;
            power    = std::exp (k * time);
            factor   = std::exp (k * timeStep);
            scale    = isStraight (k) ? 1.0 : 1.0 / std::expm1 (k);
        }

        /** Steps on a sample, returning how far along its way it is (1 at the end). */
        double step() noexcept
        {
            time  += timeStep;
            power *= factor;
            if (time >= 1.0)
                return 1.0;
            return isStraight (k) ? time : (power - 1.0) * scale;
        }
    };

    // Each stage's step per sample (an instant one for a zero time), worked
    // out again whenever the parameters, the sample rate or the release's
    // starting level change. A ramp under way carries on from the level it
    // has reached, so a change never makes it jump.
    void recalculate() noexcept
    {
        const auto perSample = [this] (float seconds)
        {
            return seconds > 0.0f ? 1.0 / ((double) seconds * sampleRate) : 2.0;
        };
        delaySamples = (double) parameters.delay * sampleRate;
        holdSamples  = (double) parameters.hold * sampleRate;
        attackStep   = perSample (parameters.attack);
        decayStep    = perSample (parameters.decay);
        releaseStep  = perSample (parameters.release);
        attackK      = risingSteepness (parameters.attackCurve);
        decayK       = fallingSteepness (parameters.decayCurve);
        releaseK     = fallingSteepness (parameters.releaseCurve);
        glideStep    = 1.0 / (sustainGlideSeconds * sampleRate);

        switch (stage)
        {
            case Stage::attack:  ramp.start (timeFor (level, attackK), attackK, attackStep); break;
            case Stage::decay:   resumeDecay(); break;
            case Stage::release: ramp.start (releaseFrom > 0.0 ? timeFor (1.0 - level / releaseFrom, releaseK) : 1.0, releaseK, releaseStep); break;
            case Stage::idle:
            case Stage::delay:
            case Stage::hold:
            case Stage::sustain: break;
        }
    }

    // The attack climbs from wherever the level is (silence, or where a
    // note played again had got to), at its usual pace.
    SYNTH2_ENVELOPE_COLD void startAttack() noexcept
    {
        stage = Stage::attack;
        ramp.start (timeFor (std::clamp (level, 0.0, 1.0), attackK), attackK, attackStep);
    }

    // The decay falls from the top to SUSTAIN (nothing to fall with SUSTAIN at the top).
    SYNTH2_ENVELOPE_COLD void startDecay() noexcept
    {
        stage = sustain() < 1.0 ? Stage::decay : Stage::sustain;
        ramp.start (0.0, decayK, decayStep);
    }

    // The decay carries on from the level it has reached towards SUSTAIN. One
    // turned above that level ends the decay there, and the level glides up
    // to it.
    SYNTH2_ENVELOPE_COLD void resumeDecay() noexcept
    {
        if (level <= sustain() || sustain() >= 1.0)
        {
            stage = Stage::sustain;
            return;
        }
        ramp.start (timeFor ((1.0 - level) / (1.0 - sustain()), decayK), decayK, decayStep);
    }

    void waitToAttack() noexcept
    {
        elapsed += 1.0;
        if (elapsed >= delaySamples)
        {
            elapsed = 0.0;
            startAttack();
        }
    }

    void wait (double length, Stage next) noexcept
    {
        elapsed += 1.0;
        if (elapsed >= length)
        {
            elapsed = 0.0;
            if (next == Stage::decay)
                startDecay();
            else
                stage = next;
        }
    }

    void rise() noexcept
    {
        level = ramp.step();
        if (level >= 1.0)
        {
            level   = 1.0;
            elapsed = 0.0;
            if (holdSamples > 0.0)
                stage = Stage::hold;
            else
                startDecay();
        }
    }

    // The decay lands on SUSTAIN.
    void fall() noexcept
    {
        const double along = ramp.step();
        level = 1.0 - (1.0 - sustain()) * along;
        if (along >= 1.0)
        {
            level = sustain();
            stage = Stage::sustain;
        }
    }

    void settle() noexcept
    {
        const double target = sustain();
        level = level < target ? std::min (target, level + glideStep) : std::max (target, level - glideStep);
    }

    void fade() noexcept
    {
        const double along = ramp.step();
        level = releaseFrom * (1.0 - along);
        if (along >= 1.0)
            reset();
    }

    Parameters parameters;
    double sampleRate   = 44100.0;
    double delaySamples = 0.0;
    double holdSamples  = 0.0;
    double elapsed      = 0.0;
    double attackStep   = 0.0;
    double decayStep    = 0.0;
    double releaseStep  = 0.0;
    double attackK      = 0.0;
    double decayK       = 0.0;
    double releaseK     = 0.0;
    Ramp   ramp;
    double glideStep    = 0.0;
    double releaseFrom  = 0.0;
    double level        = 0.0;
    Stage  stage        = Stage::idle;
};

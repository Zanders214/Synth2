#pragma once

#include <algorithm>

/**
    Envelope

    A note's DAHDSR envelope, framework-free so the engine, the editor's AMP
    display and the tests share it. Once the key goes down it waits DELAY,
    rises over ATTACK, stays at the top for HOLD, falls over DECAY to the
    SUSTAIN level and stays there while the key is held; once the key goes up
    it falls to silence over RELEASE. Every ramp is a straight line.

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
    };

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
        stage   = delaySamples > 0.0 ? Stage::delay : Stage::attack;
    }

    /** The key went up: fall to silence over RELEASE, from where it is now. */
    void noteOff() noexcept
    {
        if (stage == Stage::idle)
            return;
        releaseFrom = level;
        stage       = Stage::release;
        recalculate();
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

    /** How far through its stage the envelope is, 0..1 (0.5 while it sustains). */
    float getProgress() const noexcept
    {
        switch (stage)
        {
            case Stage::delay:   return fraction (elapsed, delaySamples);
            case Stage::attack:  return (float) level;
            case Stage::hold:    return fraction (elapsed, holdSamples);
            case Stage::decay:   return parameters.sustain < 1.0f ? fraction (1.0 - level, 1.0 - sustain()) : 1.0f;
            case Stage::sustain: return 0.5f;
            case Stage::release: return releaseFrom > 0.0 ? fraction (releaseFrom - level, releaseFrom) : 1.0f;
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
            case Stage::delay:   wait (delaySamples, Stage::attack); break;
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

private:
    bool isSettled() const noexcept
    {
        return stage == Stage::idle
            || (stage == Stage::sustain && ! (level < sustain()) && ! (level > sustain()));
    }

    double sustain() const noexcept { return (double) parameters.sustain; }

    static float fraction (double done, double total) noexcept
    {
        return total > 0.0 ? (float) std::clamp (done / total, 0.0, 1.0) : 1.0f;
    }

    // Each stage's rate per sample (an instant one for a zero time), worked
    // out again whenever the parameters, the sample rate or the release's
    // starting level change.
    void recalculate() noexcept
    {
        const auto perSample = [this] (double distance, float seconds)
        {
            return seconds > 0.0f ? distance / ((double) seconds * sampleRate) : 2.0;
        };
        delaySamples = (double) parameters.delay * sampleRate;
        holdSamples  = (double) parameters.hold * sampleRate;
        attackRate   = perSample (1.0, parameters.attack);
        decayRate    = perSample (1.0 - sustain(), parameters.decay);
        releaseRate  = perSample (releaseFrom, parameters.release);
        glideStep    = 1.0 / (sustainGlideSeconds * sampleRate);
    }

    void wait (double length, Stage next) noexcept
    {
        elapsed += 1.0;
        if (elapsed >= length)
        {
            elapsed = 0.0;
            stage   = next;
        }
    }

    void rise() noexcept
    {
        level += attackRate;
        if (level >= 1.0)
        {
            level   = 1.0;
            elapsed = 0.0;
            stage   = holdSamples > 0.0 ? Stage::hold : Stage::decay;
        }
    }

    // The decay lands on SUSTAIN. One turned above where the decay has got
    // to ends the decay there instead, and the level glides up to it.
    void fall() noexcept
    {
        const double before = level;
        level -= decayRate;
        if (level <= sustain())
        {
            level = std::min (before, sustain());
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
        level -= releaseRate;
        if (level <= 0.0)
            reset();
    }

    Parameters parameters;
    double sampleRate   = 44100.0;
    double delaySamples = 0.0;
    double holdSamples  = 0.0;
    double elapsed      = 0.0;
    double attackRate   = 0.0;
    double decayRate    = 0.0;
    double releaseRate  = 0.0;
    double glideStep    = 0.0;
    double releaseFrom  = 0.0;
    double level        = 0.0;
    Stage  stage        = Stage::idle;
};

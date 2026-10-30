#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
    EffectSettings

    The effects' settings and the sums about them that both the engine (see
    Effects.h) and the editor's displays (see Synth2VisualModel.h) need: how
    long the delay waits, how long the reverb rings, the drive's curves, how
    MIX shares the dry sound and the effect. Kept framework-free, like Unison.h, so the
    displays never drift from what plays.
*/
namespace Effects
{
    /** The delay's note lengths when it follows the tempo: its name and its
        length in beats (a quarter note). */
    struct Division
    {
        const char* name;
        double      beats;
    };

    inline constexpr std::array<Division, 12> divisions {{
        { "1/32",  0.125 },
        { "1/16T", 1.0 / 6.0 },
        { "1/16",  0.25 },
        { "1/16D", 0.375 },
        { "1/8T",  1.0 / 3.0 },
        { "1/8",   0.5 },
        { "1/8D",  0.75 },
        { "1/4T",  2.0 / 3.0 },
        { "1/4",   1.0 },
        { "1/4D",  1.5 },
        { "1/2",   2.0 },
        { "1/1",   4.0 } }};

    inline constexpr int    dottedEighth = 6;   // the delay's note to start with: 375 ms at 120 BPM
    inline constexpr float  minTimeMs    = 10.0f;
    inline constexpr float  maxTimeMs    = 2000.0f;
    inline constexpr float  maxFeedback  = 0.95f;
    inline constexpr double maxSeconds   = 4.0;     // the longest repeat, a whole note at 60 BPM
    inline constexpr double fadeSeconds  = 0.02;

    struct DelaySettings
    {
        bool  on        = false;
        bool  sync      = false;
        float timeMs    = 375.0f;
        int   division  = dottedEighth;
        float feedback  = 0.35f;
        float tone      = 0.6f;
        float mix       = 0.25f;
        bool  pingPong  = false;
    };

    struct ReverbSettings
    {
        bool  on         = false;
        float size       = 0.5f;
        float damping    = 0.4f;
        float width      = 1.0f;
        float mix        = 0.25f;
        float preDelayMs = 20.0f;
    };

    inline constexpr float maxPreDelayMs = 250.0f;

    /** The drive's curves: WARM rounds both sides off alike (odd
        harmonics, like a transistor pushed gently), TUBE leans one way
        (even harmonics too, like a valve) and HARD clips flat. */
    enum class DriveShape { warm, tube, hard };
    inline constexpr std::array<const char*, 3> driveShapeNames { "WARM", "TUBE", "HARD" };

    struct DriveSettings
    {
        bool  on     = false;
        float amount = 0.4f;   // DRIVE: 0..1, up to maxDriveDb into the curve
        int   shape  = 0;      // a DriveShape
        float tone   = 0.7f;
        float mix    = 1.0f;
    };

    inline constexpr float maxDriveDb = 36.0f;
    inline constexpr float tubeBias   = 0.35f;   // how far TUBE leans

    /** How hard DRIVE pushes into the curve: 0 dB to +36 dB. */
    inline float driveGain (float amount) noexcept
    {
        return std::pow (10.0f, maxDriveDb * std::clamp (amount, 0.0f, 1.0f) / 20.0f);
    }

    inline DriveShape driveShapeOf (int shape) noexcept { return (DriveShape) std::clamp (shape, 0, 2); }

    /** The curve itself, at gain 1: about x for small x, never past 1. */
    inline float driveCurve (DriveShape shape, float x) noexcept
    {
        switch (shape)
        {
            case DriveShape::tube:
            {
                // tanh shifted off-centre, re-centred and re-scaled to slope 1 at 0.
                const float lean = std::tanh (tubeBias);
                return (std::tanh (x + tubeBias) - lean) / (1.0f - lean * lean);
            }
            case DriveShape::hard: return std::clamp (x, -1.0f, 1.0f);
            case DriveShape::warm: break;
        }
        return std::tanh (x);
    }

    /** log(cosh(x)), without overflowing for large x. */
    inline double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - std::log (2.0);
    }

    /** The curve's antiderivative, which the engine differences between
        samples to keep the new harmonics from folding back down as
        aliasing. */
    inline double driveCurveIntegral (DriveShape shape, double x) noexcept
    {
        switch (shape)
        {
            case DriveShape::tube:
            {
                const double lean = std::tanh ((double) tubeBias);
                return (logCosh (x + tubeBias) - lean * x) / (1.0 - lean * lean);
            }
            case DriveShape::hard: return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
            case DriveShape::warm: break;
        }
        return logCosh (x);
    }

    /** The output gain that keeps a moderate note (a quarter of full scale)
        about as loud driven as it was clean, so turning DRIVE up changes the
        colour more than the level. */
    inline float driveMakeUp (DriveShape shape, float gain) noexcept
    {
        constexpr float level = 0.25f;
        const float swing = driveCurve (shape, gain * level) - driveCurve (shape, -gain * level);
        return swing > 0.0f ? 2.0f * level / swing : 1.0f;
    }

    /** What the drive does to an input `x`: pushed into the curve and
        brought back to level (before TONE and MIX). */
    inline float driveTransfer (const DriveSettings& drive, float x) noexcept
    {
        const auto  shape = driveShapeOf (drive.shape);
        const float gain  = driveGain (drive.amount);
        return driveMakeUp (shape, gain) * driveCurve (shape, gain * x);
    }

    struct Settings
    {
        DriveSettings  drive;
        DelaySettings  delay;
        ReverbSettings reverb;
        bool   reverbFirst = false;
        double bpm         = 120.0;   // the host's tempo, for a synced delay
    };

    /** How long the delay waits before each repeat, in seconds. */
    inline double delaySeconds (const DelaySettings& delay, double bpm) noexcept
    {
        if (! delay.sync)
            return std::clamp ((double) delay.timeMs, (double) minTimeMs, (double) maxTimeMs) / 1000.0;
        const auto& division = divisions[(size_t) std::clamp (delay.division, 0, (int) divisions.size() - 1)];
        return std::clamp (division.beats * 60.0 / std::clamp (bpm, 20.0, 999.0), 0.001, maxSeconds);
    }

    /** How long the reverb rings before it falls 60 dB, from SIZE: 0.3 s
        for the smallest room, about 2 s in the middle, 12 s for the largest. */
    inline double reverbSeconds (float size) noexcept
    {
        return 0.3 * std::pow (40.0, std::clamp ((double) size, 0.0, 1.0));
    }

    /** How long the effects ring on after the notes stop: the delay's
        repeats until they fall 60 dB (at most half a minute) and the
        reverb's wait and ring, one after the other. */
    inline double tailSeconds (const Settings& settings) noexcept
    {
        double tail = 0.0;
        if (settings.delay.on)
        {
            const double feedback = std::clamp ((double) settings.delay.feedback, 0.0, (double) maxFeedback);
            const double repeats  = feedback > 0.001 ? std::log (0.001) / std::log (feedback) : 0.0;
            tail += std::min (30.0, delaySeconds (settings.delay, settings.bpm) * (1.0 + repeats));
        }
        if (settings.reverb.on)
            tail += std::clamp ((double) settings.reverb.preDelayMs, 0.0, (double) maxPreDelayMs) / 1000.0
                  + reverbSeconds (settings.reverb.size);
        return tail;
    }

    /** How much of the dry sound MIX keeps: all of it up to the middle,
        then less and less, to none at the top. */
    inline float dryGain (float mix) noexcept { return std::min (1.0f, 2.0f - 2.0f * std::clamp (mix, 0.0f, 1.0f)); }

    /** And how much of the effect it adds: more and more up to the middle,
        then all of it. */
    inline float wetGain (float mix) noexcept { return std::min (1.0f, 2.0f * std::clamp (mix, 0.0f, 1.0f)); }
}

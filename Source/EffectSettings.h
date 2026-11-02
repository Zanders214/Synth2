#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
    EffectSettings

    The effects' settings and the sums about them that both the engine (see
    Effects.h) and the editor's displays (see Synth2VisualModel.h) need: how
    long the delay waits, how long the reverb rings, the drive's curves, the
    EQ's filters, how MIX shares the dry sound and the effect. Kept framework-free, like Unison.h, so the
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

    // ---- The EQ ------------------------------------------------------------------

    /** The EQ: LOW CUT and HIGH CUT, 12 or 24 dB an octave as SLOPE says
        (each out at the end of its range), a LOW shelf, a MID bell as wide
        as Q says and a HIGH shelf, each GAIN at its FREQ. */
    struct EqSettings
    {
        bool  on         = false;
        float lowCutHz   = 20.0f;      // minLowCutHz: out
        float highCutHz  = 20000.0f;   // maxHighCutHz: out
        int   slope      = 0;          // an EqSlope
        float lowGainDb  = 0.0f;
        float lowHz      = 120.0f;
        float midGainDb  = 0.0f;
        float midHz      = 1000.0f;
        float midQ       = 0.7f;
        float highGainDb = 0.0f;
        float highHz     = 8000.0f;
    };

    enum class EqSlope { gentle, steep };   // 12 or 24 dB an octave
    inline constexpr std::array<const char*, 2> eqSlopeNames { "12 dB", "24 dB" };

    inline constexpr float minLowCutHz  = 20.0f;
    inline constexpr float maxLowCutHz  = 2000.0f;
    inline constexpr float minHighCutHz = 1000.0f;
    inline constexpr float maxHighCutHz = 20000.0f;
    inline constexpr float minLowHz     = 30.0f;
    inline constexpr float maxLowHz     = 1000.0f;
    inline constexpr float minMidHz     = 100.0f;
    inline constexpr float maxMidHz     = 10000.0f;
    inline constexpr float minHighHz    = 1000.0f;
    inline constexpr float maxHighHz    = 16000.0f;
    inline constexpr float minEqQ       = 0.3f;
    inline constexpr float maxEqQ       = 6.0f;
    inline constexpr float maxEqGainDb  = 18.0f;

    /** Whether LOW CUT is at the bottom of its range, and so out. */
    inline bool lowCutIsOut (float hz) noexcept  { return hz <= minLowCutHz * 1.001f; }
    /** Whether HIGH CUT is at the top of its range, and so out. */
    inline bool highCutIsOut (float hz) noexcept { return hz >= maxHighCutHz * 0.999f; }

    /** One second-order filter, as the engine runs it: a0 is 1. */
    struct Biquad
    {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;

        bool isIdentity() const noexcept
        {
            return std::abs (b0 - 1.0) < 1.0e-12 && std::abs (b1) < 1.0e-12 && std::abs (b2) < 1.0e-12
                && std::abs (a1) < 1.0e-12 && std::abs (a2) < 1.0e-12;
        }

        /** How loud it leaves a sine at `hz`, at `sampleRate`. */
        double magnitudeAt (double hz, double sampleRate) const noexcept
        {
            // The top and the bottom of the transfer function, at e^-jw.
            const double w     = 2.0 * pi * hz / sampleRate;
            const double c1    = std::cos (w);
            const double s1    = std::sin (w);
            const double c2    = std::cos (2.0 * w);
            const double s2    = std::sin (2.0 * w);
            const double numRe = b0 + b1 * c1 + b2 * c2;
            const double numIm = b1 * s1 + b2 * s2;
            const double denRe = 1.0 + a1 * c1 + a2 * c2;
            const double denIm = a1 * s1 + a2 * s2;
            return std::sqrt ((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm));
        }

        static constexpr double pi = 3.14159265358979323846;
    };

    namespace EqFilters
    {
        // The cookbook's filters (Robert Bristow-Johnson's), normalised so a0 is 1.
        inline Biquad normalised (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }

        // The angle `hz` turns through each sample, kept under the top of the band.
        inline double angleOf (double hz, double sampleRate) noexcept
        {
            return 2.0 * Biquad::pi * std::clamp (hz, 1.0, 0.45 * sampleRate) / sampleRate;
        }

        inline Biquad lowCut (double hz, double q, double sampleRate) noexcept
        {
            const double w = angleOf (hz, sampleRate);
            const double c = std::cos (w);
            const double alpha = std::sin (w) / (2.0 * q);
            return normalised ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }

        inline Biquad highCut (double hz, double q, double sampleRate) noexcept
        {
            const double w = angleOf (hz, sampleRate);
            const double c = std::cos (w);
            const double alpha = std::sin (w) / (2.0 * q);
            return normalised ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }

        inline Biquad bell (double hz, double gainDb, double q, double sampleRate) noexcept
        {
            if (std::abs (gainDb) < 1.0e-3)
                return {};
            const double a = std::pow (10.0, gainDb / 40.0);
            const double w = angleOf (hz, sampleRate);
            const double c = std::cos (w);
            const double alpha = std::sin (w) / (2.0 * q);
            return normalised (1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a);
        }

        // A shelf as steep as it can be without overshooting (the cookbook's S = 1).
        inline Biquad lowShelf (double hz, double gainDb, double sampleRate) noexcept
        {
            if (std::abs (gainDb) < 1.0e-3)
                return {};
            const double a = std::pow (10.0, gainDb / 40.0);
            const double w = angleOf (hz, sampleRate);
            const double c = std::cos (w);
            const double lift = 2.0 * std::sqrt (a) * std::sin (w) / std::sqrt (2.0);
            return normalised (a * ((a + 1.0) - (a - 1.0) * c + lift), 2.0 * a * ((a - 1.0) - (a + 1.0) * c),
                               a * ((a + 1.0) - (a - 1.0) * c - lift), (a + 1.0) + (a - 1.0) * c + lift,
                               -2.0 * ((a - 1.0) + (a + 1.0) * c), (a + 1.0) + (a - 1.0) * c - lift);
        }

        inline Biquad highShelf (double hz, double gainDb, double sampleRate) noexcept
        {
            if (std::abs (gainDb) < 1.0e-3)
                return {};
            const double a = std::pow (10.0, gainDb / 40.0);
            const double w = angleOf (hz, sampleRate);
            const double c = std::cos (w);
            const double lift = 2.0 * std::sqrt (a) * std::sin (w) / std::sqrt (2.0);
            return normalised (a * ((a + 1.0) + (a - 1.0) * c + lift), -2.0 * a * ((a - 1.0) + (a + 1.0) * c),
                               a * ((a + 1.0) + (a - 1.0) * c - lift), (a + 1.0) - (a - 1.0) * c + lift,
                               2.0 * ((a - 1.0) - (a + 1.0) * c), (a + 1.0) - (a - 1.0) * c - lift);
        }
    }

    /** The EQ's filters, one after the other: LOW CUT (two stages, the
        second only at 24 dB), the LOW shelf, the MID bell, the HIGH shelf,
        then HIGH CUT (two stages again). A filter that is out, or at 0 dB,
        is left as it is (it passes the sound untouched). */
    inline constexpr int numEqStages = 7;
    using EqStages = std::array<Biquad, (size_t) numEqStages>;

    inline EqStages eqStages (const EqSettings& eq, double sampleRate) noexcept
    {
        // Butterworth: one stage at Q 0.71 for 12 dB, two at 0.54 and 1.31 for 24.
        const bool   steep  = eq.slope == (int) EqSlope::steep;
        const double firstQ = steep ? 0.54119610 : 0.70710678;
        constexpr double secondQ = 1.30656296;

        EqStages stages {};
        if (! lowCutIsOut (eq.lowCutHz))
        {
            stages[0] = EqFilters::lowCut (eq.lowCutHz, firstQ, sampleRate);
            if (steep)
                stages[1] = EqFilters::lowCut (eq.lowCutHz, secondQ, sampleRate);
        }
        stages[2] = EqFilters::lowShelf (eq.lowHz, std::clamp (eq.lowGainDb, -maxEqGainDb, maxEqGainDb), sampleRate);
        stages[3] = EqFilters::bell (eq.midHz, std::clamp (eq.midGainDb, -maxEqGainDb, maxEqGainDb),
                                     std::clamp (eq.midQ, minEqQ, maxEqQ), sampleRate);
        stages[4] = EqFilters::highShelf (eq.highHz, std::clamp (eq.highGainDb, -maxEqGainDb, maxEqGainDb), sampleRate);
        if (! highCutIsOut (eq.highCutHz))
        {
            stages[5] = EqFilters::highCut (eq.highCutHz, firstQ, sampleRate);
            if (steep)
                stages[6] = EqFilters::highCut (eq.highCutHz, secondQ, sampleRate);
        }
        return stages;
    }

    /** How loud the EQ leaves a sine at `hz`, in dB. */
    inline double eqResponseDb (const EqSettings& eq, double hz, double sampleRate) noexcept
    {
        double gain = 1.0;
        for (const auto& stage : eqStages (eq, sampleRate))
            gain *= stage.magnitudeAt (hz, sampleRate);
        return 20.0 * std::log10 (std::max (gain, 1.0e-9));
    }

    // ---- The chain ---------------------------------------------------------------

    /** The four effects, and the order sound goes through them: each
        once, first to last. (The EQ came last, so its number is the
        highest.) */
    enum class Kind { drive, delay, reverb, eq };
    inline constexpr int numKinds = 4;
    using Order = std::array<Kind, (size_t) numKinds>;
    inline constexpr Order defaultOrder { Kind::drive, Kind::eq, Kind::delay, Kind::reverb };

    /** Whether `order` has each effect exactly once. */
    inline bool isOrder (const Order& order) noexcept
    {
        std::array<bool, (size_t) numKinds> seen {};
        for (const auto kind : order)
        {
            const auto k = (size_t) kind;
            if (k >= seen.size() || seen[k])
                return false;
            seen[k] = true;
        }
        return true;
    }

    /** An order as one number (so it can be handed over in one atomic), and
        back: each place a digit, first place lowest, counting in numKinds. */
    inline int encode (const Order& order) noexcept
    {
        int code = 0;
        for (auto place = order.size(); place-- > 0;)
            code = code * numKinds + (int) order[place];
        return code;
    }

    /** The order `code` stands for, or the default for anything that isn't one. */
    inline Order decode (int code) noexcept
    {
        if (code < 0)
            return defaultOrder;
        Order order {};
        int rest = code;
        for (auto& kind : order)
        {
            kind = (Kind) (rest % numKinds);
            rest /= numKinds;
        }
        return rest == 0 && isOrder (order) ? order : defaultOrder;
    }

    struct Settings
    {
        DriveSettings  drive;
        DelaySettings  delay;
        ReverbSettings reverb;
        EqSettings     eq;
        Order  order = defaultOrder;
        double bpm   = 120.0;   // the host's tempo, for a synced delay
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

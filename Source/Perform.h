#pragma once

#include "LoopFx.h"

/** Perform: what a hand does to a whole mix while it plays. */
namespace pulse
{
    /** Two things for playing a mix live, made to sit on the master (the Performance window puts it there):
        a sweep filter on one control (to the left a low-pass closes, to the right a high-pass opens, in the middle
        it is out of the way), and fills: while a fill is held, the last quarter, eighth, sixteenth or thirty-second
        note is repeated in time with the song, and the song itself comes back when it is let go. */
    class Perform : public ModEffect
    {
    public:
        enum { pSweep, pResonance, pFill };
        static inline const char* xmlTypeName = "pulsePerform";
        static const char* getPluginName() { return "Perform"; }
        static inline const StringArray fillNames { "Off", "1/4", "1/8", "1/16", "1/32" };
        static constexpr double fillBeats[] = { 0.0, 1.0, 0.5, 0.25, 0.125 };
        static constexpr int numFills = 4;

        explicit Perform (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("sweep", "Sweep", { -1.0f, 1.0f }, 0.0f, [] (float v) { return sweepText (v); });
            add ("resonance", "Resonance", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("fill", "Fill", steps (fillNames.size()), 0.0f, fmt::choice (fillNames));
        }

        String getName() const override       { return "Perform"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Sweep", { pSweep, pResonance } }, { "Fill", { pFill } } }; }

        /** The middle of the sweep is a little wide, so that "off" is easy to find by hand. */
        static bool sweepOn (float v) { return std::abs (v) > 0.02f; }
        /** Where the filter is for a place on the sweep: to the left a low-pass from 18 kHz down to 80 Hz, to the right a high-pass from 25 Hz up to 8 kHz. */
        static float cutoffFor (float v)
        {
            const float a = jlimit (0.0f, 1.0f, (std::abs (v) - 0.02f) / 0.98f);
            return v < 0.0f ? 18000.0f * std::pow (80.0f / 18000.0f, a) : 25.0f * std::pow (8000.0f / 25.0f, a);
        }
        static String sweepText (float v)
        {
            if (! sweepOn (v)) return "Off";
            const float f = cutoffFor (v);
            return String (v < 0.0f ? "Low-pass " : "High-pass ") + (f >= 1000.0f ? String (f / 1000.0f, 1) + " kHz" : String (roundToInt (f)) + " Hz");
        }
        static double qFor (float resonance) { return 0.707 * std::pow (7.0, (double) jlimit (0.0f, 1.0f, resonance)); }

        /** What the sweep leaves of a frequency, as a gain (for the display). */
        float response (double f) const
        {
            const float v = get (pSweep);
            if (! sweepOn (v)) return 1.0f;
            Biquad a, b;
            const auto type = v < 0.0f ? Biquad::Type::highCut : Biquad::Type::lowCut;
            a.set (type, 48000.0, cutoffFor (v), qFor (get (pResonance)), 0.0);
            b.set (type, 48000.0, cutoffFor (v), 0.707, 0.0);
            return (float) (a.magnitude (f, 48000.0) * b.magnitude (f, 48000.0));
        }

        bool canSleepOnSilence() const override { return getInt (pFill) == 0; }   // a held fill goes on with nothing coming in

        // for the display: whether a fill is repeating right now, and how far through its slice it is (0..1)
        std::atomic<bool> repeating { false };
        std::atomic<float> slicePhase { 0.0f };

    protected:
        void prepare() override
        {
            ringSize = (int) (sampleRate * 2.1) + 64;   // a quarter note at 30 BPM is two seconds
            for (auto& b : ring) b.assign ((size_t) ringSize, 0.0f);
            written = repeatAt = 0;
            validFrom = 0;
            sliceStart = 0; sliceLen = 64;
            active = false;
            lastFill = 0;
            wet = 0.0f; filterMix = 0.0f;
            sweepNow = get (pSweep);
            coeffIn = 0; wasFiltering = false;
            for (auto& f : stage1) f = {};
            for (auto& f : stage2) f = {};
            freeBeat = 0.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const double beatsPerSample = bpm.load() / 60.0 / sampleRate;
            double beat = fc.isPlaying ? fc.editTime.getStart().inSeconds() * bpm.load() / 60.0 : freeBeat;
            const int fill = jlimit (0, numFills, getInt (pFill));
            const float sweepWant = get (pSweep);
            const double q = qFor (get (pResonance));
            const float fade = (float) (0.002 * sampleRate);
            const float aWet = std::exp (-1.0f / fade), aSweep = std::exp (-1.0f / (float) (0.02 * sampleRate)), aMix = std::exp (-1.0f / (float) (0.005 * sampleRate));

            if (fill != lastFill)
            {
                if (fill == 0)
                {
                    active = false;
                    validFrom = written;   // what is recorded from here on follows the slice, not what was played before it
                }
                else
                {
                    const int len = jlimit (64, ringSize - 8, (int) std::round (fillBeats[fill] / beatsPerSample));
                    if (active && written >= sliceStart + sliceLen && len <= sliceLen)
                        sliceLen = len;   // a shorter fill while one is held: the start of the same slice
                    else
                    {
                        // from the last line of the grid, if what was played since then is still in memory: the fill is in time
                        const double g = fillBeats[fill];
                        int64 start = written - (int64) std::round ((beat - std::floor (beat / g) * g) / beatsPerSample);
                        if (start < validFrom || active) start = written;
                        sliceStart = start;
                        sliceLen = len;
                        repeatAt = 0;
                        active = true;
                    }
                }
                lastFill = fill;
            }

            for (int i = 0; i < n; ++i)
            {
                const float dryL = l[i], dryR = r[i];
                const bool captured = written >= sliceStart + sliceLen;
                if (! (active && captured))
                {
                    // the memory of what was played: it stands still while a slice is being repeated
                    const auto at = (size_t) (written % ringSize);
                    ring[0][at] = dryL; ring[1][at] = dryR;
                    ++written;
                }
                float outL = dryL, outR = dryR;
                const float target = active && captured ? 1.0f : 0.0f;
                wet = target + (wet - target) * aWet;
                if (wet > 1.0e-4f && written >= sliceStart + sliceLen)
                {
                    const int64 into = repeatAt % sliceLen;
                    const auto at = (size_t) (((sliceStart + into) % ringSize + ringSize) % ringSize);
                    // a short fade at each end of the slice, so that the join does not click
                    const float edge = jmin (1.0f, ((float) into + 1.0f) / fade, ((float) (sliceLen - into)) / fade);
                    outL = dryL + (ring[0][at] * edge - dryL) * wet;
                    outR = dryR + (ring[1][at] * edge - dryR) * wet;
                    ++repeatAt;
                }

                // the sweep
                sweepNow = sweepWant + (sweepNow - sweepWant) * aSweep;
                const bool filtering = sweepOn (sweepNow);
                filterMix = (filtering ? 1.0f : 0.0f) + (filterMix - (filtering ? 1.0f : 0.0f)) * aMix;
                if (filtering || filterMix > 1.0e-4f)
                {
                    if (! wasFiltering) { for (auto& f : stage1) f.reset(); for (auto& f : stage2) f.reset(); coeffIn = 0; }
                    if (--coeffIn <= 0)
                    {
                        coeffIn = 16;
                        if (filtering)   // (on the way out the filter stays where it was while it fades)
                        {
                            const auto type = sweepNow < 0.0f ? Biquad::Type::highCut : Biquad::Type::lowCut;
                            const double f = cutoffFor (sweepNow);
                            for (auto& s : stage1) s.set (type, sampleRate, f, q, 0.0);
                            for (auto& s : stage2) s.set (type, sampleRate, f, 0.707, 0.0);
                        }
                    }
                    const float fl = stage2[0].process (stage1[0].process (outL, 0), 0), fr = stage2[1].process (stage1[1].process (outR, 0), 0);
                    outL += (fl - outL) * filterMix;
                    outR += (fr - outR) * filterMix;
                    wasFiltering = true;
                }
                else wasFiltering = false;

                l[i] = outL; r[i] = outR;
                beat += beatsPerSample;
            }
            freeBeat = beat;
            repeating.store (active && written >= sliceStart + sliceLen);
            slicePhase.store ((float) (repeatAt % sliceLen) / (float) sliceLen);
        }

    private:
        std::array<std::vector<float>, 2> ring;
        int ringSize = 0;
        int64 written = 0, repeatAt = 0, validFrom = 0, sliceStart = 0;
        int sliceLen = 64, lastFill = 0, coeffIn = 0;
        bool active = false, wasFiltering = false;
        float wet = 0.0f, filterMix = 0.0f, sweepNow = 0.0f;
        std::array<Biquad, 2> stage1, stage2;
        double freeBeat = 0.0;
    };
}

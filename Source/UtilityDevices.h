#pragma once

// Included from PulseSynths.h (needs PulseEffect, Biquad, AnalyserFifo).

/** Utility devices: Utility (gain / width / mono / bass mono / phase / balance), Tuner and Spectrum. */
namespace pulse
{
    //==============================================================================
    /** Ableton-style Utility: gain, stereo width (mid/side), mono, bass mono below a frequency,
        channel mode, phase invert, balance, DC filter and mute. */
    class Utility : public PulseEffect
    {
    public:
        enum { pGain, pWidth, pMono, pBassMono, pBassFreq, pChannels, pInvert, pBalance, pDC, pMute };
        static inline const char* xmlTypeName = "pulseUtility";
        static const char* getPluginName() { return "Utility"; }
        static inline const StringArray channelNames { "Stereo", "Left", "Right", "Swap" };
        static inline const StringArray invertNames { "Off", "Left", "Right", "Both" };

        explicit Utility (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("gain", "Gain", { -60.0f, 35.0f }, 0.0f, [] (float v) { return v <= -59.9f ? String ("-inf dB") : fmt::db (v); });
            add ("width", "Width", { 0.0f, 4.0f }, 1.0f, fmt::pct);
            add ("mono", "Mono", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("bassmono", "Bass Mono", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("bassfreq", "Bass Freq", skewed (50.0f, 500.0f, 150.0f), 120.0f, fmt::hz);
            add ("channels", "Channels", steps (channelNames.size()), 0.0f, fmt::choice (channelNames));
            add ("invert", "Phase", steps (invertNames.size()), 0.0f, fmt::choice (invertNames));
            add ("balance", "Balance", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); });
            add ("dc", "DC Filter", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("mute", "Mute", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override     { return "Utility"; }
        String getPluginType() override     { return xmlTypeName; }

        std::vector<Section> getSections() const override
        {
            return { { "Level", { pGain, pBalance, pMute } }, { "Stereo", { pWidth, pMono, pChannels, pInvert } },
                     { "Low end", { pBassMono, pBassFreq, pDC } } };
        }

    protected:
        void prepare() override
        {
            for (auto& b : sideHp) b = {};
            for (auto& d : dc) d = {};
            lastGain = -1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const bool mute = getInt (pMute) > 0;
            const float g = mute || get (pGain) <= -59.9f ? 0.0f : Decibels::decibelsToGain (get (pGain));
            const float bal = get (pBalance);
            const float gl = g * (bal > 0.0f ? 1.0f - bal : 1.0f), gr = g * (bal < 0.0f ? 1.0f + bal : 1.0f);
            const float width = getInt (pMono) > 0 ? 0.0f : get (pWidth);
            const bool bassMono = getInt (pBassMono) > 0, dcOn = getInt (pDC) > 0;
            const int ch = getInt (pChannels), inv = getInt (pInvert);
            if (bassMono)
            {
                const float f = get (pBassFreq);
                if (std::abs (f - lastBassFreq) > 0.5f)
                {
                    for (auto& b : sideHp) b.set (Biquad::Type::lowCut, sampleRate, f, 0.707, 0.0);
                    lastBassFreq = f;
                }
            }
            if (dcOn && dc[0].b0 == 1.0 && dc[0].b1 == 0.0)
                for (auto& d : dc) d.set (Biquad::Type::lowCut, sampleRate, 8.0, 0.707, 0.0);
            if (lastGain < 0.0f) { lastGL = gl; lastGR = gr; lastGain = 1.0f; }

            for (int i = 0; i < n; ++i)
            {
                float L = l[i], R = r[i];
                switch (ch)
                {
                    case 1: R = L; break;
                    case 2: L = R; break;
                    case 3: std::swap (L, R); break;
                    default: break;
                }
                if (inv == 1 || inv == 3) L = -L;
                if (inv == 2 || inv == 3) R = -R;
                if (dcOn) { L = (float) dc[0].process (L, 0); R = (float) dc[1].process (R, 0); }

                const float mid = 0.5f * (L + R);
                float side = 0.5f * (L - R) * width;
                if (bassMono) side = (float) sideHp[0].process (side, 0);   // lows of the side signal removed = mono bass
                L = mid + side;
                R = mid - side;

                const float t = (float) (i + 1) / (float) n;
                l[i] = L * (lastGL + (gl - lastGL) * t);
                r[i] = R * (lastGR + (gr - lastGR) * t);
            }
            lastGL = gl; lastGR = gr;
        }

    private:
        std::array<Biquad, 2> sideHp, dc;
        float lastBassFreq = -1.0f, lastGL = 1.0f, lastGR = 1.0f, lastGain = -1.0f;
    };

    //==============================================================================
    /** Chromatic tuner: passes audio through (or mutes it) while the card shows the note and cents. */
    class Tuner : public PulseEffect
    {
    public:
        enum { pReference, pMute };
        static inline const char* xmlTypeName = "pulseTuner";
        static const char* getPluginName() { return "Tuner"; }

        explicit Tuner (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("ref", "Reference", { 415.0f, 466.0f, 0.1f }, 440.0f, [] (float v) { return "A = " + String (v, 1) + " Hz"; });
            add ("mute", "Output", steps (2), 0.0f, fmt::choice ({ "On", "Muted" }));
        }

        String getName() const override     { return "Tuner"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 260; }
        std::vector<Section> getSections() const override { return { { "Tuner", { pReference, pMute } } }; }

        double getSampleRate() const { return sampleRate; }
        AnalyserFifo fifo;

        /** Fundamental frequency of a mono buffer (YIN), or 0 when there is no clear pitch. */
        static double detectPitch (const float* x, int n, double sr, double minHz = 30.0, double maxHz = 2000.0)
        {
            const int maxLag = jmin (n / 2, (int) (sr / minHz)), minLag = jmax (2, (int) (sr / maxHz));
            if (maxLag <= minLag + 2) return 0.0;
            float rms = 0;
            for (int i = 0; i < n; ++i) rms += x[i] * x[i];
            if (std::sqrt (rms / (float) n) < 0.003f) return 0.0;   // too quiet

            const int w = n - maxLag;
            std::vector<float> d ((size_t) maxLag + 1, 0.0f);
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                float s = 0;
                for (int i = 0; i < w; ++i) { const float diff = x[i] - x[i + tau]; s += diff * diff; }
                d[(size_t) tau] = s;
            }
            // cumulative mean normalised difference picks the period...
            std::vector<float> dn (d.size(), 1.0f);
            float running = 0;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                running += d[(size_t) tau];
                dn[(size_t) tau] = running > 0 ? d[(size_t) tau] * (float) tau / running : 1.0f;
            }
            int best = -1;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (dn[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < maxLag && dn[(size_t) tau + 1] < dn[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            if (best < 0) return 0.0;
            // ...then the raw difference's own minimum (unbiased) and a parabola through it
            while (best > minLag && d[(size_t) best - 1] < d[(size_t) best]) --best;
            while (best + 1 < maxLag && d[(size_t) best + 1] < d[(size_t) best]) ++best;
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float denom = a - 2 * b + c;
            const double t = best + (std::abs (denom) > 1.0e-9f ? 0.5 * (a - c) / denom : 0.0);
            return sr / t;
        }

        /** Note number (fractional) of a frequency for a reference A. */
        static double noteOf (double hz, double refA = 440.0) { return 69.0 + 12.0 * std::log2 (hz / refA); }

    protected:
        void prepare() override {}
        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            fifo.push (l, r, n);
            if (getInt (pMute) > 0)
            {
                FloatVectorOperations::clear (l, n);
                FloatVectorOperations::clear (r, n);
            }
        }
    };

    //==============================================================================
    /** Spectrum analyser: passes audio through; the card shows a large log-frequency spectrum. */
    class Spectrum : public PulseEffect
    {
    public:
        enum { pBlock, pAverage, pFloor, pHold };
        static inline const char* xmlTypeName = "pulseSpectrum";
        static const char* getPluginName() { return "Spectrum"; }
        static inline const StringArray blockNames { "1024", "2048", "4096", "8192" };

        explicit Spectrum (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("block", "Block", steps (blockNames.size()), 2.0f, fmt::choice (blockNames));
            add ("avg", "Average", { 0.0f, 0.98f }, 0.7f, fmt::pct);
            add ("floor", "Range", { -120.0f, -30.0f }, -90.0f, fmt::db);
            add ("hold", "Peak Hold", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override     { return "Spectrum"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 420; }
        std::vector<Section> getSections() const override { return { { "Analyser", { pBlock, pAverage, pFloor, pHold } } }; }

        double getSampleRate() const { return sampleRate; }
        int fftOrder() const { return 10 + getInt (pBlock); }
        AnalyserFifo fifo;

    protected:
        void prepare() override {}
        void process (float* l, float* r, int n, const te::PluginRenderContext&) override { fifo.push (l, r, n); }
    };
}

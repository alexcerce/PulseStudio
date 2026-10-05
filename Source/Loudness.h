#pragma once

// Included from PulseSynths.h (needs PulseEffect and Biquad).

/** Loudness metering to ITU-R BS.1770-4 / EBU R128: momentary (400 ms), short-term (3 s) and gated
    integrated loudness in LUFS, plus 4x oversampled true peak. */
namespace pulse
{
    class LoudnessAnalyser
    {
    public:
        void prepare (double sampleRate)
        {
            const SpinLock::ScopedLockType sl (busy);
            sr = sampleRate;
            // the exact BS.1770 K-weighting filters, re-derived for this sample rate (as in libebur128)
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (MathConstants<double>::pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                for (auto& f : shelf)
                {
                    f.b0 = (Vh + Vb * K / Q + K * K) / a0; f.b1 = 2.0 * (K * K - Vh) / a0; f.b2 = (Vh - Vb * K / Q + K * K) / a0;
                    f.a1 = 2.0 * (K * K - 1.0) / a0;       f.a2 = (1.0 - K / Q + K * K) / a0;
                }
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (MathConstants<double>::pi * f0 / sr), a0 = 1.0 + K / Q + K * K;
                for (auto& f : hpf)
                {
                    f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
                    f.a1 = 2.0 * (K * K - 1.0) / a0; f.a2 = (1.0 - K / Q + K * K) / a0;
                }
            }
            subLen = jmax (1, (int) std::round (sr * 0.1));
            oversampler = std::make_unique<dsp::Oversampling<float>> (2, 2, dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
            oversampler->initProcessing (4096);
            tpCopy.setSize (2, 4096);
            clear();
        }

        void reset()
        {
            const SpinLock::ScopedLockType sl (busy);
            clear();
        }

    private:
        void clear()
        {
            for (auto& f : shelf) f.reset();
            for (auto& f : hpf) f.reset();
            subs.fill (0.0);
            subPos = subCount = 0;
            histSum.fill (0.0);
            histCount.fill (0);
            dirty = true;
            acc = 0.0;
            accN = 0;
            truePeak = 0.0f;
            if (oversampler != nullptr) oversampler->reset();
        }

        /** The mean power of the last four 100 ms pieces (one 400 ms block). */
        double lastBlockPower() const
        {
            double p = 0;
            for (int k = 1; k <= 4; ++k) p += subs[(size_t) ((subPos - k + subSlots) % subSlots)];
            return p * 0.25;
        }

    public:
        /** Feeds stereo audio. Nothing is allocated here: the last 3 s are a ring, and the blocks for
            the integrated loudness are counted in a histogram (0.1 LU steps), however long it runs. */
        void process (const float* l, const float* r, int n)
        {
            // one caller at a time (a second one, e.g. a render next to live playback, skips its metering)
            const SpinLock::ScopedTryLockType sl (busy);
            if (! sl.isLocked() || sr <= 0.0 || oversampler == nullptr) return;
            for (int i = 0; i < n; ++i)
            {
                const double kl = hpf[0].process (shelf[0].process (l[i], 0), 0);
                const double kr = hpf[1].process (shelf[1].process (r[i], 0), 0);
                acc += kl * kl + kr * kr;
                if (++accN >= subLen)
                {
                    subs[(size_t) subPos] = acc / accN;   // the ring keeps 3 s
                    subPos = (subPos + 1) % subSlots;
                    subCount = jmin (subSlots, subCount + 1);
                    acc = 0.0;
                    accN = 0;
                    if (subCount >= 4)   // a new 400 ms block every 100 ms
                    {
                        const double p = lastBlockPower(), lufs = toLufs (p);
                        if (lufs > -70.0)   // the absolute gate
                        {
                            const int bin = jlimit (0, histBins - 1, (int) ((lufs + 70.0) * 10.0));
                            histSum[(size_t) bin] += p;
                            ++histCount[(size_t) bin];
                            dirty = true;
                        }
                    }
                }
            }
            // true peak
            for (int start = 0; start < n; start += 4096)
            {
                const int len = jmin (4096, n - start);
                float* chans[2] = { const_cast<float*> (l + start), const_cast<float*> (r + start) };
                tpCopy.copyFrom (0, 0, chans[0], len);
                tpCopy.copyFrom (1, 0, chans[1], len);
                dsp::AudioBlock<float> block (tpCopy);
                auto up = oversampler->processSamplesUp (block.getSubBlock (0, (size_t) len));
                for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                    for (size_t i = 0; i < up.getNumSamples(); ++i)
                        truePeak = jmax (truePeak, std::abs (up.getSample ((int) ch, (int) i)));
                for (int ch = 0; ch < 2; ++ch)   // the sample peak counts too (filters can undershoot)
                    truePeak = jmax (truePeak, tpCopy.getMagnitude (ch, 0, len));
            }
        }

        static double toLufs (double power) { return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -200.0; }

        double momentary() const { return subCount < 4 ? -200.0 : toLufs (lastBlockPower()); }

        double shortTerm() const
        {
            if (subCount < subSlots) return -200.0;
            double s = 0;
            for (auto p : subs) s += p;
            return toLufs (s / (double) subSlots);
        }

        /** Gated integrated loudness over everything since the last reset (worked out again only when a block was added). */
        double integrated() const
        {
            if (! dirty) return cachedIntegrated;
            dirty = false;
            double sum = 0;
            uint64 count = 0;
            for (int b = 0; b < histBins; ++b) { sum += histSum[(size_t) b]; count += histCount[(size_t) b]; }
            if (count == 0) return cachedIntegrated = -200.0;
            const double relGate = toLufs (sum / (double) count) - 10.0;
            sum = 0;
            count = 0;
            for (int b = 0; b < histBins; ++b)
                if (histCount[(size_t) b] > 0 && toLufs (histSum[(size_t) b] / histCount[(size_t) b]) > relGate) { sum += histSum[(size_t) b]; count += histCount[(size_t) b]; }
            return cachedIntegrated = (count > 0 ? toLufs (sum / (double) count) : -200.0);
        }

        float truePeakDb() const { return Decibels::gainToDecibels (truePeak, -200.0f); }

        /** Measures a whole audio file. */
        struct Result { double integrated = -200.0; float truePeakDb = -200.0f; };
        static Result measureFile (te::Engine& e, const File& f)
        {
            Result res;
            std::unique_ptr<AudioFormatReader> r (e.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
            if (r == nullptr) return res;
            LoudnessAnalyser a;
            a.prepare (r->sampleRate);
            AudioBuffer<float> b (2, 8192);
            for (int64 pos = 0; pos < r->lengthInSamples; pos += 8192)
            {
                const int len = (int) jmin<int64> (8192, r->lengthInSamples - pos);
                r->read (&b, 0, len, pos, true, true);
                if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, len);
                a.process (b.getReadPointer (0), b.getReadPointer (1), len);
            }
            res.integrated = a.integrated();
            res.truePeakDb = a.truePeakDb();
            return res;
        }

    private:
        double sr = 0.0;
        std::array<Biquad, 2> shelf, hpf;
        int subLen = 4800, accN = 0;
        double acc = 0.0;
        static constexpr int subSlots = 30, histBins = 751;   // 3 s of 100 ms pieces; -70 .. +5 LUFS in 0.1 steps
        std::array<double, subSlots> subs {};
        int subPos = 0, subCount = 0;
        std::array<double, histBins> histSum {};
        std::array<uint32, histBins> histCount {};
        mutable bool dirty = true;
        mutable double cachedIntegrated = -200.0;
        SpinLock busy;
        float truePeak = 0.0f;
        std::unique_ptr<dsp::Oversampling<float>> oversampler;
        AudioBuffer<float> tpCopy;
    };

    //==============================================================================
    /** Invisible probe at the end of the master chain feeding the loudness readouts. */
    class LoudnessProbe : public PulseEffect
    {
    public:
        static inline const char* xmlTypeName = "pulseLoudness";
        static const char* getPluginName() { return "Loudness"; }

        explicit LoudnessProbe (te::PluginCreationInfo info) : PulseEffect (info) {}

        String getName() const override     { return "Loudness"; }
        String getPluginType() override     { return xmlTypeName; }
        std::vector<Section> getSections() const override { return {}; }

        std::atomic<float> momentaryLufs { -200.0f }, shortTermLufs { -200.0f }, integratedLufs { -200.0f }, truePeakDb { -200.0f };
        std::atomic<bool> resetRequested { false };

    protected:
        void prepare() override { analyser.prepare (sampleRate); }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            if (resetRequested.exchange (false)) analyser.reset();
            if (! fc.isPlaying) return;   // only measure what is played
            analyser.process (l, r, n);
            if (++blocks % 8 == 0)
            {
                momentaryLufs = (float) analyser.momentary();
                shortTermLufs = (float) analyser.shortTerm();
                integratedLufs = (float) analyser.integrated();
                truePeakDb = analyser.truePeakDb();
            }
        }

    private:
        LoudnessAnalyser analyser;
        int blocks = 0;
    };
}

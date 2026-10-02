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
            reset();
        }

        void reset()
        {
            for (auto& f : shelf) f.reset();
            for (auto& f : hpf) f.reset();
            subs.clear();
            blockPowers.clear();
            acc = 0.0;
            accN = 0;
            truePeak = 0.0f;
            if (oversampler != nullptr) oversampler->reset();
        }

        /** Feeds stereo audio. */
        void process (const float* l, const float* r, int n)
        {
            if (sr <= 0.0) return;
            for (int i = 0; i < n; ++i)
            {
                const double kl = hpf[0].process (shelf[0].process (l[i], 0), 0);
                const double kr = hpf[1].process (shelf[1].process (r[i], 0), 0);
                acc += kl * kl + kr * kr;
                if (++accN >= subLen)
                {
                    subs.push_back (acc / accN);
                    if (subs.size() > 30) subs.erase (subs.begin());   // keep 3 s
                    acc = 0.0;
                    accN = 0;
                    if (subs.size() >= 4)   // a new 400 ms block every 100 ms
                    {
                        const size_t k = subs.size();
                        blockPowers.push_back ((subs[k - 1] + subs[k - 2] + subs[k - 3] + subs[k - 4]) * 0.25);
                    }
                }
            }
            // true peak
            for (int start = 0; start < n; start += 4096)
            {
                const int len = jmin (4096, n - start);
                float* chans[2] = { const_cast<float*> (l + start), const_cast<float*> (r + start) };
                tpCopy.setSize (2, len, false, false, true);
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

        double momentary() const
        {
            if (subs.size() < 4) return -200.0;
            const size_t k = subs.size();
            return toLufs ((subs[k - 1] + subs[k - 2] + subs[k - 3] + subs[k - 4]) * 0.25);
        }

        double shortTerm() const
        {
            if (subs.size() < 30) return -200.0;
            double s = 0;
            for (auto p : subs) s += p;
            return toLufs (s / (double) subs.size());
        }

        /** Gated integrated loudness over everything since the last reset. */
        double integrated() const
        {
            double sum = 0;
            int count = 0;
            for (auto p : blockPowers) if (toLufs (p) > -70.0) { sum += p; ++count; }
            if (count == 0) return -200.0;
            const double relGate = toLufs (sum / count) - 10.0;
            sum = 0;
            count = 0;
            for (auto p : blockPowers) if (toLufs (p) > -70.0 && toLufs (p) > relGate) { sum += p; ++count; }
            return count > 0 ? toLufs (sum / count) : -200.0;
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
        std::vector<double> subs, blockPowers;
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

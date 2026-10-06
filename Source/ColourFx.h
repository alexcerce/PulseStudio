#pragma once

#include "ModFx.h"

/** Colour effects: Frequency Shifter (and ring modulator), Redux (sample-rate and bit reduction) and Exciter. */
namespace pulse
{
    /** Splits a signal into two copies a quarter of a cycle apart at every frequency (two chains of all-passes). */
    struct QuarterCycleSplitter
    {
        void reset() { for (auto& s : a) s = {}; for (auto& s : b) s = {}; held = 0.0f; }
        inline void process (float x, float& inPhase, float& quarter) noexcept
        {
            static constexpr float ca[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static constexpr float cb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            float u = x, v = x;
            for (int i = 0; i < 4; ++i) { u = a[i].run (u, ca[i] * ca[i]); v = b[i].run (v, cb[i] * cb[i]); }
            inPhase = held;   // the first chain comes out one sample later
            held = u;
            quarter = v;
        }
    private:
        struct Section
        {
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            inline float run (float x, float c) noexcept { const float y = c * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
        };
        Section a[4], b[4];
        float held = 0.0f;
    };

    //==============================================================================
    /** Moves every frequency up or down by the same number of Hz (so harmonics stop lining up: bells, metal,
        slow phasing at a few Hz), or multiplies the sound by a sine (ring modulation: both directions at once). */
    class FreqShifter : public ModEffect
    {
    public:
        enum { pShift, pFine, pMode, pLfoAmount, pLfoRate, pSpread, pFeedback, pDelay, pMix };
        static inline const char* xmlTypeName = "pulseShifter";
        static const char* getPluginName() { return "Frequency Shifter"; }
        static inline const StringArray modeNames { "Shift", "Ring mod" };

        explicit FreqShifter (te::PluginCreationInfo info) : ModEffect (info)
        {
            auto hzSigned = [] (float v) { return (v > 0.05f ? "+" : "") + String (v, std::abs (v) < 100.0f ? 1 : 0) + " Hz"; };
            add ("shift", "Shift", { -2000.0f, 2000.0f }, 100.0f, hzSigned);
            add ("fine", "Fine", { -20.0f, 20.0f }, 0.0f, hzSigned);
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("lfoamount", "LFO Amount", skewed (0.0f, 500.0f, 50.0f), 0.0f, [] (float v) { return String (v, 1) + " Hz"; });
            add ("lforate", "LFO Rate", skewed (0.02f, 20.0f, 1.0f), 0.5f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("spread", "Stereo", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("feedback", "Feedback", { 0.0f, 0.9f }, 0.0f, fmt::pct);
            add ("delay", "Feedback Delay", skewed (1.0f, 300.0f, 40.0f), 20.0f, fmt::ms);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override       { return "Frequency Shifter"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Shift", { pShift, pFine, pMode } }, { "Movement", { pLfoAmount, pLfoRate, pSpread } }, { "Spiral", { pFeedback, pDelay, pMix } } };
        }

        /** The shift in Hz without the LFO. */
        float shiftHz() const { return get (pShift) + get (pFine); }
        std::atomic<float> shiftNow { 0.0f };   // with the LFO, for the display

    protected:
        void prepare() override
        {
            for (auto& s : split) s.reset();
            for (auto& d : lines) d.setMax ((int) (sampleRate * 0.31) + 16);
            carrier[0] = carrier[1] = 0.0;
            last[0] = last[1] = 0.0f;
            sShift.reset(); sMix.reset();
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const bool ring = getInt (pMode) == 1;
            const float lfoAmt = get (pLfoAmount), spread = get (pSpread), feedback = get (pFeedback);
            const float fbDelay = jmax (1.0f, get (pDelay) * 0.001f * (float) sampleRate);
            const float a = std::exp (-1.0f / (float) (0.01 * sampleRate));
            double phase, inc;
            lfoBegin (get (pLfoRate), 0, fc, phase, inc);
            float* d[2] = { l, r };
            for (int i = 0; i < n; ++i)
            {
                const float base = sShift.next (shiftHz(), a), mix = sMix.next (get (pMix), a);
                const float hz = base + lfoAmt * (float) std::sin (phase * MathConstants<double>::twoPi);
                for (int ch = 0; ch < 2; ++ch)
                {
                    // at full Stereo the right side moves the opposite way
                    const float chHz = ch == 1 ? hz * (1.0f - 2.0f * spread) : hz;
                    carrier[ch] += chHz / sampleRate;
                    carrier[ch] -= std::floor (carrier[ch]);
                    const float c = (float) std::cos (carrier[ch] * MathConstants<double>::twoPi), s = (float) std::sin (carrier[ch] * MathConstants<double>::twoPi);
                    const float dry = d[ch][i];
                    const float in = dry + feedback * lines[(size_t) ch].read (fbDelay);
                    float wet;
                    if (ring) wet = in * c;
                    else
                    {
                        float inPhase, quarter;
                        split[(size_t) ch].process (in, inPhase, quarter);
                        wet = inPhase * c + quarter * s;
                    }
                    lines[(size_t) ch].push (wet);
                    last[ch] = wet;
                    d[ch][i] = dry * (1.0f - mix) + wet * mix;
                }
                phase += inc;
                if (i == n - 1) shiftNow.store (hz);
            }
            lfoEnd (phase);
        }

    private:
        std::array<QuarterCycleSplitter, 2> split;
        std::array<DelayLine, 2> lines;
        double carrier[2] {};
        float last[2] {};
        Smooth sShift, sMix;
    };

    //==============================================================================
    /** Redux: plays the sound at a lower sample rate (each value held for a while) and with fewer bits
        (fewer steps of loudness): old samplers, consoles and telephones. */
    class Redux : public PulseEffect
    {
    public:
        enum { pRate, pJitter, pBits, pSoft, pTone, pMix };
        static inline const char* xmlTypeName = "pulseRedux";
        static const char* getPluginName() { return "Redux"; }

        explicit Redux (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("rate", "Sample Rate", skewed (200.0f, 48000.0f, 6000.0f), 12000.0f, fmt::hz);
            add ("jitter", "Jitter", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("bits", "Bits", { 1.0f, 16.0f }, 12.0f, [] (float v) { return String (v, 1) + " bit"; });
            add ("soft", "Soft", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("tone", "Tone", skewed (300.0f, 20000.0f, 4000.0f), 20000.0f, [] (float v) { return v >= 19900.0f ? String ("Off") : fmt::hz (v); });
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override       { return "Redux"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Sample rate", { pRate, pJitter, pSoft } }, { "Bits", { pBits } }, { "Output", { pTone, pMix } } }; }

        /** A value (-1..1) after the bit reduction. */
        static float quantise (float x, float bits) noexcept
        {
            const float levels = std::pow (2.0f, bits - 1.0f);
            return std::round (jlimit (-1.0f, 1.0f, x) * levels) / levels;
        }

    protected:
        void prepare() override
        {
            count = 0.0; hold[0] = hold[1] = 0.0f; nextEvery = 1.0;
            for (auto& b : pre) b.reset();
            for (auto& b : post) b.reset();
            lastPre = lastPost = -1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float rate = jmin (get (pRate), (float) sampleRate), bits = get (pBits), jitter = get (pJitter), mix = get (pMix);
            const bool soft = getInt (pSoft) > 0, tone = get (pTone) < 19900.0f;
            const double every = sampleRate / rate;   // input samples per held value
            // "Soft" takes out what the lower rate cannot carry before it is sampled, so less of it folds back as grit
            if (soft && std::abs (rate - lastPre) > 1.0f) { for (auto& b : pre) b.set (Biquad::Type::highCut, sampleRate, rate * 0.45, 0.707, 0.0); lastPre = rate; }
            if (tone && std::abs (get (pTone) - lastPost) > 0.5f) { for (auto& b : post) b.set (Biquad::Type::highCut, sampleRate, get (pTone), 0.707, 0.0); lastPost = get (pTone); }
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                if (soft) { a = pre[0].process (a, 0); b = pre[1].process (b, 0); }
                count += 1.0;
                if (count >= nextEvery)
                {
                    count -= nextEvery;
                    if (count >= nextEvery) count = 0.0;
                    nextEvery = every * (1.0 + jitter * (random.nextFloat() * 1.5f - 0.5f));
                    hold[0] = quantise (a, bits);
                    hold[1] = quantise (b, bits);
                }
                float wl = hold[0], wr = hold[1];
                if (tone) { wl = post[0].process (wl, 0); wr = post[1].process (wr, 0); }
                l[i] = l[i] * (1.0f - mix) + wl * mix;   // (exactly the held value at 100%)
                r[i] = r[i] * (1.0f - mix) + wr * mix;
            }
        }

    private:
        double count = 0.0, nextEvery = 1.0;
        float hold[2] {}, lastPre = -1.0f, lastPost = -1.0f;
        std::array<Biquad, 2> pre, post;
        Random random { 31 };
    };

    //==============================================================================
    /** Exciter: makes new harmonics from the top of the sound and adds a little of them back, for presence
        and air that an EQ cannot give (it can only turn up what is already there). */
    class Exciter : public PulseEffect
    {
    public:
        enum { pFreq, pDrive, pColour, pAmount, pOutput };
        static inline const char* xmlTypeName = "pulseExciter";
        static const char* getPluginName() { return "Exciter"; }

        explicit Exciter (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("freq", "Frequency", skewed (500.0f, 12000.0f, 3000.0f), 3000.0f, fmt::hz);
            add ("drive", "Drive", { 0.0f, 30.0f }, 12.0f, fmt::db);
            add ("colour", "Colour", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v < -0.05f ? "Odd " + String (roundToInt (-v * 100)) + "%" : v > 0.05f ? "Even " + String (roundToInt (v * 100)) + "%" : String ("Both"); });
            add ("amount", "Amount", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("output", "Output", { -12.0f, 12.0f }, 0.0f, fmt::db);
        }

        String getName() const override       { return "Exciter"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Harmonics", { pFreq, pDrive, pColour } }, { "Output", { pAmount, pOutput } } }; }

        /** What the harmonic maker does to a value: odd harmonics (even-handed clipping), even ones (lopsided), or both. */
        static float shape (float x, float colour) noexcept
        {
            const float odd = std::tanh (x), even = std::tanh (std::abs (x)) * 0.8f;
            const float e = 0.5f + 0.5f * colour;
            return odd * (1.0f - e) + even * e;
        }

        std::atomic<float> addedDb { -100.0f };   // how loud the added harmonics are (for the display)

    protected:
        void prepare() override
        {
            for (auto& f : hpIn) f.reset();
            for (auto& f : hpOut) f.reset();
            lastFreq = -1.0f;
            sAmount.v = get (pAmount);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float freq = get (pFreq), colour = get (pColour), out = Decibels::decibelsToGain (get (pOutput));
            const float drive = Decibels::decibelsToGain (get (pDrive));
            // more drive makes more harmonics, not just a louder copy: the result is levelled again afterwards
            const float level = 1.0f / std::sqrt (drive);
            if (std::abs (freq - lastFreq) > 1.0f)
            {
                for (auto& f : hpIn) f.set (Biquad::Type::lowCut, sampleRate, freq, 0.707, 0.0);
                for (auto& f : hpOut) f.set (Biquad::Type::lowCut, sampleRate, freq, 0.707, 0.0);   // also removes what the lopsided shape adds at 0 Hz
                lastFreq = freq;
            }
            const float a = std::exp (-1.0f / (float) (0.01 * sampleRate));
            float peak = 0.0f;
            float* d[2] = { l, r };
            for (int i = 0; i < n; ++i)
            {
                sAmount.v = get (pAmount) + (sAmount.v - get (pAmount)) * a;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float top = hpIn[(size_t) ch].process (d[ch][i], 0);
                    const float made = hpOut[(size_t) ch].process (shape (top * drive, colour), 0) * level;
                    peak = jmax (peak, std::abs (made) * sAmount.v);
                    d[ch][i] = (d[ch][i] + made * sAmount.v) * out;
                }
            }
            addedDb = jmax (Decibels::gainToDecibels (peak, -100.0f), addedDb.load() - 1.0f);
        }

    private:
        std::array<Biquad, 2> hpIn, hpOut;
        float lastFreq = -1.0f;
        struct { float v = 0.3f; } sAmount;
    };
}

#pragma once

#include "Common.h"

/** Pulse's own built-in devices, implemented as Tracktion plugins:
    Glue Compressor, Saturator, EQ (6 band, with analyser) and Auto Filter.
    The synths (Wavetable, Simpler) live in PulseSynths.h. */
namespace pulse
{
    //==============================================================================
    // Value formatting
    namespace fmt
    {
        inline String hz (float v)   { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }
        inline String db (float v)   { return String (v, 1) + " dB"; }
        inline String ms (float v)   { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; }
        inline String pct (float v)  { return String (roundToInt (v * 100.0f)) + "%"; }
        inline String st (float v)   { return String (roundToInt (v)) + " st"; }
        inline String ct (float v)   { return String (roundToInt (v)) + " ct"; }
        inline String num (float v)  { return String (v, 2); }
        inline std::function<String (float)> choice (StringArray names)
        {
            return [names] (float v) { return names[jlimit (0, names.size() - 1, roundToInt (v))]; };
        }
    }

    /** A range with its centre value placed at the middle of the knob. */
    inline NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    inline NormalisableRange<float> steps (int count) { return { 0.0f, (float) (count - 1), 1.0f }; }

    //==============================================================================
    /** Base for Pulse devices: parameters backed by the plugin state, plus card layout hints. */
    class PulsePlugin : public te::Plugin
    {
    public:
        using te::Plugin::Plugin;

        ~PulsePlugin() override
        {
            notifyListenersOfDeletion();
            for (auto& p : params)
                p.param->detachFromCurrentValue();
        }

        /** Groups of parameters shown as labelled knob sections on the device card. */
        struct Section { String name; std::vector<int> params; };
        virtual std::vector<Section> getSections() const = 0;

        /** Width of the visual display drawn at the left of the card (0 = none). */
        virtual int getDisplayWidth() const { return 0; }

        te::AutomatableParameter* param (int i) const   { return params[(size_t) i].param.get(); }
        float get (int i) const                          { return params[(size_t) i].param->getCurrentValue(); }
        int getInt (int i) const                         { return roundToInt (get (i)); }
        void set (int i, float v)                        { params[(size_t) i].param->setParameter (v, sendNotification); }
        int numParams() const                            { return (int) params.size(); }

        String getShortName (int) override               { return getName().substring (0, 6); }
        String getSelectableDescription() override       { return getName(); }

    protected:
        int add (const String& id, const String& name, NormalisableRange<float> range, float def, std::function<String (float)> toText)
        {
            auto cv = std::make_unique<CachedValue<float>>();
            cv->referTo (state, Identifier (id), getUndoManager(), def);
            auto p = addParam (id, name, range, toText, [] (const String& s) { return s.getFloatValue(); });
            p->attachToCurrentValue (*cv);
            params.push_back ({ p, std::move (cv) });
            return (int) params.size() - 1;
        }

        struct P { te::AutomatableParameter::Ptr param; std::unique_ptr<CachedValue<float>> value; };
        std::vector<P> params;
        double sampleRate = 44100.0;
        int blockSize = 512;
    };

    /** Shared stereo effect plumbing. */
    class PulseEffect : public PulsePlugin
    {
    public:
        using PulsePlugin::PulsePlugin;
        int getNumOutputChannelsGivenInputs (int) override  { return 2; }
        BusLayout getBusses() const override                 { return BusLayout::singleStereoInOut(); }
        void deinitialise() override {}

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            sampleRate = info.sampleRate;
            blockSize = info.blockSizeSamples;
            prepare();
        }

        void applyToBuffer (const te::PluginRenderContext& fc) override
        {
            if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
                return;

            auto& b = *fc.destBuffer;
            if (b.getNumChannels() < 2)
                return;

            // sidechain input arrives on channels 3 / 4 (when a source is chosen)
            sidechain = nullptr;
            if (wantsSidechain() && b.getNumChannels() > 2 && getSidechainSourceID().isValid())
            {
                scBuffer.setSize (2, fc.bufferNumSamples, false, false, true);
                scBuffer.copyFrom (0, 0, b, 2, fc.bufferStartSample, fc.bufferNumSamples);
                scBuffer.copyFrom (1, 0, b, b.getNumChannels() > 3 ? 3 : 2, fc.bufferStartSample, fc.bufferNumSamples);
                sidechain = &scBuffer;
            }

            te::clearChannels (b, 2, -1, fc.bufferStartSample, fc.bufferNumSamples);
            process (b.getWritePointer (0, fc.bufferStartSample), b.getWritePointer (1, fc.bufferStartSample), fc.bufferNumSamples, fc);
            te::sanitiseValues (b, fc.bufferStartSample, fc.bufferNumSamples, 4.0f);
        }

        /** Effects that can be keyed from another track declare two extra inputs. */
        virtual bool wantsSidechain() const { return false; }

        void getChannelNames (StringArray* ins, StringArray* outs) override
        {
            te::Plugin::getChannelNames (ins, outs);
            if (ins != nullptr && wantsSidechain())
                ins->addArray ({ "Sidechain Left", "Sidechain Right" });
        }

    protected:
        /** The sidechain signal for this block (nullptr when none is routed in). */
        const AudioBuffer<float>* sidechain = nullptr;
        AudioBuffer<float> scBuffer;

        virtual void prepare() = 0;
        virtual void process (float* l, float* r, int n, const te::PluginRenderContext&) = 0;
    };

    //==============================================================================
    // DSP building blocks
    /** RBJ-cookbook biquad (transposed direct form II). */
    struct Biquad
    {
        enum class Type { lowCut, lowShelf, bell, notch, highShelf, highCut, bandPass };
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] {}, z2[2] {};

        void set (Type t, double sr, double f, double q, double gainDb)
        {
            f = jlimit (10.0, sr * 0.49, f);
            q = jmax (0.05, q);
            const double A = std::pow (10.0, gainDb / 40.0), w = MathConstants<double>::twoPi * f / sr;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
            double a0 = 1;
            switch (t)
            {
                case Type::lowCut:   b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::highCut:  b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::bandPass: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::notch:    b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
                case Type::bell:
                    b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
                case Type::lowShelf:
                {
                    const double s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                    a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
                    break;
                }
                case Type::highShelf:
                {
                    const double s = 2 * std::sqrt (A) * alpha;
                    b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                    a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }
            b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        }

        inline float process (float x, int ch) noexcept
        {
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return (float) y;
        }

        void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

        /** Magnitude response at frequency f. */
        double magnitude (double f, double sr) const
        {
            const std::complex<double> z = std::polar (1.0, -MathConstants<double>::twoPi * f / sr);
            auto num = b0 + b1 * z + b2 * z * z;
            auto den = 1.0 + a1 * z + a2 * z * z;
            return std::abs (num / den);
        }
    };

    /** Topology-preserving state variable filter (Zavalishin), stable under fast modulation. */
    struct SVF
    {
        float ic1[2] {}, ic2[2] {};
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;

        void set (float cutoff, float resonance01, double sr)
        {
            g = (float) std::tan (MathConstants<double>::pi * jlimit (16.0, sr * 0.45, (double) cutoff) / sr);
            k = 2.0f - 1.96f * jlimit (0.0f, 1.0f, resonance01);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        /** mode: 0 low, 1 band, 2 high, 3 notch. */
        inline float process (float x, int ch, int mode) noexcept
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            switch (mode)
            {
                case 0:  return v2;
                case 1:  return v1;
                case 2:  return x - k * v1 - v2;
                default: return x - k * v1;
            }
        }

        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
    };

    /** Mono FIFO the UI reads for spectrum / scope displays. */
    struct AnalyserFifo
    {
        static constexpr int size = 8192;
        std::array<float, size> data {};
        std::atomic<int> writePos { 0 };

        void push (const float* l, const float* r, int n) noexcept
        {
            int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                data[(size_t) w] = 0.5f * (l[i] + r[i]);
                w = (w + 1) & (size - 1);
            }
            writePos.store (w, std::memory_order_release);
        }

        /** Copies the newest n samples (n <= size). */
        void read (float* dest, int n) const noexcept
        {
            int w = writePos.load (std::memory_order_acquire);
            int start = (w - n + size) & (size - 1);
            for (int i = 0; i < n; ++i)
                dest[i] = data[(size_t) ((start + i) & (size - 1))];
        }
    };

    //==============================================================================
    /** SSL-style bus compressor: stereo linked, soft knee, stepped attack/release with auto release. */
    class GlueCompressor : public PulseEffect
    {
    public:
        enum { pThreshold, pRatio, pAttack, pRelease, pMakeup, pMix, pSoftClip, pRange, pScFilter };
        static inline const char* xmlTypeName = "pulseGlue";
        static const char* getPluginName() { return "Glue Compressor"; }

        static constexpr float attacks[]  = { 0.01f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        static constexpr float releases[] = { 0.1f, 0.2f, 0.4f, 0.6f, 0.8f, 1.2f, -1.0f };
        static constexpr float ratios[]   = { 2.0f, 4.0f, 10.0f };

        explicit GlueCompressor (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("threshold", "Threshold", { -40.0f, 0.0f }, -10.0f, fmt::db);
            add ("ratio", "Ratio", steps (3), 1.0f, fmt::choice ({ "2:1", "4:1", "10:1" }));
            add ("attack", "Attack", steps (7), 2.0f, fmt::choice ({ "0.01 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }));
            add ("release", "Release", steps (7), 6.0f, fmt::choice ({ "0.1 s", "0.2 s", "0.4 s", "0.6 s", "0.8 s", "1.2 s", "Auto" }));
            add ("makeup", "Makeup", { 0.0f, 20.0f }, 0.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("softclip", "Soft Clip", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("range", "Range", { 1.0f, 40.0f }, 40.0f, [] (float v) { return String (v, 1) + " dB"; });
            add ("schpf", "SC Filter", skewed (20.0f, 500.0f, 100.0f), 20.0f, [] (float v) { return v <= 20.5f ? String ("Off") : fmt::hz (v); });
        }

        bool wantsSidechain() const override { return true; }

        String getName() const override     { return "Glue Compressor"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 150; }

        std::vector<Section> getSections() const override
        {
            return { { "Compressor", { pThreshold, pRatio, pAttack, pRelease } }, { "Output", { pMakeup, pRange, pMix, pSoftClip } }, { "Sidechain", { pScFilter } } };
        }

        std::atomic<float> gainReductionDb { 0.0f };

    protected:
        void prepare() override { envDb = 0.0f; slowDb = 0.0f; }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float thr = get (pThreshold), ratio = ratios[jlimit (0, 2, getInt (pRatio))];
            const float atk = attacks[jlimit (0, 6, getInt (pAttack))] * 0.001f;
            const float relSel = releases[jlimit (0, 6, getInt (pRelease))];
            const bool autoRelease = relSel < 0.0f;
            const float makeup = Decibels::decibelsToGain (get (pMakeup)), mix = get (pMix), range = get (pRange);
            const bool clip = getInt (pSoftClip) > 0;
            const float knee = 6.0f;

            auto coeff = [this] (float seconds) { return std::exp (-1.0f / (float) (jmax (1.0e-5f, seconds) * sampleRate)); };
            const float scF = get (pScFilter);
            const bool scFilter = scF > 20.5f;
            if (scFilter && std::abs (scF - lastScF) > 0.5f)
            {
                for (auto& bq : scHp) bq.set (Biquad::Type::lowCut, sampleRate, scF, 0.707, 0.0);
                lastScF = scF;
            }
            const float* keyL = sidechain != nullptr ? sidechain->getReadPointer (0) : l;
            const float* keyR = sidechain != nullptr ? sidechain->getReadPointer (1) : r;
            const float aA = coeff (atk), aR = coeff (autoRelease ? 0.15f : relSel), aSlow = coeff (1.2f);
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float dryL = l[i], dryR = r[i];
                float kl = keyL[i], kr = keyR[i];
                if (scFilter) { kl = scHp[0].process (kl, 0); kr = scHp[1].process (kr, 0); }   // the detector ignores the lows
                const float level = jmax (std::abs (kl), std::abs (kr));
                const float inDb = Decibels::gainToDecibels (level, -120.0f);

                // soft-knee gain computer
                float over = inDb - thr, target = 0.0f;
                if (2.0f * over < -knee)        target = 0.0f;
                else if (2.0f * over > knee)    target = over * (1.0f - 1.0f / ratio);
                else                            target = (1.0f - 1.0f / ratio) * square (over + knee * 0.5f) / (2.0f * knee);
                target = jmin (target, range);

                envDb = target > envDb ? aA * envDb + (1.0f - aA) * target : aR * envDb + (1.0f - aR) * target;
                float gr = envDb;
                if (autoRelease)   // program dependent: a slow envelope holds back long-term reduction
                {
                    slowDb = target > slowDb ? slowDb + (1.0f - aSlow) * 4.0f * (target - slowDb) : aSlow * slowDb + (1.0f - aSlow) * target;
                    gr = jmax (envDb, slowDb * 0.6f);
                }
                maxGr = jmax (maxGr, gr);

                const float g = Decibels::decibelsToGain (-gr) * makeup;
                float wl = dryL * g, wr = dryR * g;
                if (clip) { wl = std::tanh (wl); wr = std::tanh (wr); }
                l[i] = dryL + mix * (wl - dryL);
                r[i] = dryR + mix * (wr - dryR);
            }
            gainReductionDb.store (maxGr);
        }

    private:
        float envDb = 0.0f, slowDb = 0.0f, lastScF = -1.0f;
        std::array<Biquad, 2> scHp;
    };

    //==============================================================================
    /** Waveshaping saturator with 4x oversampling, tone tilt and DC blocking. */
    class Saturator : public PulseEffect
    {
    public:
        enum { pDrive, pType, pTone, pOutput, pMix, pBias };
        static inline const char* xmlTypeName = "pulseSaturator";
        static const char* getPluginName() { return "Saturator"; }
        static inline const StringArray typeNames { "Analog", "Tape", "Tube", "Hard clip", "Foldback", "Sine fold", "Bit crush" };

        explicit Saturator (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("drive", "Drive", { 0.0f, 36.0f }, 6.0f, fmt::db);
            add ("type", "Type", steps (typeNames.size()), 0.0f, fmt::choice (typeNames));
            add ("tone", "Tone", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v < -0.01f ? "Dark " + fmt::pct (-v) : v > 0.01f ? "Bright " + fmt::pct (v) : String ("Flat"); });
            add ("output", "Output", { -24.0f, 12.0f }, 0.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("bias", "Bias", { -1.0f, 1.0f }, 0.0f, fmt::num);
        }

        String getName() const override     { return "Saturator"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 110; }

        std::vector<Section> getSections() const override
        {
            return { { "Shape", { pDrive, pType, pBias } }, { "Output", { pTone, pOutput, pMix } } };
        }

        /** The transfer curve (used by the display too). */
        static float shape (int type, float x, float bias) noexcept
        {
            x += bias * 0.5f;
            switch (type)
            {
                case 0:  return std::tanh (x);
                case 1:  return x / (1.0f + std::abs (x)) * 1.4f * (1.0f - 0.15f * std::tanh (x * x * 0.1f));
                case 2:  return x >= 0.0f ? std::tanh (x * 1.2f) : std::tanh (x * 0.7f) * 0.85f;   // asymmetric
                case 3:  return jlimit (-1.0f, 1.0f, x);
                case 4:  { float y = x; for (int i = 0; i < 8 && std::abs (y) > 1.0f; ++i) y = (y > 1.0f ? 2.0f : -2.0f) - y; return y; }
                case 5:  return std::sin (x * MathConstants<float>::halfPi);
                default: { const float steps = 16.0f; return std::round (jlimit (-1.0f, 1.0f, std::tanh (x)) * steps) / steps; }
            }
        }

        float shaped (float x) const noexcept
        {
            const float g = Decibels::decibelsToGain (get (pDrive));
            const int t = getInt (pType);
            const float bias = get (pBias);
            return (shape (t, x * g, bias) - shape (t, 0.0f, bias)) / jmax (1.0f, std::sqrt (g) * 0.75f);
        }

    protected:
        void prepare() override
        {
            oversampler = std::make_unique<dsp::Oversampling<float>> (2, 2, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
            oversampler->initProcessing ((size_t) jmax (16, blockSize));
            dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
            lp[0] = lp[1] = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float g = Decibels::decibelsToGain (get (pDrive));
            const int type = getInt (pType);
            const float bias = get (pBias), mix = get (pMix), tone = get (pTone);
            const float out = Decibels::decibelsToGain (get (pOutput)) / jmax (1.0f, std::sqrt (g) * 0.75f);
            const float offset = shape (type, 0.0f, bias);
            const float dcR = 1.0f - (float) (MathConstants<double>::twoPi * 10.0 / sampleRate);
            const float lpA = (float) std::exp (-MathConstants<double>::twoPi * 1200.0 / sampleRate);

            for (int start = 0; start < n; start += blockSize)
            {
                const int num = jmin (blockSize, n - start);
                HeapBlock<float> dry (num * 2);
                std::copy (l + start, l + start + num, dry.get());
                std::copy (r + start, r + start + num, dry.get() + num);

                float* chans[] = { l + start, r + start };
                dsp::AudioBlock<float> block (chans, 2, (size_t) num);
                auto up = oversampler->processSamplesUp (block);
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto* d = up.getChannelPointer (ch);
                    for (size_t i = 0; i < up.getNumSamples(); ++i)
                        d[i] = shape (type, d[i] * g, bias) - offset;
                }
                oversampler->processSamplesDown (block);

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = chans[ch];
                    auto* dr = dry.get() + ch * num;
                    for (int i = 0; i < num; ++i)
                    {
                        // DC blocker (asymmetric shapes add DC)
                        float y = d[i] - dcX[ch] + dcR * dcY[ch];
                        dcX[ch] = d[i];
                        dcY[ch] = y;
                        // tilt: shift balance between low and high bands around ~1.2 kHz
                        lp[ch] = lpA * lp[ch] + (1.0f - lpA) * y;
                        const float hi = y - lp[ch];
                        y = lp[ch] * (1.0f - jmax (0.0f, tone) * 0.7f) + hi * (1.0f + jmin (0.0f, tone) * 0.9f + jmax (0.0f, tone) * 0.8f);
                        d[i] = dr[i] + mix * (y * out - dr[i]);
                    }
                }
            }
        }

    private:
        std::unique_ptr<dsp::Oversampling<float>> oversampler;
        float dcX[2] {}, dcY[2] {}, lp[2] {};
    };

    //==============================================================================
    /** Six-band parametric EQ with a response display and spectrum analyser. */
    class PulseEQ : public PulseEffect
    {
    public:
        static constexpr int numBands = 6;
        enum { pOn, pType, pFreq, pGain, pQ, perBand };
        static inline const char* xmlTypeName = "pulseEQ";
        static const char* getPluginName() { return "EQ Six"; }
        static inline const StringArray typeNames { "Low cut", "Low shelf", "Bell", "Notch", "High shelf", "High cut" };

        explicit PulseEQ (te::PluginCreationInfo info) : PulseEffect (info)
        {
            const float freqs[] = { 40.0f, 120.0f, 400.0f, 1500.0f, 5000.0f, 16000.0f };
            const int types[] = { 0, 1, 2, 2, 4, 5 };
            for (int b = 0; b < numBands; ++b)
            {
                auto n = String (b + 1);
                add ("b" + n + "on", "Band " + n + " On", steps (2), (b == 0 || b == 5) ? 0.0f : 1.0f, fmt::choice ({ "Off", "On" }));
                add ("b" + n + "type", "Band " + n + " Type", steps (typeNames.size()), (float) types[b], fmt::choice (typeNames));
                add ("b" + n + "freq", "Band " + n + " Freq", skewed (20.0f, 20000.0f, 1000.0f), freqs[b], fmt::hz);
                add ("b" + n + "gain", "Band " + n + " Gain", { -18.0f, 18.0f }, 0.0f, fmt::db);
                add ("b" + n + "q", "Band " + n + " Q", skewed (0.1f, 18.0f, 1.0f), 0.71f, fmt::num);
            }
            outputIndex = add ("output", "Output", { -18.0f, 18.0f }, 0.0f, fmt::db);
        }

        String getName() const override      { return "EQ Six"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 470; }
        std::vector<Section> getSections() const override { return { { "Output", { outputIndex } } }; }

        static int idx (int band, int which) { return band * perBand + which; }
        static Biquad::Type biquadType (int t)
        {
            const Biquad::Type map[] = { Biquad::Type::lowCut, Biquad::Type::lowShelf, Biquad::Type::bell,
                                         Biquad::Type::notch, Biquad::Type::highShelf, Biquad::Type::highCut };
            return map[jlimit (0, 5, t)];
        }
        static bool usesGain (int t) { return t == 1 || t == 2 || t == 4; }

        /** Builds a band's filter stages (cuts are 24 dB/oct). Used by the display too. */
        void designBand (int b, double sr, Biquad& s1, Biquad& s2, bool& twoStages) const
        {
            const int t = getInt (idx (b, pType));
            const double f = get (idx (b, pFreq)), q = get (idx (b, pQ)), g = get (idx (b, pGain));
            s1.set (biquadType (t), sr, f, q, g);
            twoStages = (t == 0 || t == 5);
            if (twoStages)
                s2.set (biquadType (t), sr, f, 0.707, 0.0);
        }

        bool isBandOn (int b) const { return getInt (idx (b, pOn)) > 0; }

        /** Total response in dB at frequency f (for the display). */
        double responseDb (double f) const
        {
            double mag = 1.0;
            for (int b = 0; b < numBands; ++b)
            {
                if (! isBandOn (b)) continue;
                Biquad s1, s2; bool two = false;
                designBand (b, 48000.0, s1, s2, two);
                mag *= s1.magnitude (f, 48000.0);
                if (two) mag *= s2.magnitude (f, 48000.0);
            }
            return Decibels::gainToDecibels (mag, -60.0) + get (outputIndex);
        }

        AnalyserFifo analyser;

    protected:
        void prepare() override
        {
            for (auto& s : stages) s.reset();
            lastHash = -1.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            // redesign the filters only when a parameter moved
            double hash = 0;
            for (int i = 0; i < numParams(); ++i) hash = hash * 1.000123 + get (i) * (i + 1);
            if (hash != lastHash)
            {
                lastHash = hash;
                for (int b = 0; b < numBands; ++b)
                {
                    active[(size_t) b] = isBandOn (b);
                    auto prevType = types[(size_t) b];
                    designBand (b, sampleRate, stages[(size_t) b * 2], stages[(size_t) b * 2 + 1], twoStage[(size_t) b]);
                    types[(size_t) b] = getInt (idx (b, pType));
                    if (prevType != types[(size_t) b]) { stages[(size_t) b * 2].reset(); stages[(size_t) b * 2 + 1].reset(); }
                }
            }

            const float out = Decibels::decibelsToGain (get (outputIndex));
            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i], r[i] };
                for (int b = 0; b < numBands; ++b)
                {
                    if (! active[(size_t) b]) continue;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        x[ch] = stages[(size_t) b * 2].process (x[ch], ch);
                        if (twoStage[(size_t) b]) x[ch] = stages[(size_t) b * 2 + 1].process (x[ch], ch);
                    }
                }
                l[i] = x[0] * out;
                r[i] = x[1] * out;
            }
            analyser.push (l, r, n);
        }

    private:
        int outputIndex = 0;
        std::array<Biquad, numBands * 2> stages;
        std::array<bool, numBands> active {}, twoStage {};
        std::array<int, numBands> types {};
        double lastHash = -1.0;
    };

    //==============================================================================
    /** Filter with LFO (free or tempo-synced) and envelope follower modulation. */
    class AutoFilter : public PulseEffect,
                       private Timer
    {
    public:
        enum { pType, pCutoff, pReso, pDrive, pLfoAmount, pLfoRate, pLfoSync, pLfoShape, pPhase, pEnvAmount, pAttack, pRelease, pMix };
        static inline const char* xmlTypeName = "pulseAutoFilter";
        static const char* getPluginName() { return "Auto Filter"; }
        static inline const StringArray typeNames { "Low pass", "Band pass", "High pass", "Notch", "Low pass 24" };
        static inline const StringArray syncNames { "Free (Hz)", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        static constexpr double syncBeats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        static inline const StringArray shapeNames { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random" };

        explicit AutoFilter (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("type", "Type", steps (typeNames.size()), 0.0f, fmt::choice (typeNames));
            add ("cutoff", "Frequency", skewed (20.0f, 20000.0f, 1000.0f), 1200.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("drive", "Drive", { 0.0f, 24.0f }, 0.0f, fmt::db);
            add ("lfoamount", "LFO Amount", { 0.0f, 1.0f }, 0.4f, fmt::pct);
            add ("lforate", "LFO Rate", skewed (0.02f, 20.0f, 1.0f), 1.0f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("lfosync", "LFO Sync", steps (syncNames.size()), 3.0f, fmt::choice (syncNames));
            add ("lfoshape", "LFO Shape", steps (shapeNames.size()), 0.0f, fmt::choice (shapeNames));
            add ("phase", "Stereo Phase", { 0.0f, 180.0f }, 0.0f, [] (float v) { return String (roundToInt (v)) + String::charToString (0x00b0); });
            add ("envamount", "Env Amount", { -1.0f, 1.0f }, 0.0f, fmt::pct);
            add ("envattack", "Env Attack", skewed (0.1f, 200.0f, 10.0f), 5.0f, fmt::ms);
            add ("envrelease", "Env Release", skewed (5.0f, 2000.0f, 200.0f), 150.0f, fmt::ms);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            bpm.store (edit.tempoSequence.getTempo (0)->getBpm());
            startTimer (250);
        }

        String getName() const override      { return "Auto Filter"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 170; }

        std::vector<Section> getSections() const override
        {
            return { { "Filter", { pType, pCutoff, pReso, pDrive } },
                     { "LFO", { pLfoAmount, pLfoRate, pLfoSync, pLfoShape, pPhase } },
                     { "Envelope", { pEnvAmount, pAttack, pRelease, pMix } } };
        }

        static float lfoValue (int shape, double phase, float randomHeld) noexcept   // phase 0..1 → -1..1
        {
            phase -= std::floor (phase);
            switch (shape)
            {
                case 0:  return (float) std::sin (phase * MathConstants<double>::twoPi);
                case 1:  return (float) (phase < 0.5 ? phase * 4.0 - 1.0 : 3.0 - phase * 4.0);
                case 2:  return (float) (phase * 2.0 - 1.0);
                case 3:  return (float) (1.0 - phase * 2.0);
                case 4:  return phase < 0.5 ? 1.0f : -1.0f;
                default: return randomHeld;
            }
        }

        std::atomic<float> currentCutoff { 1000.0f };   // for the display

    protected:
        void prepare() override
        {
            for (auto& f : filters)  f.reset();
            for (auto& f : filters2) f.reset();
            env = 0.0f;
            freePhase = 0.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const int type = getInt (pType), shape = getInt (pLfoShape), sync = jlimit (0, 7, getInt (pLfoSync));
            const float cutoff = get (pCutoff), reso = get (pReso), amount = get (pLfoAmount);
            const float drive = Decibels::decibelsToGain (get (pDrive)), mix = get (pMix), envAmt = get (pEnvAmount);
            const double phaseOffset = get (pPhase) / 360.0;
            const float aA = std::exp (-1.0f / (float) (get (pAttack) * 0.001 * sampleRate));
            const float aR = std::exp (-1.0f / (float) (get (pRelease) * 0.001 * sampleRate));
            const int mode = type == 4 ? 0 : type;

            // the LFO follows the song position when synced (so it lines up with the bars)
            double phase = freePhase, phaseInc = get (pLfoRate) / sampleRate;
            if (sync > 0)
            {
                const double beatsPerSecond = bpm.load() / 60.0;
                phaseInc = beatsPerSecond / syncBeats[sync] / sampleRate;
                phase = fc.isPlaying ? fc.editTime.getStart().inSeconds() * beatsPerSecond / syncBeats[sync] : freePhase;
            }

            constexpr int chunk = 16;
            for (int start = 0; start < n; start += chunk)
            {
                const int num = jmin (chunk, n - start);
                // envelope follower on the input
                for (int i = start; i < start + num; ++i)
                {
                    const float lvl = jmax (std::abs (l[i]), std::abs (r[i]));
                    env = lvl > env ? aA * env + (1.0f - aA) * lvl : aR * env + (1.0f - aR) * lvl;
                }

                if (phase - std::floor (phase) < phaseInc * num && shape == 5)
                    randomHeld = random.nextFloat() * 2.0f - 1.0f;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float lfo = lfoValue (shape, phase + (ch == 1 ? phaseOffset : 0.0), randomHeld);
                    const float octaves = lfo * amount * 4.0f + envAmt * jmin (1.0f, env * 2.0f) * 5.0f;
                    const float fc2 = jlimit (20.0f, 20000.0f, cutoff * std::pow (2.0f, octaves));
                    if (ch == 0) currentCutoff.store (fc2);
                    filters[ch].set (fc2, reso, sampleRate);
                    filters2[ch].set (fc2, reso * 0.5f, sampleRate);

                    auto* d = ch == 0 ? l : r;
                    for (int i = start; i < start + num; ++i)
                    {
                        const float dry = d[i];
                        float x = drive > 1.001f ? std::tanh (dry * drive) / std::tanh (drive) : dry;
                        x = filters[ch].process (x, ch, mode);
                        if (type == 4) x = filters2[ch].process (x, ch, 0);
                        d[i] = dry + mix * (x - dry);
                    }
                }
                phase += phaseInc * num;
            }
            freePhase = phase - std::floor (phase);
        }

    private:
        void timerCallback() override { bpm.store (edit.tempoSequence.getTempo (0)->getBpm()); }

        SVF filters[2], filters2[2];
        float env = 0.0f, randomHeld = 0.0f;
        double freePhase = 0.0;
        std::atomic<double> bpm { 120.0 };
        Random random;
    };
}

namespace pulse
{
    //==============================================================================
    /** Look-ahead brickwall limiter for the master: input gain, ceiling, release.
        Reports its look-ahead as latency so the engine compensates. */
    class Limiter : public PulseEffect
    {
    public:
        enum { pGain, pCeiling, pRelease, pAutoRelease };
        static inline const char* xmlTypeName = "pulseLimiter";
        static const char* getPluginName() { return "Limiter"; }
        static constexpr double lookaheadMs = 1.5;

        explicit Limiter (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("gain", "Gain", { 0.0f, 24.0f }, 0.0f, fmt::db);
            add ("ceiling", "Ceiling", { -12.0f, 0.0f }, -0.3f, fmt::db);
            add ("release", "Release", skewed (10.0f, 1000.0f, 100.0f), 100.0f, fmt::ms);
            add ("auto", "Auto Release", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override      { return "Limiter"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 150; }
        double getLatencySeconds() override   { return lookaheadMs / 1000.0; }

        std::vector<Section> getSections() const override { return { { "Limiter", { pGain, pCeiling, pRelease, pAutoRelease } } }; }

        std::atomic<float> gainReductionDb { 0.0f };

    protected:
        void prepare() override
        {
            look = jmax (1, (int) std::round (lookaheadMs * 0.001 * sampleRate));
            delay[0].assign ((size_t) look, 0.0f);
            delay[1].assign ((size_t) look, 0.0f);
            need.assign ((size_t) look, 1.0f);
            pos = 0;
            env = 1.0f;
            slow = 1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float in = Decibels::decibelsToGain (get (pGain));
            const float ceiling = Decibels::decibelsToGain (get (pCeiling));
            const bool autoRel = getInt (pAutoRelease) > 0;
            const float rel = std::exp (-1.0f / (float) (get (pRelease) * 0.001 * sampleRate));
            const float relFast = std::exp (-1.0f / (float) (0.02 * sampleRate)), relSlow = std::exp (-1.0f / (float) (0.8 * sampleRate));
            float minGain = 1.0f;

            for (int i = 0; i < n; ++i)
            {
                const float xl = l[i] * in, xr = r[i] * in;
                const float peak = jmax (std::abs (xl), std::abs (xr));
                need[(size_t) pos] = peak > ceiling ? ceiling / peak : 1.0f;

                // the lowest gain needed anywhere in the look-ahead window
                float target = 1.0f;
                for (auto g : need) target = jmin (target, g);

                if (target < env) env = target;   // instant attack (the window gives the warning)
                else if (autoRel)
                {
                    slow = slow * relSlow + target * (1.0f - relSlow);
                    env = jmin (target, env * (target > slow ? relFast : relSlow) + target * (1.0f - (target > slow ? relFast : relSlow)));
                }
                else env = env * rel + target * (1.0f - rel);

                const float dl = delay[0][(size_t) pos], dr = delay[1][(size_t) pos];
                delay[0][(size_t) pos] = xl;
                delay[1][(size_t) pos] = xr;
                pos = (pos + 1) % look;

                l[i] = jlimit (-ceiling, ceiling, dl * env);   // final safety clip
                r[i] = jlimit (-ceiling, ceiling, dr * env);
                minGain = jmin (minGain, env);
            }
            gainReductionDb.store (-Decibels::gainToDecibels (minGain, -60.0f));
        }

    private:
        std::vector<float> delay[2], need;
        int look = 64, pos = 0;
        float env = 1.0f, slow = 1.0f;
    };
}

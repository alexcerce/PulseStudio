#pragma once

#include "PulseFx2.h"

/** Modulation effects: Pulse Chorus (chorus, ensemble, vibrato), Phaser-Flanger and Auto Pan (auto-pan and tremolo). */
namespace pulse
{
    /** A value that moves to its target without steps (for settings changed while playing). */
    struct ParamSmooth
    {
        float v = 0.0f;
        bool started = false;
        inline float next (float target, float coeff) noexcept
        {
            if (! started) { v = target; started = true; }
            v = target + (v - target) * coeff;
            return v;
        }
        void reset() { started = false; }
    };

    /** What the three have in common: an LFO that runs free or in time with the song, and the tempo to do it. */
    class ModEffect : public PulseEffect, private Timer
    {
    public:
        static inline const StringArray syncNames { "Free (Hz)", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        static constexpr double syncBeats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };

        explicit ModEffect (te::PluginCreationInfo info) : PulseEffect (info)
        {
            bpm.store (edit.tempoSequence.getTempo (0)->getBpm());
            startTimer (250);
        }
        ~ModEffect() override { stopTimer(); }

        /** Where the LFO is in its cycle (0..1), for the displays. */
        std::atomic<float> lfoPhase { 0.0f };
        /** Cycles per second right now (the free rate, or what the sync setting comes to at this tempo). */
        double rateNow (float freeHz, int sync) const { return sync > 0 ? bpm.load() / 60.0 / syncBeats[jlimit (1, 7, sync)] : (double) freeHz; }

    protected:
        /** The phase at the start of this block and how far it moves per sample. Synced, it follows the song position while playing. */
        void lfoBegin (float freeHz, int sync, const te::PluginRenderContext& fc, double& phase, double& inc) const
        {
            inc = rateNow (freeHz, sync) / sampleRate;
            phase = freePhase;
            if (sync > 0 && fc.isPlaying)
                phase = fc.editTime.getStart().inSeconds() * (bpm.load() / 60.0) / syncBeats[jlimit (1, 7, sync)];
        }
        void lfoEnd (double phase) { freePhase = phase - std::floor (phase); lfoPhase.store ((float) freePhase); }

        using Smooth = ParamSmooth;

        double freePhase = 0.0;
        std::atomic<double> bpm { 120.0 };

    private:
        void timerCallback() override { bpm.store (edit.tempoSequence.getTempo (0)->getBpm()); }
    };

    //==============================================================================
    /** Chorus (one moving copy a side), Ensemble (three, a third of a cycle apart) and Vibrato (only the moving copy). */
    class Chorus : public ModEffect
    {
    public:
        enum { pMode, pRate, pAmount, pDelay, pFeedback, pWidth, pLowCut, pHighCut, pMix };
        static inline const char* xmlTypeName = "pulseChorus";
        static const char* getPluginName() { return "Pulse Chorus"; }
        static inline const StringArray modeNames { "Chorus", "Ensemble", "Vibrato" };

        explicit Chorus (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("rate", "Rate", skewed (0.05f, 10.0f, 1.0f), 0.8f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("amount", "Amount", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("delay", "Delay", skewed (1.0f, 30.0f, 8.0f), 7.0f, fmt::ms);
            add ("feedback", "Feedback", { -0.9f, 0.9f }, 0.0f, fmt::pct);
            add ("width", "Width", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 150.0f), 20.0f, [] (float v) { return v <= 21.0f ? String ("Off") : fmt::hz (v); });
            add ("highcut", "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, [] (float v) { return v >= 19900.0f ? String ("Off") : fmt::hz (v); });
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.5f, fmt::pct);
        }

        String getName() const override       { return "Pulse Chorus"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Movement", { pMode, pRate, pAmount, pDelay } }, { "Colour", { pFeedback, pLowCut, pHighCut } }, { "Output", { pWidth, pMix } } };
        }

        /** How many moving copies a side, and how far (ms) each swings either way of Delay. */
        int voices() const        { return getInt (pMode) == 1 ? 3 : 1; }
        float swingMs() const     { return get (pAmount) * jmin (get (pDelay) * 0.85f, 6.0f); }

    protected:
        void prepare() override
        {
            for (auto& d : lines) d.setMax ((int) (sampleRate * 0.05) + 16);
            for (auto& b : lowCut) b.reset();
            for (auto& b : highCut) b.reset();
            lastLow = lastHigh = -1.0f;
            fb[0] = fb[1] = 0.0f;
            sDelay.reset(); sMix.reset(); sSwing.reset();
            voicePhase[0] = 0.0; voicePhase[1] = 1.0 / 3.0; voicePhase[2] = 2.0 / 3.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const int mode = getInt (pMode), nv = mode == 1 ? 3 : 1;
            const float feedback = get (pFeedback), width = get (pWidth);
            const float mixTarget = mode == 2 ? 1.0f : get (pMix), swingTarget = swingMs(), delayTarget = get (pDelay);
            const bool useLow = get (pLowCut) > 21.0f, useHigh = get (pHighCut) < 19900.0f;
            if (std::abs (get (pLowCut) - lastLow) > 0.5f)   { for (auto& b : lowCut) b.set (Biquad::Type::lowCut, sampleRate, get (pLowCut), 0.707, 0.0); lastLow = get (pLowCut); }
            if (std::abs (get (pHighCut) - lastHigh) > 0.5f) { for (auto& b : highCut) b.set (Biquad::Type::highCut, sampleRate, get (pHighCut), 0.707, 0.0); lastHigh = get (pHighCut); }
            const float a = std::exp (-1.0f / (float) (0.02 * sampleRate)), msToSamples = (float) (sampleRate * 0.001);
            const float norm = nv == 3 ? 0.62f : 1.0f;
            double phase, inc;
            lfoBegin (get (pRate), 0, fc, phase, inc);

            for (int i = 0; i < n; ++i)
            {
                const float delay = sDelay.next (delayTarget, a), swing = sSwing.next (swingTarget, a), mix = sMix.next (mixTarget, a);
                const float in[2] = { l[i], r[i] };
                float wet[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    lines[(size_t) ch].push (in[ch] + fb[ch] * feedback);
                    float sum = 0.0f;
                    for (int v = 0; v < nv; ++v)
                    {
                        // the right side runs a quarter of a cycle behind the left at full width
                        const float lfo = (float) std::sin ((voicePhase[v] + (ch == 1 ? 0.25 * width : 0.0)) * MathConstants<double>::twoPi);
                        sum += lines[(size_t) ch].read (jmax (1.0f, (delay + swing * lfo) * msToSamples));
                    }
                    float w = sum * norm;
                    fb[ch] = sum / (float) nv;   // the average of the copies, so the loop never gains however many there are
                    if (useLow) w = lowCut[(size_t) ch].process (w, 0);
                    if (useHigh) w = highCut[(size_t) ch].process (w, 0);
                    wet[ch] = w;
                }
                l[i] = in[0] * (1.0f - mix) + wet[0] * mix;
                r[i] = in[1] * (1.0f - mix) + wet[1] * mix;
                // in the ensemble the three copies drift at slightly different speeds, as its old hardware did
                for (int v = 0; v < 3; ++v)
                {
                    voicePhase[v] += inc * voiceSpeed[v];
                    if (voicePhase[v] >= 1.0) voicePhase[v] -= 1.0;
                }
            }
            lfoEnd (voicePhase[0]);
        }

    private:
        std::array<DelayLine, 2> lines;
        std::array<Biquad, 2> lowCut, highCut;
        float lastLow = -1.0f, lastHigh = -1.0f, fb[2] {};
        Smooth sDelay, sMix, sSwing;
        double voicePhase[3] { 0.0, 1.0 / 3.0, 2.0 / 3.0 };
        static constexpr double voiceSpeed[3] = { 1.0, 1.11, 0.93 };
    };

    //==============================================================================
    /** Phaser (a chain of all-pass filters whose notches sweep) and Flanger (a very short moving delay: a comb). */
    class PhaserFlanger : public ModEffect
    {
    public:
        enum { pMode, pRate, pSync, pAmount, pCentre, pFeedback, pStages, pSpread, pMix };
        static inline const char* xmlTypeName = "pulsePhaser";
        static const char* getPluginName() { return "Phaser-Flanger"; }
        static inline const StringArray modeNames { "Phaser", "Flanger" };
        static inline const StringArray stageNames { "2", "4", "6", "8", "12" };
        static constexpr int stageCounts[] = { 2, 4, 6, 8, 12 };
        static constexpr int maxStages = 12;
        static constexpr float sweepOctaves = 2.5f;   // either way at full Amount

        explicit PhaserFlanger (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("rate", "Rate", skewed (0.02f, 10.0f, 0.5f), 0.4f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("sync", "Sync", steps (syncNames.size()), 0.0f, fmt::choice (syncNames));
            add ("amount", "Amount", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("centre", "Centre", skewed (50.0f, 10000.0f, 800.0f), 700.0f, fmt::hz);
            add ("feedback", "Feedback", { -0.95f, 0.95f }, 0.4f, fmt::pct);
            add ("stages", "Stages", steps (stageNames.size()), 1.0f, fmt::choice (stageNames));
            add ("spread", "Stereo", { 0.0f, 180.0f }, 90.0f, [] (float v) { return String (roundToInt (v)) + String::charToString (0x00b0); });
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.5f, fmt::pct);
        }

        String getName() const override       { return "Phaser-Flanger"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Sweep", { pMode, pRate, pSync, pAmount, pCentre } }, { "Colour", { pFeedback, pStages, pSpread } }, { "Output", { pMix } } };
        }

        int numStages() const { return stageCounts[jlimit (0, 4, getInt (pStages))]; }

        /** How loud a frequency comes out (as a gain) when the sweep sits at `sweptHz`, for the display. */
        float responseAt (double f, double sweptHz) const
        {
            const double sr = 96000.0, w = MathConstants<double>::twoPi * f / sr;
            const std::complex<double> z1 = std::polar (1.0, -w);
            std::complex<double> A;
            if (getInt (pMode) == 1)
                A = std::polar (1.0, -MathConstants<double>::twoPi * f / jlimit (50.0, 10000.0, sweptHz));
            else
            {
                const double t = std::tan (MathConstants<double>::pi * jlimit (20.0, sr * 0.45, sweptHz) / sr), a = (t - 1.0) / (t + 1.0);
                A = std::pow ((a + z1) / (1.0 + a * z1), numStages());
            }
            const double mix = get (pMix), fbk = get (pFeedback);
            return (float) std::abs ((1.0 - mix) + mix * A / (1.0 - fbk * A));
        }

        std::atomic<float> sweptLeft { 700.0f }, sweptRight { 700.0f };

    protected:
        void prepare() override
        {
            for (auto& d : lines) d.setMax ((int) (sampleRate * 0.03) + 16);
            for (auto& s : x1) s.fill (0.0f);
            for (auto& s : y1) s.fill (0.0f);
            last[0] = last[1] = 0.0f;
            sCentre.reset(); sMix.reset(); sFb.reset();
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const int mode = getInt (pMode), stages = numStages();
            const float amount = get (pAmount), spread = get (pSpread) / 360.0f;
            const float a20 = std::exp (-1.0f / (float) (0.02 * sampleRate));
            double phase, inc;
            lfoBegin (get (pRate), getInt (pSync), fc, phase, inc);
            float* d[2] = { l, r };

            for (int i = 0; i < n; ++i)
            {
                const float centre = sCentre.next (get (pCentre), a20), mix = sMix.next (get (pMix), a20), feedback = sFb.next (get (pFeedback), a20);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float lfo = (float) std::sin ((phase + (ch == 1 ? spread : 0.0f)) * MathConstants<double>::twoPi);
                    const float f = jlimit (30.0f, (float) sampleRate * 0.45f, centre * std::pow (2.0f, lfo * amount * sweepOctaves));
                    if (i == n - 1) (ch == 0 ? sweptLeft : sweptRight).store (f);
                    const float dry = d[ch][i];
                    float wet;
                    if (mode == 1)
                    {
                        // a delay of one cycle of the swept frequency: the notches fall halfway between its multiples
                        lines[(size_t) ch].push (dry + last[ch] * feedback);
                        wet = lines[(size_t) ch].read (jmax (1.0f, (float) sampleRate / jlimit (50.0f, 10000.0f, f)));
                    }
                    else
                    {
                        const float t = std::tan (MathConstants<float>::pi * f / (float) sampleRate), a = (t - 1.0f) / (t + 1.0f);
                        wet = dry + last[ch] * feedback;
                        for (int s = 0; s < stages; ++s)
                        {
                            const float y = a * wet + x1[(size_t) ch][(size_t) s] - a * y1[(size_t) ch][(size_t) s];
                            x1[(size_t) ch][(size_t) s] = wet;
                            y1[(size_t) ch][(size_t) s] = y;
                            wet = y;
                        }
                    }
                    last[ch] = wet;
                    d[ch][i] = dry * (1.0f - mix) + wet * mix;
                }
                phase += inc;
            }
            lfoEnd (phase);
        }

    private:
        std::array<DelayLine, 2> lines;
        std::array<std::array<float, maxStages>, 2> x1 {}, y1 {};
        float last[2] {};
        Smooth sCentre, sMix, sFb;
    };

    //==============================================================================
    /** Auto Pan: an LFO turns the two sides down in turn (stereo phase 180) or together (0: a tremolo). */
    class AutoPan : public ModEffect
    {
    public:
        enum { pAmount, pRate, pSync, pShape, pPhase, pSmooth };
        static inline const char* xmlTypeName = "pulseAutoPan";
        static const char* getPluginName() { return "Auto Pan"; }
        static inline const StringArray shapeNames { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random" };

        explicit AutoPan (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("amount", "Amount", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("rate", "Rate", skewed (0.05f, 30.0f, 2.0f), 2.0f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("sync", "Sync", steps (syncNames.size()), 0.0f, fmt::choice (syncNames));
            add ("shape", "Shape", steps (shapeNames.size()), 0.0f, fmt::choice (shapeNames));
            add ("phase", "Stereo Phase", { 0.0f, 360.0f }, 180.0f, [] (float v) { return String (roundToInt (v)) + String::charToString (0x00b0); });
            add ("smooth", "Smooth", { 0.0f, 1.0f }, 0.1f, fmt::pct);
        }

        String getName() const override       { return "Auto Pan"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Movement", { pAmount, pRate, pSync } }, { "Shape", { pShape, pPhase, pSmooth } } }; }

        static float lfoValue (int shape, double phase, float randomHeld) noexcept   // phase in cycles, result -1..1
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
        /** The gain of one side for an LFO value: between 1 and 1 - Amount. */
        static float gainFor (float lfo, float amount) noexcept { return 1.0f - amount * (0.5f - 0.5f * lfo); }

    protected:
        void prepare() override { g[0] = g[1] = 1.0f; held[0] = held[1] = 0.0f; }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const float amount = get (pAmount);
            const int shape = getInt (pShape);
            const double offset = get (pPhase) / 360.0;
            // even "no smoothing" takes a third of a millisecond, so square and random steps do not click
            const float a = std::exp (-1.0f / (float) ((0.0003 + get (pSmooth) * get (pSmooth) * 0.04) * sampleRate));
            double phase, inc;
            lfoBegin (get (pRate), getInt (pSync), fc, phase, inc);
            float* d[2] = { l, r };
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double ph = phase + (ch == 1 ? offset : 0.0);
                    if (shape == 5 && ph - std::floor (ph) < inc) held[ch] = random.nextFloat() * 2.0f - 1.0f;   // a new value each cycle
                    const float target = gainFor (lfoValue (shape, ph, held[ch]), amount);
                    g[ch] = target + (g[ch] - target) * a;
                    d[ch][i] *= g[ch];
                }
                phase += inc;
            }
            lfoEnd (phase);
        }

    private:
        float g[2] { 1.0f, 1.0f }, held[2] {};
        Random random { 77 };
    };
}

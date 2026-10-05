#pragma once

// Included from PulseSynths.h (needs PulseEffect, Biquad, SVF, Tuner::detectPitch, Scales).

/** Pulse Reverb (feedback delay network), Pulse Delay (sync / ping-pong / filtered feedback),
    Vocoder (16 bands, sidechain or internal carrier) and Pitch Correction (scale-aware). */
namespace pulse
{
    //==============================================================================
    /** A fractional delay line with linear interpolation. */
    struct DelayLine
    {
        std::vector<float> buf;
        int w = 0;
        void setMax (int n) { buf.assign ((size_t) jmax (4, n), 0.0f); w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void push (float x) noexcept { if (buf.empty()) return; buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
        float read (float delay) const noexcept   // samples ago (>= 1)
        {
            const int n = (int) buf.size();
            if (n < 4) return 0.0f;   // not prepared yet
            // in double: a float cannot hold a position in a several-second buffer to a fraction of a sample, and
            // could round a position just below the end up to the end itself (one past the last sample)
            double pos = (double) w - (double) jlimit (1.0f, (float) n - 2.0f, delay);
            if (pos < 0.0) pos += (double) n;
            int i = (int) pos;
            const float f = (float) (pos - (double) i);
            if (i >= n) i -= n;
            const int j = i + 1 < n ? i + 1 : 0;
            return buf[(size_t) i] * (1.0f - f) + buf[(size_t) j] * f;
        }
    };

    //==============================================================================
    class Reverb : public PulseEffect
    {
    public:
        enum { pPredelay, pSize, pDecay, pDamping, pLowCut, pDiffusion, pMod, pWidth, pFreeze, pMix };
        static inline const char* xmlTypeName = "pulseReverb";
        static const char* getPluginName() { return "Pulse Reverb"; }

        explicit Reverb (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("predelay", "Predelay", skewed (0.0f, 250.0f, 30.0f), 15.0f, fmt::ms);
            add ("size", "Size", { 0.1f, 1.0f }, 0.6f, fmt::pct);
            add ("decay", "Decay", skewed (0.2f, 20.0f, 2.5f), 2.2f, [] (float v) { return String (v, 2) + " s"; });
            add ("damping", "Damping", skewed (1000.0f, 20000.0f, 6000.0f), 7000.0f, fmt::hz);
            add ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 150.0f), 100.0f, fmt::hz);
            add ("diffusion", "Diffusion", { 0.0f, 0.9f }, 0.7f, fmt::pct);
            add ("mod", "Chorus", { 0.0f, 1.0f }, 0.25f, fmt::pct);
            add ("width", "Width", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("freeze", "Freeze", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.3f, fmt::pct);
        }

        String getName() const override     { return "Pulse Reverb"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 150; }
        std::vector<Section> getSections() const override
        {
            return { { "Space", { pPredelay, pSize, pDecay, pDiffusion } }, { "Tone", { pDamping, pLowCut, pMod } }, { "Output", { pWidth, pFreeze, pMix } } };
        }

    protected:
        static constexpr int N = 8;
        static constexpr float baseLens[N] = { 1031.0f, 1327.0f, 1523.0f, 1801.0f, 2063.0f, 2381.0f, 2683.0f, 2971.0f };   // at 44.1 kHz
        static constexpr float diffLens[4] = { 142.0f, 107.0f, 379.0f, 277.0f };

        void prepare() override
        {
            const double scale = sampleRate / 44100.0;
            pre.setMax ((int) (sampleRate * 0.3) + 4);
            for (int i = 0; i < N; ++i) { lines[(size_t) i].setMax ((int) (baseLens[i] * 2.2f * scale) + 64); lp[(size_t) i] = 0.0f; }
            for (int i = 0; i < 4; ++i) diff[(size_t) i].setMax ((int) (diffLens[i] * scale) + 8);
            for (auto& b : lowCut) b.reset();
            lastLowCut = -1.0f;
            phase = 0.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const double scale = sampleRate / 44100.0;
            const float size = get (pSize), decay = get (pDecay), mix = get (pMix), width = get (pWidth);
            const float diffusion = get (pDiffusion), modDepth = get (pMod) * 12.0f * (float) scale;
            const bool freeze = getInt (pFreeze) > 0;
            const float pd = jmax (1.0f, get (pPredelay) * 0.001f * (float) sampleRate);
            const float damp = std::exp (-MathConstants<float>::twoPi * get (pDamping) / (float) sampleRate);
            if (std::abs (get (pLowCut) - lastLowCut) > 0.5f)
            {
                for (auto& b : lowCut) b.set (Biquad::Type::lowCut, sampleRate, get (pLowCut), 0.707, 0.0);
                lastLowCut = get (pLowCut);
            }
            float len[N], g[N];
            for (int i = 0; i < N; ++i)
            {
                len[i] = baseLens[i] * (0.35f + 1.65f * size) * (float) scale;
                g[i] = freeze ? 1.0f : std::pow (10.0f, -3.0f * len[i] / ((float) sampleRate * jmax (0.05f, decay)));   // RT60
            }
            const float inGain = freeze ? 0.0f : 1.0f;

            float peak = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                const float dryL = l[s], dryR = r[s];
                pre.push (0.5f * (dryL + dryR));
                float x = lowCut[0].process (pre.read (pd), 0) * inGain;
                for (int d = 0; d < 4; ++d)   // input diffusion (allpasses)
                {
                    const float dl = diffLens[d] * (float) scale;
                    const float delayed = diff[(size_t) d].read (dl);
                    const float v = x + diffusion * delayed;
                    diff[(size_t) d].push (v);
                    x = delayed - diffusion * v;
                }
                phase += 0.7 / sampleRate;
                if (phase > 1.0) phase -= 1.0;
                float o[N];
                for (int i = 0; i < N; ++i)
                {
                    const float m = modDepth * (float) std::sin (MathConstants<double>::twoPi * (phase + i * 0.125));
                    float y = lines[(size_t) i].read (len[i] + m);
                    lp[(size_t) i] = y + (lp[(size_t) i] - y) * damp;   // one-pole damping
                    o[i] = lp[(size_t) i] * g[i];
                }
                // Householder feedback: y = o - 2/N * sum(o)
                float sum = 0.0f;
                for (int i = 0; i < N; ++i) sum += o[i];
                sum *= 2.0f / (float) N;
                for (int i = 0; i < N; ++i)
                    lines[(size_t) i].push (o[i] - sum + (i % 2 == 0 ? x : -x) * 0.35f);

                const float wl = (o[0] + o[2] + o[4] + o[6]) * 0.5f, wr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                l[s] = dryL * (1.0f - mix) + (mid + side) * mix;
                r[s] = dryR * (1.0f - mix) + (mid - side) * mix;
                peak = jmax (peak, std::abs (wl), std::abs (wr));
            }
            wetDb = jmax (Decibels::gainToDecibels (peak, -100.0f), wetDb.load() - 1.5f);
        }

    public:
        std::atomic<float> wetDb { -100.0f };   // the tail's level (for the display)

    private:
        DelayLine pre;
        std::array<DelayLine, N> lines;
        std::array<DelayLine, 4> diff;
        std::array<float, N> lp {};
        std::array<Biquad, 1> lowCut;
        float lastLowCut = -1.0f;
        double phase = 0.0;
    };

    //==============================================================================
    class Delay : public PulseEffect
    {
    public:
        enum { pSync, pTime, pNote, pFeedback, pPingPong, pLowCut, pHighCut, pMod, pDrive, pMix };
        static inline const char* xmlTypeName = "pulseDelay";
        static const char* getPluginName() { return "Pulse Delay"; }
        static inline const StringArray noteNames { "1/32", "1/16", "1/16 dotted", "1/8 triplet", "1/8", "1/8 dotted", "1/4 triplet", "1/4", "1/4 dotted", "1/2", "1 bar" };
        static double noteBeats (int i)
        {
            const double b[] = { 0.125, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
            return b[jlimit (0, 10, i)];
        }

        explicit Delay (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("sync", "Sync", steps (2), 1.0f, fmt::choice ({ "Time", "Tempo" }));
            add ("time", "Time", skewed (1.0f, 2000.0f, 300.0f), 375.0f, fmt::ms);
            add ("note", "Note", steps (noteNames.size()), 5.0f, fmt::choice (noteNames));
            add ("feedback", "Feedback", { 0.0f, 1.0f }, 0.4f, fmt::pct);
            add ("pingpong", "Ping-pong", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("lowcut", "Low Cut", skewed (20.0f, 2000.0f, 200.0f), 120.0f, [] (float v) { return v <= 20.5f ? String ("Off") : fmt::hz (v); });
            add ("highcut", "High Cut", skewed (500.0f, 20000.0f, 5000.0f), 8000.0f, [] (float v) { return v >= 19500.0f ? String ("Off") : fmt::hz (v); });
            add ("mod", "Wobble", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("drive", "Drive", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.3f, fmt::pct);
        }

        String getName() const override     { return "Pulse Delay"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 150; }
        std::vector<Section> getSections() const override
        {
            return { { "Time", { pSync, pTime, pNote, pPingPong } }, { "Feedback", { pFeedback, pLowCut, pHighCut } }, { "Colour", { pMod, pDrive, pMix } } };
        }

        /** The current delay in seconds (for the display too). */
        double delaySeconds (double bpm) const
        {
            return getInt (pSync) > 0 ? noteBeats (getInt (pNote)) * 60.0 / jmax (20.0, bpm) : get (pTime) * 0.001;
        }
        std::atomic<double> lastBpm { 120.0 };

    protected:
        void prepare() override
        {
            for (auto& d : lines) d.setMax ((int) (sampleRate * 4.5) + 8);
            for (auto& f : lo) f.reset();
            for (auto& f : hi) f.reset();
            lastLo = lastHi = -1.0f;
            smoothDelay = -1.0f;
            phase = 0.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            double bpm = 120.0;
            if (fc.editTime.getStart() >= te::TimePosition())
                bpm = edit.tempoSequence.getInternalSequence().getBeatsPerSecondAt (fc.editTime.getStart()).v * 60.0;   // audio-thread safe
            lastBpm = bpm;
            const float target = (float) jlimit (1.0, sampleRate * 4.0, delaySeconds (bpm) * sampleRate);
            if (smoothDelay < 0.0f) smoothDelay = target;
            const float fb = jmin (0.98f, get (pFeedback)), mix = get (pMix), drive = get (pDrive), wob = get (pMod);
            const bool ping = getInt (pPingPong) > 0;
            const bool useLo = get (pLowCut) > 20.5f, useHi = get (pHighCut) < 19500.0f;
            if (std::abs (get (pLowCut) - lastLo) > 0.5f) { for (auto& f : lo) f.set (Biquad::Type::lowCut, sampleRate, get (pLowCut), 0.707, 0.0); lastLo = get (pLowCut); }
            if (std::abs (get (pHighCut) - lastHi) > 0.5f) { for (auto& f : hi) f.set (Biquad::Type::highCut, sampleRate, get (pHighCut), 0.707, 0.0); lastHi = get (pHighCut); }

            for (int s = 0; s < n; ++s)
            {
                smoothDelay += (target - smoothDelay) * 0.0005f;   // glide on time changes (tape-like)
                phase += 0.9 / sampleRate;
                if (phase > 1.0) phase -= 1.0;
                const float d = smoothDelay + wob * 0.004f * (float) sampleRate * (float) std::sin (MathConstants<double>::twoPi * phase);
                float yl = lines[0].read (d), yr = lines[1].read (d);
                auto shape = [drive] (float v) { return drive > 0.001f ? std::tanh (v * (1.0f + drive * 4.0f)) / (1.0f + drive * 0.5f) : v; };
                float fl = yl, fr = yr;
                if (useLo) { fl = lo[0].process (fl, 0); fr = lo[1].process (fr, 0); }
                if (useHi) { fl = hi[0].process (fl, 0); fr = hi[1].process (fr, 0); }
                fl = shape (fl) * fb;
                fr = shape (fr) * fb;
                const float inL = l[s], inR = r[s];
                if (ping)   // input enters the left line, the repeats cross over
                {
                    lines[0].push (0.5f * (inL + inR) + fr);
                    lines[1].push (fl);
                }
                else
                {
                    lines[0].push (inL + fl);
                    lines[1].push (inR + fr);
                }
                l[s] = inL * (1.0f - mix) + yl * mix;
                r[s] = inR * (1.0f - mix) + yr * mix;
            }
        }

    private:
        std::array<DelayLine, 2> lines;
        std::array<Biquad, 2> lo, hi;
        float lastLo = -1.0f, lastHi = -1.0f, smoothDelay = -1.0f;
        double phase = 0.0;
    };

    //==============================================================================
    /** 16-band vocoder: the track's audio (voice) shapes a carrier: the sidechain (a synth on
        another track), or an internal saw / noise. */
    class Vocoder : public PulseEffect
    {
    public:
        enum { pCarrier, pPitch, pFormant, pAttack, pRelease, pNoise, pMix };
        static inline const char* xmlTypeName = "pulseVocoder";
        static const char* getPluginName() { return "Vocoder"; }
        static constexpr int bands = 16;

        explicit Vocoder (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("carrier", "Carrier", steps (3), 1.0f, fmt::choice ({ "Sidechain", "Saw", "Noise" }));
            add ("pitch", "Saw Pitch", { 24.0f, 72.0f, 1.0f }, 48.0f, [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); });
            add ("formant", "Formant", { -12.0f, 12.0f }, 0.0f, fmt::st);
            add ("attack", "Attack", skewed (0.5f, 100.0f, 5.0f), 3.0f, fmt::ms);
            add ("release", "Release", skewed (5.0f, 500.0f, 60.0f), 40.0f, fmt::ms);
            add ("noise", "Unvoiced", { 0.0f, 1.0f }, 0.15f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        bool wantsSidechain() const override { return true; }
        String getName() const override     { return "Vocoder"; }
        String getPluginType() override     { return xmlTypeName; }
        std::vector<Section> getSections() const override
        {
            return { { "Carrier", { pCarrier, pPitch, pNoise } }, { "Bands", { pFormant, pAttack, pRelease } }, { "Output", { pMix } } };
        }

    protected:
        void prepare() override { lastFormant = 999.0f; for (auto& e : env) e = 0.0f; sawPhase = 0.0; }

    public:
        static float bandFreq (int b) { return 100.0f * std::pow (2.0f, (float) b * std::log2 (8000.0f / 100.0f) / (float) (bands - 1)); }
    protected:

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float formant = get (pFormant);
            if (std::abs (formant - lastFormant) > 0.01f)
            {
                const float shift = std::pow (2.0f, formant / 12.0f);
                for (int b = 0; b < bands; ++b)
                {
                    modF[(size_t) b].set (Biquad::Type::bandPass, sampleRate, bandFreq (b), 4.0, 0.0);
                    carF[(size_t) b].set (Biquad::Type::bandPass, sampleRate, jmin (bandFreq (b) * shift, (float) sampleRate * 0.45f), 4.0, 0.0);
                }
                lastFormant = formant;
            }
            const int carrier = getInt (pCarrier);
            const float aA = std::exp (-1.0f / (get (pAttack) * 0.001f * (float) sampleRate));
            const float aR = std::exp (-1.0f / (get (pRelease) * 0.001f * (float) sampleRate));
            const float mix = get (pMix), noiseAmt = get (pNoise);
            const double sawInc = MidiMessage::getMidiNoteInHertz (getInt (pPitch)) / sampleRate;
            const float* scL = sidechain != nullptr ? sidechain->getReadPointer (0) : nullptr;
            const float* scR = sidechain != nullptr ? sidechain->getReadPointer (1) : nullptr;

            for (int s = 0; s < n; ++s)
            {
                const float mod = 0.5f * (l[s] + r[s]);
                float car;
                if (carrier == 0 && scL != nullptr) car = 0.5f * (scL[s] + scR[s]);
                else if (carrier == 2)              car = rng.nextFloat() * 2.0f - 1.0f;
                else
                {
                    sawPhase += sawInc;
                    if (sawPhase >= 1.0) sawPhase -= 1.0;
                    car = (float) (2.0 * sawPhase - 1.0);
                }
                car = car * (1.0f - noiseAmt) + (rng.nextFloat() * 2.0f - 1.0f) * noiseAmt * 0.5f;
                float out = 0.0f;
                for (int b = 0; b < bands; ++b)
                {
                    const float m = std::abs (modF[(size_t) b].process (mod, 0));
                    auto& e = env[(size_t) b];
                    e = m > e ? aA * e + (1.0f - aA) * m : aR * e + (1.0f - aR) * m;
                    out += carF[(size_t) b].process (car, 1) * e;
                }
                out *= 6.0f;
                l[s] = l[s] * (1.0f - mix) + out * mix;
                r[s] = r[s] * (1.0f - mix) + out * mix;
            }
            for (int b = 0; b < bands; ++b) bandLevel[(size_t) b].store (env[(size_t) b], std::memory_order_relaxed);
        }

    public:
        /** Each band's envelope (the voice's spectrum), for the display. */
        std::array<std::atomic<float>, bands> bandLevel {};

    private:
        std::array<Biquad, bands> modF, carF;
        std::array<float, bands> env {};
        float lastFormant = 999.0f;
        double sawPhase = 0.0;
        Random rng;
    };

    //==============================================================================
    /** Pitch correction: detects the sung / played note and shifts it to the nearest note of a
        scale. Speed sets how fast it glides there (0 = hard "robot" effect). */
    class PitchCorrect : public PulseEffect
    {
    public:
        enum { pRoot, pScale, pSpeed, pAmount, pMix };
        static inline const char* xmlTypeName = "pulsePitchCorrect";
        static const char* getPluginName() { return "Pitch Correction"; }

        explicit PitchCorrect (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("root", "Key", steps (12), 0.0f, fmt::choice (Scales::rootNames()));
            StringArray scaleNames ("Chromatic");
            scaleNames.addArray (Scales::names());
            add ("scale", "Scale", steps (scaleNames.size()), 0.0f, fmt::choice (scaleNames));
            add ("speed", "Speed", skewed (0.0f, 400.0f, 60.0f), 40.0f, [] (float v) { return v < 1.0f ? String ("Instant") : fmt::ms (v); });
            add ("amount", "Amount", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override     { return "Pitch Correction"; }
        String getPluginType() override     { return xmlTypeName; }
        int getDisplayWidth() const override { return 130; }
        std::vector<Section> getSections() const override { return { { "Scale", { pRoot, pScale } }, { "Correction", { pSpeed, pAmount, pMix } } }; }

        std::atomic<float> detectedNote { -1.0f }, targetNote { -1.0f };

        /** The nearest allowed note to a (fractional) MIDI note. */
        static int nearestNote (double note, int scaleIndex, int root)
        {
            const int base = (int) std::round (note);
            if (scaleIndex < 0) return base;
            int best = base;
            double bestD = 100.0;
            for (int d = -6; d <= 6; ++d)
                if (Scales::contains (scaleIndex, root, base + d) && std::abs (note - (base + d)) < bestD)
                {
                    bestD = std::abs (note - (base + d));
                    best = base + d;
                }
            return best;
        }

    protected:
        static constexpr int analysisSize = 2048;

        void prepare() override
        {
            line.setMax ((int) (sampleRate * 0.1) + 8);
            analysis.assign (analysisSize, 0.0f);
            ordered.assign (analysisSize, 0.0f);
            aPos = 0;
            ratio = 1.0f;
            targetRatio = 1.0f;
            tapPhase = 0.0f;
            sinceAnalysis = 0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const int scaleIndex = getInt (pScale) - 1, root = getInt (pRoot);
            const float speedMs = get (pSpeed), amount = get (pAmount), mix = get (pMix);
            const float glide = speedMs < 1.0f ? 1.0f : 1.0f - std::exp (-1.0f / (speedMs * 0.001f * (float) sampleRate));
            const float window = (float) (sampleRate * 0.03);   // 30 ms grains

            for (int s = 0; s < n; ++s)
            {
                const float x = 0.5f * (l[s] + r[s]);
                analysis[(size_t) aPos] = x;
                aPos = (aPos + 1) % analysisSize;
                if (++sinceAnalysis >= analysisSize / 4)   // re-detect every ~10 ms
                {
                    sinceAnalysis = 0;
                    for (int i = 0; i < analysisSize; ++i) ordered[(size_t) i] = analysis[(size_t) ((aPos + i) % analysisSize)];
                    const double hz = Tuner::detectPitch (ordered.data(), analysisSize, sampleRate, 70.0, 1000.0);
                    if (hz > 0.0)
                    {
                        const double note = Tuner::noteOf (hz);
                        const int tgt = nearestNote (note, scaleIndex, root);
                        detectedNote = (float) note;
                        targetNote = (float) tgt;
                        targetRatio = (float) std::pow (2.0, (tgt - note) * amount / 12.0);
                    }
                    else
                    {
                        detectedNote = -1.0f;
                        targetRatio = 1.0f;
                    }
                }
                ratio += (targetRatio - ratio) * glide;

                // two-tap delay-line pitch shifter: taps sweep through a 30 ms window, crossfaded
                line.push (x);
                tapPhase += (1.0f - ratio) / window;
                if (tapPhase >= 1.0f) tapPhase -= 1.0f;
                if (tapPhase < 0.0f) tapPhase += 1.0f;
                const float p2 = std::fmod (tapPhase + 0.5f, 1.0f);
                const float d1 = 1.0f + tapPhase * window, d2 = 1.0f + p2 * window;
                const float g1 = std::sin (MathConstants<float>::pi * tapPhase), g2 = std::sin (MathConstants<float>::pi * p2);
                const float y = line.read (d1) * g1 + line.read (d2) * g2;
                l[s] = l[s] * (1.0f - mix) + y * mix;
                r[s] = r[s] * (1.0f - mix) + y * mix;
            }
        }

    private:
        DelayLine line;
        std::vector<float> analysis, ordered;
        int aPos = 0, sinceAnalysis = 0;
        float ratio = 1.0f, targetRatio = 1.0f, tapPhase = 0.0f;
    };
}

#pragma once

#include "Dynamics.h"

/** More dynamics: Pulse Gate (gate / expander / ducker), Transient Shaper and De-esser.
    Each keeps a level history for its display, like the compressor. */
namespace pulse
{
    /** One-pole smoothing coefficient for a time in milliseconds. */
    inline float smoothCoeff (float ms, double sr) { return std::exp (-1.0f / (float) (jmax (0.01f, ms) * 0.001 * sr)); }

    /** Pushes peak levels to a LevelHistory every 5 ms. */
    struct HistoryFeeder
    {
        float inPk = 0, outPk = 0, grMax = 0, grMin = 0;
        int count = 0, every = 240;
        void prepare (double sr) { every = jmax (16, (int) (sr * 0.005)); count = 0; inPk = outPk = grMax = grMin = 0; }
        /** gainDb: the gain applied (negative = reduction, positive = boost). */
        inline void add (LevelHistory& h, float in, float out, float gainDb) noexcept
        {
            inPk = jmax (inPk, in);
            outPk = jmax (outPk, out);
            grMax = jmax (grMax, -gainDb);
            grMin = jmin (grMin, -gainDb);
            if (++count >= every)
            {
                // the larger swing of the two (a reduction is positive, a boost negative)
                h.push (Decibels::gainToDecibels (inPk, -100.0f), Decibels::gainToDecibels (outPk, -100.0f), grMax > -grMin ? grMax : grMin);
                count = 0;
                inPk = outPk = grMax = grMin = 0;
            }
        }
    };

    //==============================================================================
    /** Gate, downward expander and ducker in one: below the threshold the signal is turned down by
        up to Range (gate), or by a ratio (expander); as a ducker it is turned down while the key
        (the sidechain, or the signal itself) is above the threshold. Hysteresis and hold keep it
        from chattering, lookahead opens it just before the hit, and the key can be filtered. */
    class Gate : public PulseEffect
    {
    public:
        enum { pThreshold, pRange, pMode, pRatio, pHysteresis, pAttack, pHold, pRelease, pLookahead, pScHpf, pScLpf, pScListen };
        static inline const char* xmlTypeName = "pulseGate";
        static const char* getPluginName() { return "Pulse Gate"; }
        static inline const StringArray modeNames { "Gate", "Expander", "Ducker" };
        static constexpr double maxLookaheadMs = 10.0;

        explicit Gate (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("threshold", "Threshold", { -80.0f, 0.0f }, -40.0f, fmt::db);
            add ("range", "Range", { 0.0f, 80.0f }, 60.0f, [] (float v) { return v >= 79.5f ? String ("Full") : fmt::db (-v); });
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("ratio", "Ratio", skewed (1.0f, 10.0f, 2.5f), 2.0f, [] (float v) { return "1 : " + String (v, 1); });
            add ("hysteresis", "Hysteresis", { 0.0f, 12.0f }, 3.0f, fmt::db);
            add ("attack", "Attack", skewed (0.01f, 100.0f, 2.0f), 0.5f, fmt::ms);
            add ("hold", "Hold", skewed (0.0f, 500.0f, 60.0f), 30.0f, fmt::ms);
            add ("release", "Release", skewed (5.0f, 2000.0f, 150.0f), 120.0f, fmt::ms);
            add ("lookahead", "Lookahead", { 0.0f, (float) maxLookaheadMs }, 0.0f, fmt::ms);
            add ("schpf", "Key Low Cut", skewed (10.0f, 2000.0f, 150.0f), 10.0f, [] (float v) { return v <= 10.5f ? String ("Off") : fmt::hz (v); });
            add ("sclpf", "Key High Cut", skewed (500.0f, 22000.0f, 5000.0f), 22000.0f, [] (float v) { return v >= 21500.0f ? String ("Off") : fmt::hz (v); });
            add ("sclisten", "Key Listen", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override       { return "Pulse Gate"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        bool wantsSidechain() const override   { return true; }
        double getLatencySeconds() override    { return get (pLookahead) * 0.001; }

        std::vector<Section> getSections() const override
        {
            return { { "Gate", { pThreshold, pRange, pMode, pRatio, pHysteresis } }, { "Timing", { pAttack, pHold, pRelease, pLookahead } },
                     { "Key", { pScHpf, pScLpf, pScListen } } };
        }

        /** The gain (dB, zero or below) for a steady key level, as the display draws it. */
        float staticGainDb (float levelDb) const
        {
            const float th = get (pThreshold), range = get (pRange);
            switch (getInt (pMode))
            {
                case 1:  return levelDb < th ? -jmin (range, (th - levelDb) * (get (pRatio) - 1.0f)) : 0.0f;
                case 2:  return levelDb > th ? -range : 0.0f;
                default: return levelDb < th ? -range : 0.0f;
            }
        }

        LevelHistory history;
        std::atomic<float> gainReductionDb { 0.0f }, keyDb { -100.0f };
        std::atomic<bool> isOpen { false };

    protected:
        void prepare() override
        {
            const int n = (int) (sampleRate * maxLookaheadMs * 0.001) + 8;
            for (auto& d : delay) d.assign ((size_t) n, 0.0f);
            delayPos = 0;
            for (auto& f : hp) f.reset();
            for (auto& f : lp) f.reset();
            lastHp = lastLp = -1.0f;
            gainDb = 0.0f; env = 0.0f; holdLeft = 0; open = false;
            feeder.prepare (sampleRate);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float th = get (pThreshold), range = get (pRange), ratio = get (pRatio), hyst = get (pHysteresis);
            const int mode = getInt (pMode);
            const float aAtk = smoothCoeff (get (pAttack), sampleRate), aRel = smoothCoeff (get (pRelease), sampleRate);
            const float envRel = smoothCoeff (10.0f, sampleRate);
            const int holdSamples = (int) (get (pHold) * 0.001 * sampleRate);
            const int look = jlimit (0, (int) delay[0].size() - 2, (int) (get (pLookahead) * 0.001 * sampleRate));
            const bool listen = getInt (pScListen) > 0;
            const bool useHp = get (pScHpf) > 10.5f, useLp = get (pScLpf) < 21500.0f;
            if (std::abs (get (pScHpf) - lastHp) > 0.5f) { for (auto& f : hp) f.set (Biquad::Type::lowCut, sampleRate, get (pScHpf), 0.707, 0.0); lastHp = get (pScHpf); }
            if (std::abs (get (pScLpf) - lastLp) > 0.5f) { for (auto& f : lp) f.set (Biquad::Type::highCut, sampleRate, get (pScLpf), 0.707, 0.0); lastLp = get (pScLpf); }
            const float* kl = sidechain != nullptr ? sidechain->getReadPointer (0) : l;
            const float* kr = sidechain != nullptr ? sidechain->getReadPointer (1) : r;
            const int size = (int) delay[0].size();
            float worst = 0.0f, keyPeak = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                float a = kl[i], b = kr[i];
                if (useHp) { a = hp[0].process (a, 0); b = hp[1].process (b, 0); }
                if (useLp) { a = lp[0].process (a, 0); b = lp[1].process (b, 0); }
                const float key = jmax (std::abs (a), std::abs (b));
                env = key > env ? key : key + (env - key) * envRel;   // instant up, 10 ms down
                keyPeak = jmax (keyPeak, env);
                const float levelDb = Decibels::gainToDecibels (env, -120.0f);

                float target = 0.0f;   // dB
                if (mode == 1)
                    target = levelDb < th ? -jmin (range, (th - levelDb) * (ratio - 1.0f)) : 0.0f;
                else
                {
                    // gate and ducker: open above the threshold, shut below threshold - hysteresis once the hold has run out
                    if (levelDb > th) { open = true; holdLeft = holdSamples; }
                    else if (levelDb < th - hyst) { if (holdLeft > 0) --holdLeft; else open = false; }
                    const bool pass = mode == 2 ? ! open : open;
                    target = pass ? 0.0f : -range;
                }
                gainDb = target + (gainDb - target) * (target > gainDb ? aAtk : aRel);   // opening uses the attack, closing the release
                const float g = Decibels::decibelsToGain (gainDb, -100.0f);

                // the signal itself, late by the lookahead
                delay[0][(size_t) delayPos] = l[i];
                delay[1][(size_t) delayPos] = r[i];
                const int rp = (delayPos - look + size) % size;
                const float dl = delay[0][(size_t) rp], dr = delay[1][(size_t) rp];
                delayPos = (delayPos + 1) % size;

                const float inPk = jmax (std::abs (dl), std::abs (dr));
                l[i] = listen ? a : dl * g;
                r[i] = listen ? b : dr * g;
                worst = jmin (worst, gainDb);
                feeder.add (history, inPk, jmax (std::abs (l[i]), std::abs (r[i])), gainDb);
            }
            gainReductionDb.store (-worst);
            keyDb.store (Decibels::gainToDecibels (keyPeak, -100.0f));
            isOpen.store (gainDb > -range * 0.5f);
        }

    private:
        std::array<std::vector<float>, 2> delay;
        int delayPos = 0, holdLeft = 0;
        std::array<Biquad, 2> hp, lp;
        float lastHp = -1.0f, lastLp = -1.0f, gainDb = 0.0f, env = 0.0f;
        bool open = false;
        HistoryFeeder feeder;
    };

    //==============================================================================
    /** Transient shaper: turns the hit (attack) and the tail (sustain) of each sound up or down
        without a threshold, by comparing a fast and a slow follower of the level. It looks 3 ms
        ahead (reported as latency) so that the very start of a hit is shaped too. */
    class TransientShaper : public PulseEffect
    {
    public:
        enum { pAttack, pSustain, pLength, pOutput, pClip, pMix };
        static inline const char* xmlTypeName = "pulseTransient";
        static const char* getPluginName() { return "Transient Shaper"; }

        explicit TransientShaper (te::PluginCreationInfo info) : PulseEffect (info)
        {
            auto amount = [] (float v) { return (v > 0.005f ? "+" : "") + String (roundToInt (v * 100)) + "%"; };
            add ("attack", "Attack", { -1.0f, 1.0f }, 0.3f, amount);
            add ("sustain", "Sustain", { -1.0f, 1.0f }, 0.0f, amount);
            add ("length", "Hit Length", skewed (2.0f, 80.0f, 20.0f), 20.0f, fmt::ms);
            add ("output", "Output", { -24.0f, 12.0f }, 0.0f, fmt::db);
            add ("clip", "Soft Clip", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override       { return "Transient Shaper"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Shape", { pAttack, pSustain, pLength } }, { "Output", { pOutput, pClip, pMix } } };
        }

        static constexpr float maxChangeDb = 18.0f;   // the most Attack or Sustain can add or take away
        static constexpr double lookaheadMs = 3.0;
        double getLatencySeconds() override    { return lookaheadMs * 0.001; }

        LevelHistory history;
        std::atomic<float> gainNowDb { 0.0f };

    protected:
        void prepare() override
        {
            peak = fast = slow = relSlow = 1.0e-6f;
            smoothed = 0.0f;
            look = jmax (1, (int) (sampleRate * lookaheadMs * 0.001));
            for (auto& d : delay) d.assign ((size_t) look, 0.0f);
            delayPos = 0;
            feeder.prepare (sampleRate);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float attack = get (pAttack), sustain = get (pSustain), mix = get (pMix);
            const float out = Decibels::decibelsToGain (get (pOutput));
            const bool clip = getInt (pClip) > 0;
            // The level first, as the peaks with a 50 ms fall, so that the cycles of a low note are not taken for hits.
            const float aPeak = smoothCoeff (50.0f, sampleRate);
            // the hit: a follower that rises at once against one that takes "Hit Length" to rise
            const float aFastUp = smoothCoeff (0.2f, sampleRate), aSlowUp = smoothCoeff (get (pLength), sampleRate), aDown = smoothCoeff (60.0f, sampleRate);
            // the tail: the level against a follower that falls slowly
            const float aRelSlow = smoothCoeff (400.0f, sampleRate);
            const float aSmooth = smoothCoeff (1.0f, sampleRate);
            constexpr float ripple = 1.0f;   // dB that a steady note may wobble by without counting as a hit or a tail
            float last = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float level = jmax (std::abs (l[i]), std::abs (r[i]), 1.0e-6f);
                peak = level > peak ? level : level + (peak - level) * aPeak;
                fast = peak > fast ? peak + (fast - peak) * aFastUp : peak;
                slow = peak + (slow - peak) * (peak > slow ? aSlowUp : aDown);
                relSlow = peak > relSlow ? peak : peak + (relSlow - peak) * aRelSlow;

                const float hitDb = jlimit (0.0f, maxChangeDb, Decibels::gainToDecibels (fast / jmax (1.0e-6f, slow), -100.0f) - ripple);
                const float tailDb = jlimit (0.0f, maxChangeDb, Decibels::gainToDecibels (relSlow / jmax (1.0e-6f, peak), -100.0f) - ripple);
                const float target = jlimit (-maxChangeDb, maxChangeDb, attack * hitDb * 1.5f + sustain * tailDb);
                smoothed = target + (smoothed - target) * aSmooth;
                const float g = Decibels::decibelsToGain (smoothed);

                // the sound itself comes out late by the lookahead, so the gain is already there when a hit arrives
                const float dl = delay[0][(size_t) delayPos], dr = delay[1][(size_t) delayPos];
                delay[0][(size_t) delayPos] = l[i];
                delay[1][(size_t) delayPos] = r[i];
                if (++delayPos >= look) delayPos = 0;

                float wl = dl * g * out, wr = dr * g * out;
                if (clip)   // rounds off what the boosted hits push past full scale
                {
                    auto soft = [] (float x) { return std::abs (x) < 0.7f ? x : (x > 0 ? 1.0f : -1.0f) * (0.7f + 0.3f * std::tanh ((std::abs (x) - 0.7f) / 0.3f)); };
                    wl = soft (wl); wr = soft (wr);
                }
                const float inPk = jmax (std::abs (dl), std::abs (dr));
                l[i] = dl + (wl - dl) * mix;
                r[i] = dr + (wr - dr) * mix;
                last = smoothed;
                feeder.add (history, inPk, jmax (std::abs (l[i]), std::abs (r[i])), smoothed);
            }
            gainNowDb.store (last);
        }

    private:
        float peak = 0, fast = 0, slow = 0, relSlow = 0, smoothed = 0;
        std::array<std::vector<float>, 2> delay;
        int delayPos = 0, look = 1;
        HistoryFeeder feeder;
    };

    //==============================================================================
    /** De-esser: listens to a band around the "s" sounds and, when it gets too loud, turns down
        either just the top of the signal (split band) or all of it (wide band). */
    class DeEsser : public PulseEffect
    {
    public:
        enum { pFreq, pThreshold, pRange, pMode, pRelease, pListen };
        static inline const char* xmlTypeName = "pulseDeEsser";
        static const char* getPluginName() { return "De-esser"; }
        static inline const StringArray modeNames { "Split band", "Wide band" };

        explicit DeEsser (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("freq", "Frequency", skewed (2000.0f, 14000.0f, 6000.0f), 6500.0f, fmt::hz);
            add ("threshold", "Threshold", { -60.0f, 0.0f }, -30.0f, fmt::db);
            add ("range", "Range", { 0.0f, 24.0f }, 8.0f, [] (float v) { return fmt::db (-v); });
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("release", "Release", skewed (10.0f, 400.0f, 80.0f), 60.0f, fmt::ms);
            add ("listen", "Listen", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override       { return "De-esser"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Detect", { pFreq, pThreshold, pListen } }, { "Reduce", { pRange, pMode, pRelease } } };
        }

        /** The band the detector listens to (magnitude at f, 0..1) for the display. */
        float detectorResponse (float f) const
        {
            const float x = std::log2 (jmax (20.0f, f) / get (pFreq));
            return 1.0f / std::sqrt (1.0f + (x * 2.4f) * (x * 2.4f) * (x * 2.4f) * (x * 2.4f));
        }

        LevelHistory history;
        std::atomic<float> reductionDb { 0.0f }, bandDb { -100.0f };

    protected:
        void prepare() override
        {
            for (auto& f : band) f = {};
            for (auto& f : split) f = {};
            env = 0.0f; gainDb = 0.0f; lastFreq = -1.0f;
            feeder.prepare (sampleRate);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float freq = get (pFreq), th = get (pThreshold), range = get (pRange);
            const bool wide = getInt (pMode) > 0, listen = getInt (pListen) > 0;
            if (std::abs (freq - lastFreq) > 1.0f)
            {
                for (auto& f : band) f.set (freq, 0.55f, sampleRate);              // the detection band
                for (auto& f : split) f.set (freq * 0.75f, 0.0f, sampleRate);       // where the top is split off
                lastFreq = freq;
            }
            const float aUp = smoothCoeff (0.3f, sampleRate), aDown = smoothCoeff (get (pRelease), sampleRate);
            const float aGain = smoothCoeff (1.0f, sampleRate);
            float worst = 0.0f, bandPeak = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float bl = band[0].process (l[i], 0, 1), br = band[1].process (r[i], 1, 1);
                const float key = jmax (std::abs (bl), std::abs (br));
                env = key + (env - key) * (key > env ? aUp : aDown);
                bandPeak = jmax (bandPeak, env);
                const float over = Decibels::gainToDecibels (env, -120.0f) - th;
                const float target = over > 0.0f ? -jmin (range, over * 0.75f) : 0.0f;   // about 4 : 1 above the threshold
                gainDb = target + (gainDb - target) * aGain;
                const float g = Decibels::decibelsToGain (gainDb);
                const float inPk = jmax (std::abs (l[i]), std::abs (r[i]));

                // the split runs in every mode, so that changing the mode while playing does not click
                const float lowL = split[0].process (l[i], 0, 0), lowR = split[1].process (r[i], 1, 0);
                if (listen) { l[i] = bl; r[i] = br; }
                else if (wide) { l[i] *= g; r[i] *= g; }
                else
                {
                    // low + (everything else) adds back up to the input exactly; only the top part is turned down
                    l[i] = lowL + (l[i] - lowL) * g;
                    r[i] = lowR + (r[i] - lowR) * g;
                }
                worst = jmin (worst, gainDb);
                feeder.add (history, inPk, jmax (std::abs (l[i]), std::abs (r[i])), gainDb);
            }
            reductionDb.store (-worst);
            bandDb.store (Decibels::gainToDecibels (bandPeak, -100.0f));
        }

    private:
        std::array<SVF, 2> band, split;
        float env = 0.0f, gainDb = 0.0f, lastFreq = -1.0f;
        HistoryFeeder feeder;
    };
}

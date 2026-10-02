#pragma once

#include "PulseDevices.h"
#include "PrismEQ.h"
#include "Loudness.h"

/** Pro dynamics: Pulse Compressor and Multiband Dynamics (the Limiter is in PulseDevices.h).
    Both keep a level history (input, output, gain reduction) for their displays. */
namespace pulse
{
    /** Levels over time for a display: one entry per ~5 ms (written by the audio thread). */
    struct LevelHistory
    {
        static constexpr int size = 600;
        std::array<std::atomic<float>, size> in {}, out {}, gr {};
        std::atomic<int> write { 0 };

        void push (float inDb, float outDb, float grDb) noexcept
        {
            const int w = write.load (std::memory_order_relaxed);
            in[(size_t) w].store (inDb, std::memory_order_relaxed);
            out[(size_t) w].store (outDb, std::memory_order_relaxed);
            gr[(size_t) w].store (grDb, std::memory_order_relaxed);
            write.store ((w + 1) % size, std::memory_order_release);
        }
    };

    /** Gain computer with a soft knee: how many dB to reduce for a level (dB). */
    inline float gainReduction (float levelDb, float threshold, float ratio, float knee)
    {
        const float over = levelDb - threshold;
        const float slope = 1.0f - 1.0f / jmax (1.0f, ratio);
        if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
            return slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return over > 0.0f ? slope * over : 0.0f;
    }

    //==============================================================================
    /** A full-featured compressor: threshold, ratio (to infinity), soft knee, attack, release
        (auto option), lookahead, range, makeup (auto option), dry/wet, stereo link, sidechain
        filters (and listening to the sidechain), external sidechain, and five characters:
        Clean (peak, transparent), Punchy (lets transients through), Vintage (RMS, warm
        saturation), Opto (programme-dependent release) and Bus (feedback, gentle). */
    class ProCompressor : public PulseEffect
    {
    public:
        enum { pThreshold, pRatio, pKnee, pAttack, pRelease, pAutoRelease, pLookahead, pCharacter, pMakeup, pAutoMakeup,
               pRange, pMix, pLink, pScHpf, pScLpf, pScListen, numP };
        static inline const char* xmlTypeName = "pulseCompressor";
        static const char* getPluginName() { return "Pulse Compressor"; }
        static inline const StringArray characterNames { "Clean", "Punchy", "Vintage", "Opto", "Bus" };
        static constexpr double maxLookaheadMs = 10.0;

        explicit ProCompressor (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("threshold", "Threshold", { -60.0f, 0.0f }, -18.0f, fmt::db);
            add ("ratio", "Ratio", skewed (1.0f, 100.0f, 4.0f), 4.0f, [] (float v) { return v >= 99.5f ? String ("inf : 1") : String (v, v < 10.0f ? 1 : 0) + " : 1"; });
            add ("knee", "Knee", { 0.0f, 24.0f }, 6.0f, fmt::db);
            add ("attack", "Attack", skewed (0.01f, 300.0f, 10.0f), 10.0f, fmt::ms);
            add ("release", "Release", skewed (5.0f, 3000.0f, 150.0f), 150.0f, fmt::ms);
            add ("autorelease", "Auto Release", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("lookahead", "Lookahead", { 0.0f, (float) maxLookaheadMs }, 0.0f, fmt::ms);
            add ("character", "Character", steps (characterNames.size()), 0.0f, fmt::choice (characterNames));
            add ("makeup", "Makeup", { -12.0f, 36.0f }, 0.0f, fmt::db);
            add ("automakeup", "Auto Makeup", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("range", "Range", { 0.0f, 60.0f }, 60.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("link", "Stereo Link", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("schpf", "SC Low Cut", skewed (10.0f, 1000.0f, 100.0f), 10.0f, [] (float v) { return v <= 10.5f ? String ("Off") : fmt::hz (v); });
            add ("sclpf", "SC High Cut", skewed (1000.0f, 22000.0f, 6000.0f), 22000.0f, [] (float v) { return v >= 21500.0f ? String ("Off") : fmt::hz (v); });
            add ("sclisten", "SC Listen", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override      { return "Pulse Compressor"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 420; }
        bool wantsSidechain() const override  { return true; }
        double getLatencySeconds() override   { return get (pLookahead) * 0.001; }

        std::vector<Section> getSections() const override
        {
            return { { "Compressor", { pThreshold, pRatio, pKnee, pRange } }, { "Timing", { pAttack, pRelease, pAutoRelease, pLookahead } },
                     { "Character", { pCharacter, pMakeup, pAutoMakeup, pMix, pLink } }, { "Sidechain", { pScHpf, pScLpf, pScListen } } };
        }

        /** The static curve: output level (dB) for an input level, with the current settings. */
        float transfer (float inDb) const
        {
            const float gr = jmin (get (pRange), gainReduction (inDb, get (pThreshold), get (pRatio), get (pKnee)));
            return inDb - gr + makeupDb();
        }

        float makeupDb() const
        {
            float m = get (pMakeup);
            if (getInt (pAutoMakeup) > 0)   // about half of the reduction at -10 dBFS comes back
                m += 0.5f * jmin (get (pRange), gainReduction (-10.0f, get (pThreshold), get (pRatio), get (pKnee)));
            return m;
        }

        LevelHistory history;
        std::atomic<float> gainReductionDb { 0.0f };

    protected:
        void prepare() override
        {
            const int maxLook = (int) std::ceil (maxLookaheadMs * 0.001 * sampleRate) + 2;
            for (auto& d : delay) d.assign ((size_t) maxLook, 0.0f);
            pos = 0;
            for (auto& e : env) e = 0.0f;
            for (auto& g : grSmooth) g = 0.0f;
            for (auto& r : rms) r = 0.0f;
            lastOut[0] = lastOut[1] = 0.0f;
            for (auto& f : scFilters) f.reset();
            lastHp = lastLp = -1.0f;
            histCount = 0; histIn = histOut = -100.0f; histGr = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const int character = getInt (pCharacter);
            const float threshold = get (pThreshold), ratio = get (pRatio), knee = get (pKnee), range = get (pRange);
            const float attackMs = get (pAttack) * (character == 1 ? 2.5f : 1.0f);   // punchy: a little slower to grab
            const float att = std::exp (-1.0f / (float) (jmax (0.01f, attackMs) * 0.001 * sampleRate));
            const float relMs = get (pRelease);
            const bool autoRel = getInt (pAutoRelease) > 0;
            const float link = get (pLink), mix = get (pMix);
            const float makeup = Decibels::decibelsToGain (makeupDb());
            const bool listen = getInt (pScListen) > 0;
            const int look = jlimit (0, (int) delay[0].size() - 1, (int) std::round (get (pLookahead) * 0.001 * sampleRate));
            const int dsize = (int) delay[0].size();
            updateSidechainFilters();
            const bool useHp = get (pScHpf) > 10.5f, useLp = get (pScLpf) < 21500.0f;
            const float* keyL = sidechain != nullptr ? sidechain->getReadPointer (0) : nullptr;
            const float* keyR = sidechain != nullptr ? sidechain->getReadPointer (1) : nullptr;
            const float rmsCoef = std::exp (-1.0f / (float) (0.010 * sampleRate));
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float inL = l[i], inR = r[i];
                // detector input: external sidechain, the output (bus = feedback) or the input
                float kl = keyL != nullptr ? keyL[i] : (character == 4 ? lastOut[0] : inL);
                float kr = keyR != nullptr ? keyR[i] : (character == 4 ? lastOut[1] : inR);
                if (useHp) { kl = scFilters[0].process (kl, 0); kr = scFilters[0].process (kr, 1); }
                if (useLp) { kl = scFilters[1].process (kl, 0); kr = scFilters[1].process (kr, 1); }

                float lvl[2];
                if (character == 2 || character == 3)   // RMS detection (vintage, opto)
                {
                    rms[0] = rmsCoef * rms[0] + (1.0f - rmsCoef) * kl * kl;
                    rms[1] = rmsCoef * rms[1] + (1.0f - rmsCoef) * kr * kr;
                    lvl[0] = Decibels::gainToDecibels (std::sqrt (rms[0]) * 1.4142f, -120.0f);
                    lvl[1] = Decibels::gainToDecibels (std::sqrt (rms[1]) * 1.4142f, -120.0f);
                }
                else
                {
                    lvl[0] = Decibels::gainToDecibels (std::abs (kl), -120.0f);
                    lvl[1] = Decibels::gainToDecibels (std::abs (kr), -120.0f);
                }

                float target[2];
                for (int c = 0; c < 2; ++c) target[c] = jmin (range, gainReduction (lvl[c], threshold, ratio, knee));
                const float linked = jmax (target[0], target[1]);
                for (int c = 0; c < 2; ++c) target[c] = target[c] + (linked - target[c]) * link;

                for (int c = 0; c < 2; ++c)
                {
                    auto& g = grSmooth[(size_t) c];
                    float rel = relMs;
                    if (autoRel || character == 3)   // programme dependent: deep reduction lets go slowly
                        rel = relMs * (0.4f + jmin (2.0f, g / 6.0f));
                    const float relC = std::exp (-1.0f / (float) (jmax (1.0f, rel) * 0.001 * sampleRate));
                    g = target[c] > g ? att * g + (1.0f - att) * target[c] : relC * g + (1.0f - relC) * target[c];
                }

                // the audio, delayed by the lookahead
                const int readPos = (pos - look + dsize) % dsize;
                delay[0][(size_t) pos] = inL;
                delay[1][(size_t) pos] = inR;
                const float dl = delay[0][(size_t) readPos], dr = delay[1][(size_t) readPos];
                pos = (pos + 1) % dsize;

                float ol = dl * Decibels::decibelsToGain (-grSmooth[0]) * makeup;
                float orr = dr * Decibels::decibelsToGain (-grSmooth[1]) * makeup;
                if (character == 2) { ol = std::tanh (ol * 1.2f) / 1.2f; orr = std::tanh (orr * 1.2f) / 1.2f; }   // vintage warmth
                lastOut[0] = ol; lastOut[1] = orr;
                if (listen) { ol = kl; orr = kr; }
                l[i] = dl + (ol - dl) * (listen ? 1.0f : mix);
                r[i] = dr + (orr - dr) * (listen ? 1.0f : mix);

                const float g = jmax (grSmooth[0], grSmooth[1]);
                maxGr = jmax (maxGr, g);
                histIn = jmax (histIn, Decibels::gainToDecibels (jmax (std::abs (dl), std::abs (dr)), -100.0f));
                histOut = jmax (histOut, Decibels::gainToDecibels (jmax (std::abs (l[i]), std::abs (r[i])), -100.0f));
                histGr = jmax (histGr, g);
                if (++histCount >= (int) (sampleRate * 0.005))
                {
                    history.push (histIn, histOut, histGr);
                    histCount = 0; histIn = histOut = -100.0f; histGr = 0.0f;
                }
            }
            gainReductionDb.store (maxGr);
        }

    private:
        void updateSidechainFilters()
        {
            const float hp = get (pScHpf), lp = get (pScLpf);
            if (hp != lastHp) { scFilters[0].set (Biquad::Type::lowCut, sampleRate, hp, 0.707, 0.0); lastHp = hp; }
            if (lp != lastLp) { scFilters[1].set (Biquad::Type::highCut, sampleRate, jmin ((double) lp, sampleRate * 0.45), 0.707, 0.0); lastLp = lp; }
        }

        std::vector<float> delay[2];
        int pos = 0;
        std::array<float, 2> env {}, grSmooth {}, rms {};
        float lastOut[2] {};
        std::array<Biquad, 2> scFilters;
        float lastHp = -1, lastLp = -1;
        int histCount = 0;
        float histIn = -100, histOut = -100, histGr = 0;
    };

    //==============================================================================
    /** Multiband dynamics: up to four bands split by phase-coherent Linkwitz-Riley crossovers
        (the bands sum back to flat). Each band compresses (or expands upwards / downwards) on
        its own: threshold, ratio, attack, release, gain, solo and bypass. */
    class MultibandDynamics : public PulseEffect
    {
    public:
        static constexpr int maxBands = 4;
        enum Band { bOn, bMode, bThreshold, bRatio, bAttack, bRelease, bGain, bSolo, perBand };
        enum Global { gBands, gX1, gX2, gX3, gOutput, gMix, numGlobals };
        static inline const char* xmlTypeName = "pulseMultiband";
        static const char* getPluginName() { return "Multiband Dynamics"; }
        static inline const StringArray modeNames { "Compress", "Expand (down)", "Upward (lift quiet)" };

        explicit MultibandDynamics (te::PluginCreationInfo info) : PulseEffect (info)
        {
            for (int b = 0; b < maxBands; ++b)
            {
                const auto n = String (b + 1), id = "m" + n, nm = "Band " + n + " ";
                add (id + "on", nm + "Active", steps (2), 1.0f, fmt::choice ({ "Bypass", "On" }));
                add (id + "mode", nm + "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
                add (id + "thresh", nm + "Threshold", { -60.0f, 0.0f }, -20.0f, fmt::db);
                add (id + "ratio", nm + "Ratio", skewed (1.0f, 20.0f, 3.0f), 2.0f, [] (float v) { return String (v, 1) + " : 1"; });
                add (id + "attack", nm + "Attack", skewed (0.1f, 300.0f, 15.0f), 10.0f, fmt::ms);
                add (id + "release", nm + "Release", skewed (5.0f, 2000.0f, 150.0f), 120.0f, fmt::ms);
                add (id + "gain", nm + "Gain", { -24.0f, 24.0f }, 0.0f, fmt::db);
                add (id + "solo", nm + "Solo", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            }
            globalBase = numParams();
            add ("bands", "Bands", steps (3), 1.0f, fmt::choice ({ "2 bands", "3 bands", "4 bands" }));
            add ("x1", "Crossover 1", skewed (30.0f, 18000.0f, 500.0f), 120.0f, fmt::hz);
            add ("x2", "Crossover 2", skewed (30.0f, 18000.0f, 500.0f), 1000.0f, fmt::hz);
            add ("x3", "Crossover 3", skewed (30.0f, 18000.0f, 500.0f), 6000.0f, fmt::hz);
            add ("output", "Output", { -24.0f, 24.0f }, 0.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            for (auto& g : bandGr) g.store (0.0f);
        }

        String getName() const override      { return "Multiband Dynamics"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 460; }

        static int idx (int band, int which) { return band * perBand + which; }
        int g (int which) const              { return globalBase + which; }
        int numBands() const                 { return 2 + getInt (g (gBands)); }

        /** The crossovers in use, sorted (each at least a third of an octave above the last). */
        std::array<double, 3> crossovers() const
        {
            std::array<double, 3> x { get (g (gX1)), get (g (gX2)), get (g (gX3)) };
            std::sort (x.begin(), x.end());
            for (int i = 1; i < 3; ++i) x[(size_t) i] = jmax (x[(size_t) i], x[(size_t) i - 1] * 1.26);
            return x;
        }

        std::vector<Section> getSections() const override
        {
            std::vector<Section> s;
            s.push_back ({ "Crossovers", { g (gBands), g (gX1), g (gX2), g (gX3), g (gOutput), g (gMix) } });
            for (int b = 0; b < maxBands; ++b)
            {
                std::vector<int> ps;
                for (int k = 0; k < perBand; ++k) ps.push_back (idx (b, k));
                s.push_back ({ "Band " + String (b + 1), ps });
            }
            return s;
        }

        /** The dB change a band makes for a level (static curve). */
        float bandChange (int b, float levelDb) const
        {
            const float th = get (idx (b, bThreshold)), ratio = get (idx (b, bRatio));
            const float slope = 1.0f - 1.0f / jmax (1.0f, ratio);
            switch (getInt (idx (b, bMode)))
            {
                case 1:  return levelDb < th ? -jmin (40.0f, (th - levelDb) * (ratio - 1.0f)) : 0.0f;   // expander: quiet gets quieter
                case 2:  return levelDb < th ? jmin (24.0f, (th - levelDb) * slope) : 0.0f;              // upward: quiet gets louder
                default: return -gainReduction (levelDb, th, ratio, 6.0f);
            }
        }

        AnalyserFifo post;
        std::array<std::atomic<float>, maxBands> bandGr, bandLevel {};

    protected:
        struct Crossover
        {
            PrismEQ::Stage lp1, lp2, hp1, hp2, ap;
            void design (double sr, double f)
            {
                PrismEQ::Design d;
                PrismEQ::design (d, PrismEQ::highCut, sr, f, 0, 0.7071, 1); lp1 = lp2 = d.st[0];
                PrismEQ::design (d, PrismEQ::lowCut, sr, f, 0, 0.7071, 1);  hp1 = hp2 = d.st[0];
                PrismEQ::design (d, PrismEQ::allPass, sr, f, 0, 0.7071, 0); ap = d.st[0];
            }
        };

        void prepare() override
        {
            lastX = { -1, -1, -1 };
            for (auto& e : env) e = 0.0f;
            for (auto& gs : grState) gs = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const auto x = crossovers();
            if (x != lastX)
            {
                for (int i = 0; i < 3; ++i) xo[(size_t) i].design (sampleRate, x[(size_t) i]);
                // the band-1 / band-2 allpasses need their own state
                for (int i = 0; i < 3; ++i) { apA[(size_t) i] = xo[(size_t) i].ap; apB[(size_t) i] = xo[(size_t) i].ap; }
                lastX = x;
            }
            const int nb = numBands();
            int solo = -1;
            for (int b = 0; b < nb; ++b) if (getInt (idx (b, bSolo)) > 0) { solo = b; break; }
            const float out = Decibels::decibelsToGain (get (g (gOutput))), mix = get (g (gMix));
            float att[maxBands], rel[maxBands], gainLin[maxBands];
            for (int b = 0; b < nb; ++b)
            {
                att[b] = std::exp (-1.0f / (float) (get (idx (b, bAttack)) * 0.001 * sampleRate));
                rel[b] = std::exp (-1.0f / (float) (get (idx (b, bRelease)) * 0.001 * sampleRate));
                gainLin[b] = Decibels::decibelsToGain (get (idx (b, bGain)));
            }
            float maxGr[maxBands] {}, maxLvl[maxBands] { -100, -100, -100, -100 };

            for (int i = 0; i < n; ++i)
            {
                const float in[2] = { l[i], r[i] };
                float band[maxBands][2] {};
                for (int c = 0; c < 2; ++c)
                {
                    // tree: [x1] -> low | rest; rest [x2] -> mid | rest2; rest2 [x3] -> hi-mid | high
                    float s = in[c];
                    float lo = xo[0].lp2.process (xo[0].lp1.process (s, c), c);
                    float rest = xo[0].hp2.process (xo[0].hp1.process (s, c), c);
                    if (nb == 2) { band[0][c] = lo; band[1][c] = rest; continue; }
                    float mid = xo[1].lp2.process (xo[1].lp1.process (rest, c), c);
                    float rest2 = xo[1].hp2.process (xo[1].hp1.process (rest, c), c);
                    lo = apA[1].process (lo, c);   // keep band 1 in phase with what band 2 went through
                    if (nb == 3) { band[0][c] = lo; band[1][c] = mid; band[2][c] = rest2; continue; }
                    band[2][c] = xo[2].lp2.process (xo[2].lp1.process (rest2, c), c);
                    band[3][c] = xo[2].hp2.process (xo[2].hp1.process (rest2, c), c);
                    band[0][c] = apA[2].process (lo, c);
                    band[1][c] = apB[2].process (mid, c);
                }

                float sum[2] {};
                for (int b = 0; b < nb; ++b)
                {
                    const float lvl = Decibels::gainToDecibels (jmax (std::abs (band[b][0]), std::abs (band[b][1])), -120.0f);
                    auto& e = env[(size_t) b];
                    e = lvl > e ? att[b] * e + (1.0f - att[b]) * lvl : rel[b] * e + (1.0f - rel[b]) * lvl;
                    float change = getInt (idx (b, bOn)) > 0 ? bandChange (b, e) : 0.0f;
                    auto& gs = grState[(size_t) b];
                    gs = 0.7f * gs + 0.3f * change;   // de-zipper
                    const float gl = Decibels::decibelsToGain (gs) * gainLin[b];
                    maxGr[b] = std::abs (gs) > std::abs (maxGr[b]) ? gs : maxGr[b];
                    maxLvl[b] = jmax (maxLvl[b], lvl);
                    if (solo >= 0 && b != solo) continue;
                    sum[0] += band[b][0] * gl;
                    sum[1] += band[b][1] * gl;
                }
                l[i] = in[0] + (sum[0] * out - in[0]) * mix;
                r[i] = in[1] + (sum[1] * out - in[1]) * mix;
            }
            for (int b = 0; b < maxBands; ++b)
            {
                bandGr[(size_t) b].store (b < nb ? maxGr[b] : 0.0f);
                bandLevel[(size_t) b].store (b < nb ? maxLvl[b] : -100.0f);
            }
            post.push (l, r, n);
        }

    private:
        int globalBase = 0;
        std::array<Crossover, 3> xo;
        std::array<PrismEQ::Stage, 3> apA, apB;
        std::array<double, 3> lastX { -1, -1, -1 };
        std::array<float, maxBands> env {}, grState {};
    };

    //==============================================================================
    /** Look-ahead brickwall limiter: input gain, ceiling, release (auto option), true-peak
        detection (4x interpolated, catches the peaks between samples), three styles
        (Transparent, Punchy, Loud), stereo link. Shows input / output / gain reduction over time
        and measures the output's loudness (LUFS) and true peak. Reports its look-ahead as latency. */
    class Limiter : public PulseEffect
    {
    public:
        enum { pGain, pCeiling, pRelease, pAutoRelease, pTruePeak, pStyle, pLink };
        static inline const char* xmlTypeName = "pulseLimiter";
        static const char* getPluginName() { return "Limiter"; }
        static constexpr double lookaheadMs = 1.5;
        static inline const StringArray styleNames { "Transparent", "Punchy", "Loud" };

        explicit Limiter (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("gain", "Gain", { 0.0f, 24.0f }, 0.0f, fmt::db);
            add ("ceiling", "Ceiling", { -12.0f, 0.0f }, -0.3f, fmt::db);
            add ("release", "Release", skewed (10.0f, 1000.0f, 100.0f), 100.0f, fmt::ms);
            add ("auto", "Auto Release", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            add ("truepeak", "True Peak", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            add ("style", "Style", steps (styleNames.size()), 0.0f, fmt::choice (styleNames));
            add ("link", "Stereo Link", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            buildInterpolator();
        }

        String getName() const override      { return "Limiter"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 360; }
        double getLatencySeconds() override   { return lookaheadMs / 1000.0; }

        std::vector<Section> getSections() const override
        {
            return { { "Limiter", { pGain, pCeiling, pRelease, pAutoRelease } }, { "Detection", { pTruePeak, pStyle, pLink } } };
        }

        std::atomic<float> gainReductionDb { 0.0f };
        LevelHistory history;
        std::atomic<float> shortTermLufs { -200.0f }, integratedLufs { -200.0f }, truePeakDb { -200.0f };
        std::atomic<bool> resetLoudness { false };

    protected:
        void prepare() override
        {
            look = jmax (1, (int) std::round (lookaheadMs * 0.001 * sampleRate));
            for (auto& d : delay) d.assign ((size_t) look, 0.0f);
            for (auto& nd : need) nd.assign ((size_t) look, 1.0f);
            for (auto& h : hist) h.fill (0.0f);
            pos = 0;
            env[0] = env[1] = 1.0f;
            slow[0] = slow[1] = 1.0f;
            loud.prepare (sampleRate);
            histCount = 0; histIn = histOut = -100.0f; histGr = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float in = Decibels::decibelsToGain (get (pGain));
            const float ceiling = Decibels::decibelsToGain (get (pCeiling));
            const bool autoRel = getInt (pAutoRelease) > 0, truePk = getInt (pTruePeak) > 0;
            const int style = getInt (pStyle);
            const float relScale = style == 1 ? 0.5f : style == 2 ? 0.3f : 1.0f;
            const float rel = std::exp (-1.0f / (float) (get (pRelease) * relScale * 0.001 * sampleRate));
            const float relFast = std::exp (-1.0f / (float) (0.02 * relScale * sampleRate)), relSlow = std::exp (-1.0f / (float) (0.8 * relScale * sampleRate));
            const float link = get (pLink);
            float minGain = 1.0f;
            if (resetLoudness.exchange (false)) loud.reset();

            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i] * in, r[i] * in };
                if (style == 2)   // loud: a soft clip before the limiter takes the edge off
                    for (auto& v : x) v = std::tanh (v * 0.8f) / 0.8f;

                float pk[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& h = hist[(size_t) c];
                    for (int k = taps - 1; k > 0; --k) h[(size_t) k] = h[(size_t) k - 1];
                    h[0] = x[c];
                    float p = std::abs (h[taps / 2]);
                    if (truePk)   // the waveform between samples (4x)
                        for (int ph = 1; ph < 4; ++ph)
                        {
                            float acc = 0;
                            for (int k = 0; k < taps; ++k) acc += h[(size_t) k] * coef[(size_t) ph][(size_t) k];
                            p = jmax (p, std::abs (acc));
                        }
                    pk[c] = jmax (p, std::abs (x[c]));
                }
                const float linked = jmax (pk[0], pk[1]);
                for (int c = 0; c < 2; ++c)
                {
                    const float pc = pk[c] + (linked - pk[c]) * link;
                    need[(size_t) c][(size_t) pos] = pc > ceiling ? ceiling / pc : 1.0f;
                }

                float outS[2];
                for (int c = 0; c < 2; ++c)
                {
                    float target = 1.0f;   // the lowest gain needed anywhere in the look-ahead window
                    for (auto g : need[(size_t) c]) target = jmin (target, g);
                    auto& e = env[c];
                    if (target < e) e = target;   // instant attack (the window gives the warning)
                    else if (autoRel)
                    {
                        slow[c] = slow[c] * relSlow + target * (1.0f - relSlow);
                        const float k = target > slow[c] ? relFast : relSlow;
                        e = jmin (target, e * k + target * (1.0f - k));
                    }
                    else e = e * rel + target * (1.0f - rel);
                    const float d = delay[(size_t) c][(size_t) pos];
                    delay[(size_t) c][(size_t) pos] = x[c];
                    outS[c] = jlimit (-ceiling, ceiling, d * e);   // final safety clip
                    minGain = jmin (minGain, e);
                    histIn = jmax (histIn, Decibels::gainToDecibels (std::abs (d), -100.0f));
                }
                pos = (pos + 1) % look;
                l[i] = outS[0];
                r[i] = outS[1];
                histOut = jmax (histOut, Decibels::gainToDecibels (jmax (std::abs (outS[0]), std::abs (outS[1])), -100.0f));
                histGr = jmax (histGr, -Decibels::gainToDecibels (jmin (env[0], env[1]), -60.0f));
                if (++histCount >= (int) (sampleRate * 0.005))
                {
                    history.push (histIn, histOut, histGr);
                    histCount = 0; histIn = histOut = -100.0f; histGr = 0.0f;
                }
            }
            gainReductionDb.store (-Decibels::gainToDecibels (minGain, -60.0f));
            loud.process (l, r, n);
            shortTermLufs.store ((float) loud.shortTerm());
            integratedLufs.store ((float) loud.integrated());
            truePeakDb.store (loud.truePeakDb());
        }

    private:
        static constexpr int taps = 8;

        /** Windowed-sinc 4x interpolation phases (phase 0 = the sample itself). */
        void buildInterpolator()
        {
            for (int ph = 0; ph < 4; ++ph)
            {
                float sum = 0;
                for (int k = 0; k < taps; ++k)
                {
                    const double t = (k - (taps / 2 - 1)) - ph / 4.0;   // centred between taps 3 and 4
                    const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (MathConstants<double>::pi * t) / (MathConstants<double>::pi * t);
                    const double w = 0.5 + 0.5 * std::cos (MathConstants<double>::pi * t / (taps / 2 + 0.5));
                    coef[(size_t) ph][(size_t) (taps - 1 - k)] = (float) (sinc * w);
                    sum += (float) (sinc * w);
                }
                for (auto& c : coef[(size_t) ph]) c /= sum;
            }
        }

        std::array<std::vector<float>, 2> delay, need;
        std::array<std::array<float, taps>, 2> hist {};
        std::array<std::array<float, taps>, 4> coef {};
        int look = 64, pos = 0;
        float env[2] { 1, 1 }, slow[2] { 1, 1 };
        LoudnessAnalyser loud;
        int histCount = 0;
        float histIn = -100, histOut = -100, histGr = 0;
    };
}

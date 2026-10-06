#pragma once

#include "PulseDevices.h"
#include "LinearPhase.h"

/** Prism EQ: a six-band precision EQ.

    Every band has ten shapes (bell, low / high shelf, low / high cut, notch, band pass, tilt
    shelf, all pass, flat tilt), slopes from 6 to 96 dB/oct, stereo / left / right / mid / side
    placement, and two kinds of movement:
      - dynamic: the band's gain moves (up or down, by "range") when the signal around the band
        goes over a threshold, with attack and release;
      - modulation: an LFO (free or tempo-synced, five shapes) or the band's own envelope moves its
        frequency (+/- octaves) and gain (+/- dB).
    A band can be soloed (you hear just that region). Auto gain keeps the loudness steady, and a
    pre / post analyser feeds the display.

    Phase: "Natural" is the ordinary EQ (no delay; each band turns the phase around its frequency, as analogue
    EQs do). "Linear" changes the level of each frequency and nothing else, at the price of a delay that the
    engine makes up for (and, on sharp sounds, a little ringing before them rather than after). "Linear, deep"
    is four times as long, for narrow bands in the bass. In the linear modes the bands stand still: the dynamic
    and movement settings wait until the phase is Natural again. */
namespace pulse
{
    class PrismEQ : public PulseEffect, private Timer
    {
    public:
        static constexpr int numBands = 6, maxStages = 9;
        enum Band { pOn, pType, pFreq, pGain, pQ, pSlope, pPlace, pDyn, pRange, pThresh, pAttack, pRelease,
                    pModSrc, pModShape, pModRate, pModSync, pModFreq, pModGain, pSolo, perBand };
        enum Type { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf, allPass, flatTilt, numTypes };
        enum Place { stereo, left, right, mid, side };

        static inline const char* xmlTypeName = "pulsePrismEQ";
        static const char* getPluginName() { return "Prism EQ"; }
        static inline const StringArray typeNames  { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt shelf", "All pass", "Flat tilt" };
        static inline const StringArray slopeNames { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
        static inline const int slopeOrders[] = { 1, 2, 3, 4, 5, 6, 8, 12, 16 };
        static inline const StringArray placeNames { "Stereo", "Left", "Right", "Mid", "Side" };
        static inline const StringArray modSources { "Off", "LFO", "Envelope" };
        static inline const StringArray modShapes  { "Sine", "Triangle", "Saw", "Square", "Random" };
        static inline const StringArray syncNames  { "Free (Hz)", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        static inline const double syncBeats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        static inline const StringArray analyserNames { "Off", "Post", "Pre + post" };

        static inline const StringArray phaseNames { "Natural", "Linear", "Linear, deep" };

        enum Global { gOutput, gAutoGain, gMix, gAnalyser, gRange, gPhase, numGlobals };

        explicit PrismEQ (te::PluginCreationInfo info) : PulseEffect (info)
        {
            const float freqs[] = { 30.0f, 100.0f, 400.0f, 1500.0f, 5000.0f, 15000.0f };
            const int types[]   = { lowCut, lowShelf, bell, bell, highShelf, highCut };
            for (int b = 0; b < numBands; ++b)
            {
                const auto n = String (b + 1), id = "b" + n, nm = "Band " + n + " ";
                add (id + "on", nm + "On", steps (2), (b == 2 || b == 3) ? 1.0f : 0.0f, fmt::choice ({ "Off", "On" }));
                add (id + "type", nm + "Type", steps (numTypes), (float) types[b], fmt::choice (typeNames));
                add (id + "freq", nm + "Freq", skewed (10.0f, 30000.0f, 1000.0f), freqs[b], fmt::hz);
                add (id + "gain", nm + "Gain", { -30.0f, 30.0f }, 0.0f, fmt::db);
                add (id + "q", nm + "Q", skewed (0.025f, 40.0f, 1.0f), 1.0f, [] (float v) { return String (v, v < 1.0f ? 2 : 1); });
                add (id + "slope", nm + "Slope", steps (slopeNames.size()), 3.0f, fmt::choice (slopeNames));
                add (id + "place", nm + "Placement", steps (placeNames.size()), 0.0f, fmt::choice (placeNames));
                add (id + "dyn", nm + "Dynamic", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
                add (id + "range", nm + "Dyn Range", { -30.0f, 30.0f }, -6.0f, fmt::db);
                add (id + "thresh", nm + "Threshold", { -80.0f, 0.0f }, -30.0f, fmt::db);
                add (id + "attack", nm + "Attack", skewed (0.1f, 500.0f, 10.0f), 5.0f, fmt::ms);
                add (id + "release", nm + "Release", skewed (5.0f, 3000.0f, 200.0f), 120.0f, fmt::ms);
                add (id + "modsrc", nm + "Movement", steps (modSources.size()), 0.0f, fmt::choice (modSources));
                add (id + "modshape", nm + "LFO Shape", steps (modShapes.size()), 0.0f, fmt::choice (modShapes));
                add (id + "modrate", nm + "LFO Rate", skewed (0.02f, 40.0f, 1.0f), 0.5f, [] (float v) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; });
                add (id + "modsync", nm + "LFO Sync", steps (syncNames.size()), 0.0f, fmt::choice (syncNames));
                add (id + "modfreq", nm + "Move Freq", { -4.0f, 4.0f }, 0.0f, [] (float v) { return (v >= 0 ? "+" : "") + String (v, 2) + " oct"; });
                add (id + "modgain", nm + "Move Gain", { -30.0f, 30.0f }, 0.0f, fmt::db);
                add (id + "solo", nm + "Solo", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            }
            globalBase = numParams();
            add ("output", "Output", { -30.0f, 30.0f }, 0.0f, fmt::db);
            add ("autogain", "Auto Gain", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("analyser", "Analyser", steps (analyserNames.size()), 2.0f, fmt::choice (analyserNames));
            add ("range", "Display Range", steps (4), 2.0f, fmt::choice ({ "3 dB", "6 dB", "12 dB", "30 dB" }));
            add ("phase", "Phase", steps (phaseNames.size()), 0.0f, fmt::choice (phaseNames));
            startTimerHz (8);
            for (int b = 0; b < numBands; ++b) { effFreq[(size_t) b].store (get (idx (b, pFreq))); effGain[(size_t) b].store (get (idx (b, pGain))); }
        }

        ~PrismEQ() override { stopTimer(); }

        String getName() const override      { return "Prism EQ"; }
        double getLatencySeconds() override  { return latencySamples.load() / jmax (1.0, sampleRate); }

        /** 0 = natural, 1 = linear, 2 = linear with four times the length. */
        int phaseMode() const                { return getInt (g (gPhase)); }
        bool isLinear() const                { return phaseMode() > 0; }

        /** Gets the linear-phase filter ready for the mode chosen (it is built away from the audio, and swapped
            in). Returns true when the delay through the EQ has changed. The EQ calls this itself a few times a
            second; a test calls it after changing the mode. */
        bool syncPhaseMode()
        {
            const ScopedLock syncing (syncLock);
            const int mode = phaseMode();
            const int order = mode == 0 ? 0 : (mode == 1 ? 13 : 15) + (sampleRate > 60000.0 ? 1 : 0) + (sampleRate > 120000.0 ? 1 : 0);
            if (order == firOrder) return false;
            std::unique_ptr<LinearFir> made;
            StepDelay delay;
            std::vector<float> cosines;
            if (order > 0)
            {
                made = std::make_unique<LinearFir>();
                made->prepare (order);
                delay.prepare (made->latency());
                cosines.resize ((size_t) made->bins());
                for (int k = 0; k < made->bins(); ++k) cosines[(size_t) k] = (float) std::cos (MathConstants<double>::pi * k / (made->bins() - 1));
            }
            {
                const ScopedLock sl (firLock);
                fir = std::move (made);
                dryDelay = std::move (delay);
                cosTable = std::move (cosines);
                for (auto& v : groups) v.assign (fir != nullptr ? (size_t) fir->bins() : 0, 1.0);
                for (auto& v : shapes) v.assign (fir != nullptr ? (size_t) fir->bins() : 0, 1.0f);
                firOrder = order;
                firHash = -1.0;
                latencySamples = fir != nullptr ? fir->latency() : 0;
            }
            return true;
        }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 560; }

        static int idx (int band, int which) { return band * perBand + which; }
        int g (int which) const              { return globalBase + which; }

        std::vector<Section> getSections() const override
        {
            std::vector<Section> s;
            for (int b = 0; b < numBands; ++b)
            {
                std::vector<int> ps;
                for (int k = 0; k < perBand; ++k) ps.push_back (idx (b, k));
                s.push_back ({ "Band " + String (b + 1), ps });
            }
            s.push_back ({ "Output", { g (gOutput), g (gAutoGain), g (gMix), g (gAnalyser), g (gRange), g (gPhase) } });
            return s;
        }

        //==============================================================================
        /** One filter section (bilinear / RBJ), with its own two-channel state. */
        struct Stage
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            double z1[2] {}, z2[2] {};

            inline float process (float x, int ch) noexcept
            {
                const double y = b0 * x + z1[ch];
                z1[ch] = b1 * x - a1 * y + z2[ch];
                z2[ch] = b2 * x - a2 * y;
                return (float) y;
            }
            void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
            std::complex<double> response (double f, double sr) const
            {
                const auto z = std::polar (1.0, -MathConstants<double>::twoPi * f / sr);
                return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
            }
            void setCoeffs (double nb0, double nb1, double nb2, double a0, double na1, double na2)
            {
                b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
            }
        };

        struct Design
        {
            std::array<Stage, maxStages> st;
            int n = 0;
            double scale = 1.0;   // a flat gain (flat tilt normalisation)
            Stage& next() { return st[(size_t) jmin (n++, maxStages - 1)]; }
            double magnitude (double f, double sr) const
            {
                std::complex<double> h (scale, 0.0);
                for (int i = 0; i < n; ++i) h *= st[(size_t) i].response (f, sr);
                return std::abs (h);
            }
            /** The same, squared, from the cosine of the frequency's angle alone (quick enough to do for every
                point of a linear-phase filter). */
            double magnitudeSquared (double c1) const
            {
                const double c2 = 2.0 * c1 * c1 - 1.0;
                double m = scale * scale;
                for (int i = 0; i < n; ++i)
                {
                    auto& s = st[(size_t) i];
                    const double num = s.b0 * s.b0 + s.b1 * s.b1 + s.b2 * s.b2 + 2.0 * (s.b0 * s.b1 + s.b1 * s.b2) * c1 + 2.0 * s.b0 * s.b2 * c2;
                    const double den = 1.0 + s.a1 * s.a1 + s.a2 * s.a2 + 2.0 * (s.a1 + s.a1 * s.a2) * c1 + 2.0 * s.a2 * c2;
                    m *= jmax (0.0, num) / jmax (1.0e-30, den);
                }
                return m;
            }
        };

        static bool typeUsesGain (int t)  { return t == bell || t == lowShelf || t == highShelf || t == tiltShelf || t == flatTilt; }
        static bool typeUsesSlope (int t) { return t == lowCut || t == highCut || t == lowShelf || t == highShelf || t == bandPass || t == tiltShelf; }
        static bool typeUsesQ (int t)     { return t != flatTilt; }

        /** Designs one band's sections for the given frequency / gain / Q / slope. */
        static void design (Design& d, int type, double sr, double f, double gainDb, double q, int slopeIndex)
        {
            d.n = 0;
            d.scale = 1.0;
            f = jlimit (5.0, sr * 0.495, f);
            q = jlimit (0.025, 40.0, q);
            const int order = slopeOrders[jlimit (0, 8, slopeIndex)];
            const double w = MathConstants<double>::twoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
            const double K = std::tan (w * 0.5);
            auto rbj = [&] (Stage& s, int kind, double Q, double gDb)
            {
                const double A = std::pow (10.0, gDb / 40.0), alpha = sw / (2.0 * Q);
                switch (kind)
                {
                    case 0: s.setCoeffs (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A); break;               // peak
                    case 1: s.setCoeffs ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha); break;                        // high pass
                    case 2: s.setCoeffs ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha); break;                           // low pass
                    case 3: s.setCoeffs (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha); break;                                             // band pass
                    case 4: s.setCoeffs (1, -2 * cw, 1, 1 + alpha, -2 * cw, 1 - alpha); break;                                                // notch
                    case 5: s.setCoeffs (1 - alpha, -2 * cw, 1 + alpha, 1 + alpha, -2 * cw, 1 - alpha); break;                                // all pass
                    case 6: { const double s2 = 2 * std::sqrt (A) * alpha;                                                                     // low shelf
                              s.setCoeffs (A * ((A + 1) - (A - 1) * cw + s2), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s2),
                                           (A + 1) + (A - 1) * cw + s2, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s2); break; }
                    case 7: { const double s2 = 2 * std::sqrt (A) * alpha;                                                                     // high shelf
                              s.setCoeffs (A * ((A + 1) + (A - 1) * cw + s2), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s2),
                                           (A + 1) - (A - 1) * cw + s2, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s2); break; }
                    default: break;
                }
            };
            auto first = [&] (Stage& s, int kind, double gDb, double fK)
            {
                const double V = Decibels::decibelsToGain (gDb);
                s.b2 = 0; s.a2 = 0;
                switch (kind)
                {
                    case 0: s.b0 = fK / (fK + 1); s.b1 = s.b0; s.a1 = (fK - 1) / (fK + 1); break;                                // low pass
                    case 1: s.b0 = 1 / (fK + 1); s.b1 = -s.b0; s.a1 = (fK - 1) / (fK + 1); break;                                // high pass
                    case 2: s.b0 = (1 + V * fK) / (1 + fK); s.b1 = (V * fK - 1) / (1 + fK); s.a1 = (fK - 1) / (1 + fK); break;   // low shelf (gain V at DC)
                    case 3: s.b0 = (V + fK) / (1 + fK); s.b1 = (fK - V) / (1 + fK); s.a1 = (fK - 1) / (1 + fK); break;           // high shelf (gain V at Nyquist)
                    default: break;
                }
            };
            // Butterworth cascade for an order-N cut; the user's Q shapes the sharpest section
            auto butterworth = [&] (bool high)
            {
                if (order % 2 == 1) first (d.next(), high ? 1 : 0, 0.0, K);
                const int pairs = order / 2;
                for (int k = 0; k < pairs; ++k)
                {
                    double qk = 1.0 / (2.0 * std::cos ((2.0 * (k + 1) - 1.0) * MathConstants<double>::pi / (2.0 * order)));
                    if (order % 2 == 1) qk = 1.0 / (2.0 * std::cos ((double) (k + 1) * MathConstants<double>::pi / order));
                    if (k == pairs - 1) qk *= q / 0.7071;   // resonance on the last (highest-Q) section
                    rbj (d.next(), high ? 1 : 2, qk, 0.0);
                }
            };

            switch (type)
            {
                case bell:      rbj (d.next(), 0, q, gainDb); break;
                case notch:     rbj (d.next(), 4, q, 0.0); break;
                case allPass:   rbj (d.next(), 5, q, 0.0); break;
                case lowCut:    butterworth (true); break;
                case highCut:   butterworth (false); break;
                case bandPass:
                {
                    const int stages = jmax (1, order / 2);
                    const double qs = q * std::sqrt ((double) stages);   // keep the bandwidth similar as the slope steepens
                    for (int k = 0; k < stages; ++k) rbj (d.next(), 3, qs, 0.0);
                    break;
                }
                case lowShelf:
                case highShelf:
                {
                    const bool low = type == lowShelf;
                    if (order == 1) { first (d.next(), low ? 2 : 3, gainDb, K); break; }
                    const int stages = jmax (1, order / 2);   // steeper slopes stack shelves, sharing the gain
                    for (int k = 0; k < stages; ++k) rbj (d.next(), low ? 6 : 7, q * 0.7071, gainDb / stages);
                    break;
                }
                case tiltShelf:
                {
                    // low shelf down + high shelf up around the same frequency: 0 dB at f
                    if (order == 1) { first (d.next(), 2, -gainDb * 0.5, K); first (d.next(), 3, gainDb * 0.5, K); }
                    else            { rbj (d.next(), 6, q * 0.7071, -gainDb * 0.5); rbj (d.next(), 7, q * 0.7071, gainDb * 0.5); }
                    break;
                }
                case flatTilt:
                {
                    // a straight line in dB / octave over the audio range, pivoting at f: four gentle
                    // first-order tilts spread over 20 Hz - 20 kHz, normalised to 0 dB at f
                    const double per = gainDb * 2.0 / 4.0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const double fk = 20.0 * std::pow (1000.0, (k + 0.5) / 4.0);
                        const double Kk = std::tan (MathConstants<double>::pi * jmin (fk, sr * 0.45) / sr);
                        first (d.next(), 2, -per * 0.5, Kk);
                        first (d.next(), 3, per * 0.5, Kk);
                    }
                    d.scale = 1.0;
                    d.scale = 1.0 / jmax (1.0e-6, d.magnitude (f, sr));
                    break;
                }
                default: break;
            }
        }

        //==============================================================================
        bool isBandOn (int b) const { return getInt (idx (b, pOn)) > 0; }
        int bandType (int b) const  { return getInt (idx (b, pType)); }

        /** The band's static response (its settings, no movement) at frequency f, in dB. */
        double bandDb (int b, double f, bool live = false) const
        {
            Design d;
            const double fr = live ? (double) effFreq[(size_t) b].load() : (double) get (idx (b, pFreq));
            const double gn = live ? (double) effGain[(size_t) b].load() : (double) get (idx (b, pGain));
            design (d, bandType (b), 48000.0, fr, gn, get (idx (b, pQ)), getInt (idx (b, pSlope)));
            return Decibels::gainToDecibels (d.magnitude (f, 48000.0), -120.0);
        }

        /** The whole EQ at frequency f in dB (live = with the bands' current movement). */
        double responseDb (double f, bool live = false) const
        {
            double db = 0.0;
            for (int b = 0; b < numBands; ++b)
                if (isBandOn (b))
                {
                    const int pl = getInt (idx (b, pPlace));
                    const double bd = bandDb (b, f, live && bandMoves (b) && isProcessing() && ! isLinear());
                    db += (pl == left || pl == right || pl == side) ? bd * 0.5 : bd;   // drawn as its share of the stereo sum
                }
            return db + get (g (gOutput)) + (getInt (g (gAutoGain)) > 0 ? autoGainDb.load() : 0.0);
        }

        bool bandMoves (int b) const { return getInt (idx (b, pDyn)) > 0 || getInt (idx (b, pModSrc)) > 0; }

        /** True while audio runs through the EQ (the live values are fresh). */
        bool isProcessing() const { return Time::getMillisecondCounter() - lastProcessMs.load() < 400; }
        std::atomic<uint32> lastProcessMs { 0 };
        int soloBand() const
        {
            for (int b = 0; b < numBands; ++b) if (isBandOn (b) && getInt (idx (b, pSolo)) > 0) return b;
            return -1;
        }

        /** Live state for the display. */
        std::array<std::atomic<float>, numBands> effFreq, effGain, dynGain {}, bandLevel {};
        std::atomic<float> autoGainDb { 0.0f };
        AnalyserFifo pre, post;

    protected:
        void prepare() override
        {
            for (auto& bs : bands) { for (auto& s : bs.d.st) s.reset(); bs.env = 0.0; bs.phase = 0.0; bs.detector.reset(); bs.random = 0.0; bs.lastHash = -1; }
            soloStage.reset();
            lastAutoHash = -1;
            { const ScopedLock syncing (syncLock); const ScopedLock sl (firLock); firOrder = -1; }   // (the sample rate may have changed)
            syncPhaseMode();
        }

        void timerCallback() override
        {
            if (syncPhaseMode()) edit.restartPlayback();   // the delay to make up for is different now
        }

        /** The linear-phase way: the bands' own settings (no movement) become one filter. */
        void processLinear (float* l, float* r, int n, float outGain, float mix)
        {
            for (int b = 0; b < numBands; ++b)
            {
                effFreq[(size_t) b].store (get (idx (b, pFreq)));
                effGain[(size_t) b].store (get (idx (b, pGain)));
                dynGain[(size_t) b].store (0.0f);
                bandLevel[(size_t) b].store (-100.0f);
            }
            constexpr int chunk = 64;
            float dl[chunk], dr[chunk];
            for (int start = 0; start < n; start += chunk)
            {
                const int len = jmin (chunk, n - start);
                for (int i = 0; i < len; ++i) { dl[i] = l[start + i]; dr[i] = r[start + i]; dryDelay.process (dl[i], dr[i]); }
                fir->process (l + start, r + start, len, [this] { designLinear(); });
                for (int i = 0; i < len; ++i)
                {
                    l[start + i] = dl[i] + (l[start + i] * outGain - dl[i]) * mix;
                    r[start + i] = dr[i] + (r[start + i] * outGain - dr[i]) * mix;
                }
            }
        }

        /** Works the filter out again when a band has changed (looked at once per block of the filter). */
        void designLinear()
        {
            const int solo = soloBand();
            double hash = solo * 31.7;
            for (int b = 0; b < numBands; ++b)
                for (int k : { pOn, pType, pFreq, pGain, pQ, pSlope, pPlace })
                    hash = hash * 1.000171 + get (idx (b, k)) * (k + 3) * (b + 1);
            if (hash == firHash) return;
            firHash = hash;
            const int bins = fir->bins();
            enum { all, onlyLeft, onlyRight, onlyMid, onlySide };
            for (auto& v : groups) std::fill (v.begin(), v.end(), 1.0);
            bool anyLR = false, anyMS = false;
            for (int b = 0; b < numBands; ++b)
            {
                if (solo >= 0 ? b != solo : ! isBandOn (b)) continue;
                const int type = bandType (b);
                Design d;
                if (solo >= 0)
                {
                    const bool cut = type == lowCut || type == highCut;
                    design (d, cut ? type : bandPass, sampleRate, get (idx (b, pFreq)), 0.0, cut ? get (idx (b, pQ)) : jmax (0.3f, get (idx (b, pQ))), cut ? getInt (idx (b, pSlope)) : 1);
                }
                else
                    design (d, type, sampleRate, get (idx (b, pFreq)), get (idx (b, pGain)), get (idx (b, pQ)), getInt (idx (b, pSlope)));
                const int place = solo >= 0 ? (int) stereo : getInt (idx (b, pPlace));
                const int grp = place == left ? onlyLeft : place == right ? onlyRight : place == mid ? onlyMid : place == side ? onlySide : all;
                anyLR = anyLR || grp == onlyLeft || grp == onlyRight;
                anyMS = anyMS || grp == onlyMid || grp == onlySide;
                auto& into = groups[(size_t) grp];
                for (int k = 0; k < bins; ++k) into[(size_t) k] *= d.magnitudeSquared (cosTable[(size_t) k]);
            }
            for (int k = 0; k < bins; ++k)
            {
                const size_t i = (size_t) k;
                const double st = std::sqrt (groups[all][i]), lh = std::sqrt (groups[onlyLeft][i]), rh = std::sqrt (groups[onlyRight][i]);
                const double mh = std::sqrt (groups[onlyMid][i]), sh = std::sqrt (groups[onlySide][i]);
                const double a = 0.5 * (mh + sh), bx = 0.5 * (mh - sh);
                shapes[0][i] = (float) jmin (64.0, a * st * lh);
                shapes[1][i] = (float) jmin (64.0, a * st * rh);
                shapes[2][i] = (float) jlimit (-64.0, 64.0, bx * st * rh);   // what of the right goes to the left
                shapes[3][i] = (float) jlimit (-64.0, 64.0, bx * st * lh);
            }
            fir->setResponse (shapes[0].data(), anyLR ? shapes[1].data() : shapes[0].data(), anyMS ? shapes[2].data() : nullptr, anyMS ? shapes[3].data() : nullptr);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            lastProcessMs.store (Time::getMillisecondCounter());
            const int analyser = getInt (g (gAnalyser));
            if (analyser == 2) pre.push (l, r, n);
            updateAutoGain();

            const double bpm = edit.tempoSequence.getInternalSequence().getBeatsPerSecondAt (te::TimePosition()).v * 60.0;   // audio-thread safe
            const int solo = soloBand();
            const float mix = get (g (gMix));
            const float outGain = Decibels::decibelsToGain (get (g (gOutput)) + (getInt (g (gAutoGain)) > 0 ? autoGainDb.load() : 0.0f));
            constexpr int sub = 32;

            if (isLinear())
            {
                const ScopedTryLock sl (firLock);
                if (sl.isLocked() && fir != nullptr && firOrder > 0)
                {
                    processLinear (l, r, n, outGain, mix);
                    if (analyser >= 1) post.push (l, r, n);
                    return;
                }
            }

            for (int start = 0; start < n; start += sub)
            {
                const int len = jmin (sub, n - start);
                // per sub-block: movement + filter design
                for (int b = 0; b < numBands; ++b) updateBand (b, l + start, r + start, len, bpm);

                for (int i = start; i < start + len; ++i)
                {
                    const float dryL = l[i], dryR = r[i];
                    float L = dryL, R = dryR;
                    if (solo >= 0)
                    {
                        L = soloStage.process (L, 0);
                        R = soloStage.process (R, 1);
                    }
                    else
                    {
                        for (int b = 0; b < numBands; ++b)
                        {
                            auto& bs = bands[(size_t) b];
                            if (! bs.on) continue;
                            switch (bs.place)
                            {
                                case left:  for (int k = 0; k < bs.d.n; ++k) L = bs.d.st[(size_t) k].process (L, 0); L *= (float) bs.d.scale; break;
                                case right: for (int k = 0; k < bs.d.n; ++k) R = bs.d.st[(size_t) k].process (R, 1); R *= (float) bs.d.scale; break;
                                case mid:
                                case side:
                                {
                                    float m = 0.5f * (L + R), s = 0.5f * (L - R);
                                    float& x = bs.place == mid ? m : s;
                                    for (int k = 0; k < bs.d.n; ++k) x = bs.d.st[(size_t) k].process (x, 0);
                                    x *= (float) bs.d.scale;
                                    L = m + s; R = m - s;
                                    break;
                                }
                                default:
                                    for (int k = 0; k < bs.d.n; ++k) { L = bs.d.st[(size_t) k].process (L, 0); R = bs.d.st[(size_t) k].process (R, 1); }
                                    L *= (float) bs.d.scale; R *= (float) bs.d.scale;
                                    break;
                            }
                        }
                    }
                    L *= outGain; R *= outGain;
                    l[i] = dryL + (L - dryL) * mix;
                    r[i] = dryR + (R - dryR) * mix;
                }
            }
            if (analyser >= 1) post.push (l, r, n);
        }

    private:
        struct BandState
        {
            Design d;
            Stage detector;     // band pass around the band, for dynamics and the envelope source
            double env = 0.0, phase = 0.0, random = 0.0, lastHash = -1;
            int type = -1, place = 0;
            bool on = false;
        };

        void updateBand (int b, const float* l, const float* r, int len, double bpm)
        {
            auto& bs = bands[(size_t) b];
            bs.on = isBandOn (b);
            bs.place = getInt (idx (b, pPlace));
            if (! bs.on) { bandLevel[(size_t) b].store (-100.0f); return; }

            const int type = bandType (b);
            if (type != bs.type) { for (auto& s : bs.d.st) s.reset(); bs.type = type; }
            double f = get (idx (b, pFreq)), gn = get (idx (b, pGain));
            const double q = get (idx (b, pQ));

            // detector: the level around the band (mid signal, before this EQ)
            const bool dyn = getInt (idx (b, pDyn)) > 0;
            const int src = getInt (idx (b, pModSrc));
            double levelDb = -100.0;
            if (dyn || src == 2)
            {
                Design det; design (det, bandPass, sampleRate, f, 0.0, jmax (0.5, q), 1);
                bs.detector.b0 = det.st[0].b0; bs.detector.b1 = det.st[0].b1; bs.detector.b2 = det.st[0].b2;
                bs.detector.a1 = det.st[0].a1; bs.detector.a2 = det.st[0].a2;
                const double att = std::exp (-1.0 / (sampleRate * get (idx (b, pAttack)) * 0.001));
                const double rel = std::exp (-1.0 / (sampleRate * get (idx (b, pRelease)) * 0.001));
                for (int i = 0; i < len; ++i)
                {
                    const double x = std::abs (bs.detector.process (0.5f * (l[i] + r[i]), 0));
                    bs.env = x > bs.env ? att * bs.env + (1 - att) * x : rel * bs.env + (1 - rel) * x;
                }
                levelDb = Decibels::gainToDecibels (bs.env * 1.4142, -100.0);
            }
            bandLevel[(size_t) b].store ((float) levelDb);

            // modulation
            double m = 0.0;
            if (src == 1)
            {
                const int sync = getInt (idx (b, pModSync));
                const double hz = sync > 0 ? bpm / 60.0 / syncBeats[sync] : (double) get (idx (b, pModRate));
                bs.phase += hz * len / sampleRate;
                if (bs.phase >= 1.0) { bs.phase -= std::floor (bs.phase); bs.random = rng.nextDouble() * 2.0 - 1.0; }
                const double ph = bs.phase;
                switch (getInt (idx (b, pModShape)))
                {
                    case 1:  m = 1.0 - 4.0 * std::abs (ph - 0.5); break;
                    case 2:  m = 2.0 * ph - 1.0; break;
                    case 3:  m = ph < 0.5 ? 1.0 : -1.0; break;
                    case 4:  m = bs.random; break;
                    default: m = std::sin (MathConstants<double>::twoPi * ph); break;
                }
            }
            else if (src == 2)
                m = jlimit (0.0, 1.0, (levelDb + 60.0) / 60.0);

            f *= std::pow (2.0, m * get (idx (b, pModFreq)));
            gn += m * get (idx (b, pModGain));

            // dynamics: gain moves by up to "range" as the band's level goes over the threshold
            double dg = 0.0;
            if (dyn && typeUsesGain (type))
            {
                const double over = jmax (0.0, levelDb - get (idx (b, pThresh)));
                dg = get (idx (b, pRange)) * (1.0 - std::exp (-over / 6.0));
            }
            gn += dg;
            f = jlimit (10.0, 30000.0, f);
            effFreq[(size_t) b].store ((float) f);
            effGain[(size_t) b].store ((float) gn);
            dynGain[(size_t) b].store ((float) dg);

            const double hash = f * 1.31 + gn * 7.7 + q * 13.1 + type * 101.0 + getInt (idx (b, pSlope)) * 977.0;
            if (hash != bs.lastHash)
            {
                bs.lastHash = hash;
                const int prevN = bs.d.n;
                design (bs.d, type, sampleRate, f, gn, q, getInt (idx (b, pSlope)));
                if (bs.d.n != prevN) for (auto& s : bs.d.st) s.reset();
            }

            if (getInt (idx (b, pSolo)) > 0 && soloBand() == b)
            {
                Design sd;
                const bool cut = type == lowCut || type == highCut;
                design (sd, cut ? type : bandPass, sampleRate, f, 0.0, cut ? q : jmax (0.3, q), cut ? getInt (idx (b, pSlope)) : 1);
                soloStage.b0 = sd.st[0].b0; soloStage.b1 = sd.st[0].b1; soloStage.b2 = sd.st[0].b2; soloStage.a1 = sd.st[0].a1; soloStage.a2 = sd.st[0].a2;
            }
        }

        /** Auto gain: the opposite of the EQ's average change across the spectrum. */
        void updateAutoGain()
        {
            if (getInt (g (gAutoGain)) == 0) return;
            double hash = 0;
            for (int i = 0; i < globalBase; ++i) hash = hash * 1.000173 + get (i) * (i % 7 + 1);
            if (hash == lastAutoHash) return;
            lastAutoHash = hash;
            double sum = 0, wsum = 0;
            for (int k = 0; k < 48; ++k)
            {
                const double f = 30.0 * std::pow (500.0, k / 47.0);
                const double w = f < 200.0 ? 0.6 : (f > 8000.0 ? 0.5 : 1.0);   // the mids count most
                double db = 0;
                for (int b = 0; b < numBands; ++b) if (isBandOn (b)) db += bandDb (b, f);
                sum += jlimit (-30.0, 30.0, db) * w;
                wsum += w;
            }
            autoGainDb.store ((float) jlimit (-24.0, 24.0, -sum / jmax (1.0, wsum) * 0.8));
        }

        int globalBase = 0;
        std::array<BandState, numBands> bands;
        Stage soloStage;
        Random rng;
        double lastAutoHash = -1;

        // linear phase
        CriticalSection firLock, syncLock;
        std::unique_ptr<LinearFir> fir;
        StepDelay dryDelay;
        std::vector<float> cosTable;
        std::array<std::vector<double>, 5> groups;
        std::array<std::vector<float>, 4> shapes;
        int firOrder = -1;
        double firHash = -1.0;
        std::atomic<int> latencySamples { 0 };
    };
}

#pragma once

#include "ColourFx.h"

/** Pulse Amp: a guitar and bass amplifier with its speaker cabinet in one device. */
namespace pulse
{
    class Amp : public PulseEffect
    {
    public:
        enum { pModel, pGain, pBass, pMid, pTreble, pPresence, pCabinet, pMic, pLevel, pMix };
        static inline const char* xmlTypeName = "pulseAmp";
        static const char* getPluginName() { return "Pulse Amp"; }
        static inline const StringArray modelNames { "Clean", "Crunch", "Lead", "Heavy", "Bass" };
        static inline const StringArray cabNames { "None", "1x12 open", "2x12", "4x12 closed", "Bass 1x15" };

        /** What makes each amplifier what it is. */
        struct Model
        {
            float preGain;      // how hard the first stage is hit at full Gain (dB)
            float tightHz;      // lows taken out before the distortion (keeps it from going flabby)
            float asym;         // how lopsided the valves clip (even harmonics)
            int stages;         // clipping stages in a row
            float toneHz;       // where the mid control sits
            float sag;          // how much loud playing pulls the gain down for a moment
        };
        static constexpr Model models[] = {
            { 18.0f, 60.0f,  0.10f, 1, 650.0f, 0.10f },    // Clean
            { 34.0f, 110.0f, 0.25f, 2, 700.0f, 0.25f },    // Crunch
            { 48.0f, 160.0f, 0.20f, 3, 800.0f, 0.20f },    // Lead
            { 60.0f, 220.0f, 0.10f, 4, 550.0f, 0.10f },    // Heavy
            { 30.0f, 30.0f,  0.20f, 2, 450.0f, 0.30f } };  // Bass

        /** A cabinet as the filters that make its sound: where its lows and highs stop, its box resonance and its upper-mid bite. */
        struct Cabinet { float lowHz, highHz, thumpHz, thumpDb, biteHz, biteDb; };
        static constexpr Cabinet cabinets[] = {
            { 20.0f,  20000.0f, 100.0f, 0.0f, 2500.0f, 0.0f },    // None
            { 95.0f,  5200.0f,  130.0f, 3.0f, 2800.0f, 4.0f },    // 1x12 open
            { 80.0f,  5000.0f,  110.0f, 4.0f, 2400.0f, 3.5f },    // 2x12
            { 70.0f,  4400.0f,  95.0f,  5.5f, 3100.0f, 5.0f },    // 4x12 closed
            { 40.0f,  3200.0f,  70.0f,  5.0f, 1100.0f, 2.5f } };  // Bass 1x15

        explicit Amp (te::PluginCreationInfo info) : PulseEffect (info)
        {
            auto tone = [] (float v) { return String (v * 10.0f, 1); };
            add ("model", "Amp", steps (modelNames.size()), 1.0f, fmt::choice (modelNames));
            add ("gain", "Gain", { 0.0f, 1.0f }, 0.5f, tone);
            add ("bass", "Bass", { 0.0f, 1.0f }, 0.5f, tone);
            add ("mid", "Middle", { 0.0f, 1.0f }, 0.5f, tone);
            add ("treble", "Treble", { 0.0f, 1.0f }, 0.5f, tone);
            add ("presence", "Presence", { 0.0f, 1.0f }, 0.5f, tone);
            add ("cabinet", "Cabinet", steps (cabNames.size()), 2.0f, fmt::choice (cabNames));
            add ("mic", "Microphone", { 0.0f, 1.0f }, 0.3f, [] (float v) { return v < 0.05f ? String ("Centre") : v > 0.95f ? String ("Edge") : String (roundToInt (v * 100)) + "% out"; });
            add ("level", "Level", { -24.0f, 12.0f }, 0.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override       { return "Pulse Amp"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        double getLatencySeconds() override    { return latencySamples / sampleRate; }
        std::vector<Section> getSections() const override
        {
            return { { "Amp", { pModel, pGain } }, { "Tone", { pBass, pMid, pTreble, pPresence } }, { "Cabinet", { pCabinet, pMic } }, { "Output", { pLevel, pMix } } };
        }

        /** One valve stage: soft on one side, harder on the other. */
        static float valve (float x, float asym) noexcept
        {
            const float y = x >= 0.0f ? std::tanh (x) : std::tanh (x * (1.0f + asym * 2.0f)) / (1.0f + asym * 2.0f);
            return y;
        }

        /** The tone controls and the cabinet as one curve (gain at f), for the display. Built from the same filters the sound goes through. */
        float toneResponse (double f) const
        {
            std::array<Biquad, 9> fs;
            setFilters (fs.data(), 96000.0);
            double m = 1.0;
            for (auto& b : fs) m *= b.magnitude (f, 96000.0);
            return (float) m;
        }

        std::atomic<float> driveNow { 0.0f };   // how far into the clipping the loudest moments go (0..1), for the display

    protected:
        void prepare() override
        {
            for (auto& ch : filters) for (auto& b : ch) b.reset();
            for (auto& ch : tight) ch.reset();
            for (auto& ch : dcIn) ch = 0.0f;
            for (auto& ch : up) ch = {};
            for (auto& ch : dryLine) for (auto& v : ch) v = 0.0f;
            dryPos = 0;
            sagEnv = 0.0f;
            lastSig = -1.0f;
            sGain.reset(); sLevel.reset();
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const auto& m = models[jlimit (0, 4, getInt (pModel))];
            const float mix = get (pMix);
            // the filters only change when a control does
            const float sig = get (pBass) + 3.0f * get (pMid) + 7.0f * get (pTreble) + 13.0f * get (pPresence) + 29.0f * get (pMic) + 61.0f * getInt (pCabinet) + 127.0f * getInt (pModel);
            if (sig != lastSig)
            {
                for (auto& ch : filters) setFilters (ch.data(), sampleRate);
                for (auto& t : tight) t.set (Biquad::Type::lowCut, sampleRate * 2.0, m.tightHz, 0.707, 0.0);
                lastSig = sig;
            }
            const float a = std::exp (-1.0f / (float) (0.02 * sampleRate));
            const float sagUp = std::exp (-1.0f / (float) (0.005 * sampleRate)), sagDown = std::exp (-1.0f / (float) (0.12 * sampleRate));
            const float dcA = (float) (MathConstants<double>::twoPi * 12.0 / (sampleRate * 2.0));
            float* d[2] = { l, r };
            float hardest = 0.0f;
            const float stageGain = std::pow (2.2f, (float) (m.stages - 1));

            for (int i = 0; i < n; ++i)
            {
                // Gain is not a volume: quiet at 0, the model's full drive at 10, on a curve that leaves room in the middle
                const float gainDb = sGain.next (-6.0f + m.preGain * std::pow (get (pGain), 1.3f), a);
                const float level = sLevel.next (Decibels::decibelsToGain (get (pLevel)), a);
                const float pk = jmax (std::abs (l[i]), std::abs (r[i]));
                sagEnv = pk > sagEnv ? pk + (sagEnv - pk) * sagUp : pk + (sagEnv - pk) * sagDown;
                const float drive = Decibels::decibelsToGain (gainDb) / (1.0f + m.sag * 4.0f * jmin (1.0f, sagEnv));
                // Gain changes the sound more than the volume: gentle settings come out about as loud as they went in,
                // and once the valves are clipping the level stays at about -8 dB however hard they are pushed
                const float makeup = 0.4f * jmax (1.0f, 2.5f / (drive * stageGain));

                for (int ch = 0; ch < 2; ++ch)
                {
                    // the untouched sound is made as late as the amp's own path, so that a part-wet mix stays in step
                    const float dry = dryLine[ch][dryPos];
                    dryLine[ch][dryPos] = d[ch][i];
                    const float in = d[ch][i];
                    // the valves work at twice the sample rate, so that the harmonics they make do not fold back as hash
                    float out = 0.0f;
                    for (int half = 0; half < 2; ++half)
                    {
                        float x = up[(size_t) ch].interpolate (in, half);
                        x = tight[(size_t) ch].process (x, 0) * drive;
                        hardest = jmax (hardest, std::abs (x));
                        for (int s = 0; s < m.stages; ++s)
                        {
                            x = valve (x, m.asym);
                            if (s + 1 < m.stages) x *= 2.2f;   // each stage drives the next
                        }
                        dcIn[(size_t) ch] += dcA * (x - dcIn[(size_t) ch]);   // lopsided clipping leaves a standing offset: take it out
                        x -= dcIn[(size_t) ch];
                        const float y = up[(size_t) ch].decimate (x, half);
                        if (half == 1) out = y;
                    }
                    out *= makeup;
                    for (auto& b : filters[(size_t) ch]) out = b.process (out, 0);
                    d[ch][i] = dry * (1.0f - mix) + out * level * mix;
                }
                if (++dryPos >= latencySamples) dryPos = 0;
            }
            driveNow = jmax (jlimit (0.0f, 1.0f, hardest / 6.0f), driveNow.load() * 0.9f);
        }

    private:
        /** The tone stack (bass, middle, treble, presence) followed by the cabinet (two cuts, the box thump, the bite, the microphone's position). */
        void setFilters (Biquad* f, double sr) const
        {
            const auto& m = models[jlimit (0, 4, getInt (pModel))];
            const auto& c = cabinets[jlimit (0, 4, getInt (pCabinet))];
            const bool cab = getInt (pCabinet) > 0;
            const float mic = get (pMic);
            f[0].set (Biquad::Type::lowShelf, sr, 120.0, 0.7, (get (pBass) - 0.5f) * 24.0);
            f[1].set (Biquad::Type::bell, sr, m.toneHz, 0.7, (get (pMid) - 0.5f) * 20.0);
            f[2].set (Biquad::Type::highShelf, sr, 3200.0, 0.7, (get (pTreble) - 0.5f) * 24.0);
            f[3].set (Biquad::Type::bell, sr, 4800.0, 0.9, (get (pPresence) - 0.5f) * 16.0);
            f[4].set (Biquad::Type::lowCut, sr, cab ? c.lowHz : 20.0, 0.8, 0.0);
            // moving the microphone from the centre of the cone to its edge takes the top away and softens the bite
            f[5].set (Biquad::Type::highCut, sr, cab ? c.highHz * (1.0f - 0.45f * mic) : 20000.0, 0.9, 0.0);
            f[6].set (Biquad::Type::highCut, sr, cab ? c.highHz * 1.25f * (1.0f - 0.45f * mic) : 20000.0, 0.6, 0.0);
            f[7].set (Biquad::Type::bell, sr, c.thumpHz, 1.2, cab ? c.thumpDb : 0.0);
            f[8].set (Biquad::Type::bell, sr, c.biteHz, 1.4, cab ? c.biteDb * (1.0f - 0.8f * mic) : 0.0);
        }

        /** Twice the sample rate and back, with a 31-tap half-band filter each way (every other tap is zero).
            Together the two filters make the sound 15 samples late, which the amp reports as its latency. */
        struct TwoTimes
        {
            static constexpr int pairs = 8;   // taps 1, 3 ... 15 either side of the middle one
            /** A Kaiser-windowed half-band low-pass: about 60 dB down from 0.36 of the doubled rate. */
            static const std::array<float, pairs>& taps()
            {
                static const std::array<float, pairs> h = []
                {
                    auto bessel = [] (double x) { double sum = 1.0, term = 1.0; for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; };
                    const double beta = 5.65;
                    std::array<double, pairs> d {};
                    double total = 0.0;
                    for (int k = 0; k < pairs; ++k)
                    {
                        const double n = 2.0 * k + 1.0, r = n / 16.0;
                        d[(size_t) k] = std::sin (MathConstants<double>::halfPi * n) / (MathConstants<double>::pi * n) * bessel (beta * std::sqrt (1.0 - r * r)) / bessel (beta);
                        total += d[(size_t) k];
                    }
                    std::array<float, pairs> out {};
                    for (int k = 0; k < pairs; ++k) out[(size_t) k] = (float) (d[(size_t) k] * 0.25 / total);   // so that a steady level passes unchanged
                    return out;
                }();
                return h;
            }
            float in[16] {}, dec[31] {};
            inline float interpolate (float x, int half) noexcept
            {
                if (half == 1) return in[7];   // 2 x the middle tap (0.5)
                for (int k = 15; k > 0; --k) in[k] = in[k - 1];
                in[0] = x;
                const auto& h = taps();
                float y = 0.0f;
                for (int k = 0; k < pairs; ++k) y += h[(size_t) k] * (in[7 - k] + in[8 + k]);
                return 2.0f * y;
            }
            inline float decimate (float x, int half) noexcept
            {
                for (int k = 30; k > 0; --k) dec[k] = dec[k - 1];
                dec[0] = x;
                if (half == 0) return 0.0f;   // only every second value is kept
                const auto& h = taps();
                float y = 0.5f * dec[15];
                for (int k = 0; k < pairs; ++k) y += h[(size_t) k] * (dec[14 - 2 * k] + dec[16 + 2 * k]);
                return y;
            }
        };
        static constexpr int latencySamples = 15;

        std::array<std::array<Biquad, 9>, 2> filters;
        std::array<Biquad, 2> tight;
        std::array<TwoTimes, 2> up;
        float dcIn[2] {}, sagEnv = 0.0f, lastSig = -1.0f;
        float dryLine[2][latencySamples] {};
        int dryPos = 0;
        ParamSmooth sGain, sLevel;
    };
}

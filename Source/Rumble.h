#pragma once

#include "ColourFx.h"

/** Rumble: the low, rolling tail that sits between the kicks of a techno track. */
namespace pulse
{
    /** What is usually built from four devices, in one: the kick is sent into a reverb, the reverb is cut down to
        its lows and driven, and the result is pushed out of the way by the kick itself, so that each kick lands
        clean and the rumble swells up behind it. Put it on the kick's track (or on a return fed by the kick). */
    class Rumble : public PulseEffect
    {
    public:
        enum { pAmount, pDecay, pTone, pLowCut, pDrive, pDuck, pRecover, pWidth, pDry };
        static inline const char* xmlTypeName = "pulseRumble";
        static const char* getPluginName() { return "Rumble"; }

        explicit Rumble (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("amount", "Amount", { -36.0f, 6.0f }, -8.0f, fmt::db);
            add ("decay", "Decay", skewed (0.2f, 6.0f, 1.2f), 1.2f, [] (float v) { return String (v, 2) + " s"; });
            add ("tone", "Tone", skewed (40.0f, 600.0f, 120.0f), 110.0f, fmt::hz);
            add ("lowcut", "Low Cut", skewed (20.0f, 120.0f, 40.0f), 30.0f, fmt::hz);
            add ("drive", "Drive", { 0.0f, 30.0f }, 8.0f, fmt::db);
            add ("duck", "Duck", { 0.0f, 1.0f }, 0.9f, fmt::pct);
            add ("recover", "Recover", skewed (20.0f, 800.0f, 150.0f), 160.0f, fmt::ms);
            add ("width", "Width", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("dry", "Kick", steps (2), 1.0f, fmt::choice ({ "Rumble only", "Kick and rumble" }));
        }

        String getName() const override       { return "Rumble"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Rumble", { pAmount, pDecay, pDrive, pWidth } }, { "Tone", { pTone, pLowCut } }, { "Duck", { pDuck, pRecover } }, { "Output", { pDry } } };
        }

        /** What the two filters leave of a frequency, as a gain (for the display). */
        float toneResponse (double f) const
        {
            Biquad lp, hp;
            lp.set (Biquad::Type::highCut, 48000.0, get (pTone), 0.707, 0.0);
            hp.set (Biquad::Type::lowCut, 48000.0, get (pLowCut), 0.707, 0.0);
            const double a = lp.magnitude (f, 48000.0);
            return (float) (a * a * hp.magnitude (f, 48000.0));
        }

        // For the display: the last two seconds, a value every 5 ms: how loud the kick is, how loud the rumble, and
        // how far the rumble is held down (1 = not at all). Written by the audio, read by the picture.
        static constexpr int historySize = 400;
        std::array<std::atomic<float>, historySize> kickHistory {}, rumbleHistory {}, duckHistory {};
        std::atomic<int> historyAt { 0 };

    protected:
        void prepare() override
        {
            // four delay lines of lengths that share no factors, a little longer than a kick's length of low notes needs
            static constexpr double lengthsMs[numLines] = { 41.3, 53.9, 67.1, 79.7 };
            for (int i = 0; i < numLines; ++i)
            {
                line[i].assign ((size_t) jmax (16, (int) (lengthsMs[i] * 0.001 * sampleRate)), 0.0f);
                at[i] = 0;
                damp[i] = 0.0f;
            }
            for (auto& f : lp1) f.reset(); for (auto& f : lp2) f.reset(); for (auto& f : hp) f.reset();
            for (auto& d : diffuse) { d.buf.assign ((size_t) jmax (8, (int) (d.ms * 0.001 * sampleRate)), 0.0f); d.at = 0; }
            env = peak = 0.0f; duckNow = 1.0f;
            lastTone = lastCut = -1.0f;
            amountNow = Decibels::decibelsToGain (get (pAmount));
            dryNow = getInt (pDry) > 0 ? 1.0f : 0.0f;
            histCount = 0; histKick = histRumble = 0.0f; histDuck = 1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float tone = get (pTone), cut = get (pLowCut);
            if (std::abs (tone - lastTone) > 0.5f || std::abs (cut - lastCut) > 0.2f)
            {
                for (auto& f : lp1) f.set (Biquad::Type::highCut, sampleRate, tone, 0.707, 0.0);
                for (auto& f : lp2) f.set (Biquad::Type::highCut, sampleRate, tone, 0.707, 0.0);
                for (auto& f : hp) f.set (Biquad::Type::lowCut, sampleRate, cut, 0.707, 0.0);
                lastTone = tone; lastCut = cut;
            }
            // each trip round a line loses what makes the whole die away by 60 dB in Decay seconds
            const float decay = get (pDecay);
            float fb[numLines];
            for (int i = 0; i < numLines; ++i) fb[i] = std::pow (10.0f, -3.0f * (float) line[i].size() / (float) (decay * sampleRate));
            const float drive = Decibels::decibelsToGain (get (pDrive)), driveLevel = 1.0f / std::sqrt (drive);
            const float duck = get (pDuck), width = get (pWidth);
            const float amountWant = Decibels::decibelsToGain (get (pAmount)), dryWant = getInt (pDry) > 0 ? 1.0f : 0.0f;
            const float smooth = std::exp (-1.0f / (float) (0.01 * sampleRate));
            const float attack = std::exp (-1.0f / (float) (0.0015 * sampleRate)), release = std::exp (-1.0f / (float) (get (pRecover) * 0.001 * sampleRate));
            const float peakFall = std::exp (-1.0f / (float) (3.0 * sampleRate));
            const int histEvery = jmax (1, (int) (0.005 * sampleRate));
            const float showFall = std::exp (-1.0f / (float) (0.05 * sampleRate));

            for (int i = 0; i < n; ++i)
            {
                amountNow = amountWant + (amountNow - amountWant) * smooth;
                dryNow = dryWant + (dryNow - dryWant) * smooth;
                const float inL = l[i], inR = r[i], mono = (inL + inR) * 0.5f;

                // how loud the kick is right now, against how loud it gets: that is how far the rumble is pushed down
                const float mag = std::abs (mono);
                env = mag > env ? mag + (env - mag) * attack : mag + (env - mag) * release;
                peak = jmax (env, peak * peakFall, 1.0e-4f);
                const float push = jlimit (0.0f, 1.0f, env / peak);
                const float duckWant = 1.0f - duck * std::sqrt (push);
                duckNow = duckWant + (duckNow - duckWant) * 0.995f;   // (never a step: a step in a gain is a click)

                // the reverb: two all-passes to smear the kick, then four lines feeding one another
                float x = mono;
                for (auto& d : diffuse)
                {
                    const float out = d.buf[(size_t) d.at] - 0.6f * x;
                    d.buf[(size_t) d.at] = x + 0.6f * out;
                    if (++d.at >= (int) d.buf.size()) d.at = 0;
                    x = out;
                }
                float out[numLines], sum = 0.0f;
                for (int k = 0; k < numLines; ++k) { out[k] = line[k][(size_t) at[k]]; sum += out[k]; }
                sum *= 0.5f;   // (a Householder mix: every line hears the others, and energy is kept)
                for (int k = 0; k < numLines; ++k)
                {
                    float v = (out[k] - sum) * fb[k] + x * 0.5f;
                    damp[k] += (v - damp[k]) * 0.35f;   // the highs die first, as in a room
                    v = damp[k];
                    line[k][(size_t) at[k]] = v;
                    if (++at[k] >= (int) line[k].size()) at[k] = 0;
                }
                // two sums of the lines that differ: together they are the middle, apart the sides
                // (a short kick spread over a long tail is quiet: it is brought up here, before the drive, so that the drive has something to bite on)
                const float mid = (out[0] + out[1] + out[2] + out[3]) * 2.0f, side = (out[0] - out[1] + out[2] - out[3]) * 2.0f * width;
                float wet[2] = { mid + side, mid - side };
                float rumblePeak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float w = lp2[(size_t) ch].process (lp1[(size_t) ch].process (wet[ch], 0), 0);
                    w = std::tanh (w * drive) * driveLevel;
                    w = lp2[(size_t) ch].process (hp[(size_t) ch].process (w, 0), 1);   // what the drive added above Tone goes again; so does what sits under Low Cut
                    w *= duckNow * amountNow;
                    wet[ch] = w;
                    rumblePeak = jmax (rumblePeak, std::abs (w));
                }
                l[i] = inL * dryNow + wet[0];
                r[i] = inR * dryNow + wet[1];

                // (for the picture, levels that hold over a cycle of a low note: a 50 Hz wave is not drawn as its own wiggles)
                showKick = jmax (mag, showKick * showFall); showRumble = jmax (rumblePeak, showRumble * showFall);
                histKick = jmax (histKick, showKick); histRumble = jmax (histRumble, showRumble); histDuck = jmin (histDuck, duckNow);
                if (++histCount >= histEvery)
                {
                    const int at2 = (historyAt.load (std::memory_order_relaxed) + 1) % historySize;
                    kickHistory[(size_t) at2].store (histKick, std::memory_order_relaxed);
                    rumbleHistory[(size_t) at2].store (histRumble, std::memory_order_relaxed);
                    duckHistory[(size_t) at2].store (histDuck, std::memory_order_relaxed);
                    historyAt.store (at2, std::memory_order_relaxed);
                    histCount = 0; histKick = histRumble = 0.0f; histDuck = 1.0f;
                }
            }
        }

    private:
        static constexpr int numLines = 4;
        struct AllPass { double ms; std::vector<float> buf; int at = 0; };
        std::vector<float> line[numLines];
        int at[numLines] {};
        float damp[numLines] {};
        AllPass diffuse[2] { { 7.3, {}, 0 }, { 11.9, {}, 0 } };
        std::array<Biquad, 2> lp1, lp2, hp;
        float env = 0.0f, peak = 0.0f, duckNow = 1.0f, lastTone = -1.0f, lastCut = -1.0f, amountNow = 0.4f, dryNow = 1.0f;
        int histCount = 0;
        float histKick = 0.0f, histRumble = 0.0f, histDuck = 1.0f, showKick = 0.0f, showRumble = 0.0f;
    };
}

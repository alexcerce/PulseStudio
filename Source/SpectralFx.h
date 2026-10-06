#pragma once

#include "LoopFx.h"

/** Pulse Spectral: works on the sound as a set of frequencies instead of a wave. It can hold a moment for as
    long as you like (Freeze), smear it in time (Blur), move it in pitch (Shift) and delay each frequency by a
    different amount (the spectral delay). */
namespace pulse
{
    class Spectral : public PulseEffect
    {
    public:
        enum { pFreeze, pBlur, pShift, pDelay, pTilt, pFeedback, pMix };
        static inline const char* xmlTypeName = "pulseSpectral";
        static const char* getPluginName() { return "Pulse Spectral"; }
        static constexpr int order = 11, size = 1 << order, hop = size / 4, bins = size / 2 + 1;
        static constexpr float maxDelayMs = 2000.0f;
        static constexpr int shownPoints = 96;
        static constexpr float loudest = (float) size * 0.3f;   // about what one bin holds for a tone at full level

        explicit Spectral (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("freeze", "Freeze", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("blur", "Blur", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("shift", "Shift", { -24.0f, 24.0f }, 0.0f, [] (float v) { return (v > 0.005f ? "+" : "") + String (v, 2) + " st"; });
            add ("delay", "Delay", skewed (0.0f, maxDelayMs, 300.0f), 0.0f, fmt::ms);
            add ("tilt", "Tilt", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v > 0.02f ? "Highs later " + String (roundToInt (v * 100)) + "%" : v < -0.02f ? "Lows later " + String (roundToInt (-v * 100)) + "%" : String ("Even"); });
            add ("feedback", "Feedback", { 0.0f, 0.95f }, 0.0f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 1.0f, fmt::pct);
        }

        String getName() const override       { return "Pulse Spectral"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        double getLatencySeconds() override    { return size / sampleRate; }
        bool canSleepOnSilence() const override { return getInt (pFreeze) == 0; }   // a frozen sound goes on with nothing coming in
        std::vector<Section> getSections() const override { return { { "Hold", { pFreeze, pBlur } }, { "Pitch", { pShift } }, { "Delay", { pDelay, pTilt, pFeedback } }, { "Output", { pMix } } }; }

        /** How late (ms) a frequency comes out, from Delay and Tilt. pos01: 0 = the lowest frequency, 1 = the highest (on a scale of octaves). */
        float delayAt (float pos01) const
        {
            const float d = get (pDelay), t = get (pTilt);
            return d * ((1.0f - std::abs (t)) + std::abs (t) * (t > 0.0f ? pos01 : 1.0f - pos01));
        }
        static float posForBin (int k) { return jlimit (0.0f, 1.0f, std::log2 (jmax (1.0f, (float) k)) / (float) (order - 1)); }

        // for the display: the level (dB) going in and coming out at `shownPoints` frequencies from 20 Hz to 20 kHz
        std::array<std::atomic<float>, shownPoints> shownIn {}, shownOut {};
        std::atomic<bool> holding { false };

    protected:
        void prepare() override
        {
            const int frames = (int) (maxDelayMs * 0.001 * sampleRate / hop) + 4;
            for (auto& c : ch)
            {
                c.in.assign ((size_t) size, 0.0f);
                c.out.assign ((size_t) size * 2, 0.0f);
                c.dry.assign ((size_t) size, 0.0f);
                c.lastPhase.assign ((size_t) bins, 0.0f);
                c.sumPhase.assign ((size_t) bins, 0.0f);
                c.smooth.assign ((size_t) bins, 0.0f);
                c.blurFreq.assign ((size_t) bins, 0.0f);
                for (int k = 0; k < bins; ++k) c.blurFreq[(size_t) k] = (float) k;
                c.settling = 0;
                c.frozenEnergy = 0.0f;
                c.frozenMag.assign ((size_t) bins, 0.0f);
                c.frozenFreq.assign ((size_t) bins, 0.0f);
                c.ring.assign ((size_t) frames * (size_t) bins * 2, 0.0f);
                c.frozen = false;
                c.ringPos = 0;
            }
            ringFrames = frames;
            window.resize ((size_t) size);
            for (int i = 0; i < size; ++i) window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) size));
            work.assign ((size_t) size * 2, 0.0f);
            mag.assign ((size_t) bins, 0.0f); freq.assign ((size_t) bins, 0.0f);
            outMag.assign ((size_t) bins, 0.0f); outFreq.assign ((size_t) bins, 0.0f);
            pos = 0;
            for (auto& s : shownIn) s.store (-100.0f);
            for (auto& s : shownOut) s.store (-100.0f);
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float mix = get (pMix);
            float* d[2] = { l, r };
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = ch[(size_t) c];
                    const float x = d[c][i];
                    s.in[(size_t) pos] = x;
                    // what comes out is `size` samples late; the untouched sound is made as late, so a part-wet mix stays in step
                    const float wet = s.out[(size_t) pos], dry = s.dry[(size_t) pos];
                    s.out[(size_t) pos] = 0.0f;
                    s.dry[(size_t) pos] = x;
                    d[c][i] = dry * (1.0f - mix) + wet * mix;
                }
                if (++pos >= size) pos = 0;
                if (pos % hop == 0)
                    for (int c = 0; c < 2; ++c) frame (ch[(size_t) c], c == 0);
            }
        }

    private:
        struct Channel
        {
            std::vector<float> in, out, dry, lastPhase, sumPhase, smooth, blurFreq, frozenMag, frozenFreq, ring;
            bool frozen = false;
            int ringPos = 0, settling = 0;
            float frozenEnergy = 0.0f;
        };

        /** One step: the last `size` samples are turned into frequencies, changed, and turned back. */
        void frame (Channel& s, bool show)
        {
            const bool freeze = getInt (pFreeze) > 0;
            const float blur = get (pBlur), feedback = get (pFeedback);
            const float ratio = std::pow (2.0f, get (pShift) / 12.0f);
            const bool shifting = std::abs (ratio - 1.0f) > 1.0e-4f;
            const float expect = MathConstants<float>::twoPi * (float) hop / (float) size;

            for (int i = 0; i < size; ++i) work[(size_t) i] = s.in[(size_t) ((pos + i) % size)] * window[(size_t) i];
            std::fill (work.begin() + size, work.end(), 0.0f);
            fft.performRealOnlyForwardTransform (work.data(), true);

            // to loudness and true frequency (in bins) per bin
            float energy = 0.0f;
            for (int k = 0; k < bins; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                const float m = std::sqrt (re * re + im * im), ph = std::atan2 (im, re);
                float dp = ph - s.lastPhase[(size_t) k] - (float) k * expect;
                dp -= MathConstants<float>::twoPi * std::round (dp / MathConstants<float>::twoPi);
                s.lastPhase[(size_t) k] = ph;
                mag[(size_t) k] = m;
                freq[(size_t) k] = (float) k + dp / expect;
                energy += m * m;
            }
            if (show) showLevels (mag, shownIn);

            // Freeze holds the sound of the moment it is switched on (or, switched on in silence, of the next sound)
            // For a few steps after that it takes a louder moment instead if one comes, so that it does not hold just the
            // first edge of a note.
            if (! freeze) s.frozen = false;
            else if ((! s.frozen && energy > 1.0e-3f) || (s.frozen && s.settling > 0 && energy > s.frozenEnergy))
            {
                s.frozenMag = mag; s.frozenFreq = freq;
                s.frozenEnergy = energy;
                if (! s.frozen) s.settling = 12;
                s.frozen = true;
            }
            if (s.settling > 0) --s.settling;
            const bool held = freeze && s.frozen;
            if (show) holding.store (held);
            if (held) { mag = s.frozenMag; freq = s.frozenFreq; }
            else if (freeze) std::fill (mag.begin(), mag.end(), 0.0f);   // waiting for a sound to hold

            // Blur: each frequency's loudness follows slowly, so sounds smear into what comes after them
            if (blur > 0.001f)
            {
                const float b = 0.97f * std::sqrt (blur);
                for (int k = 0; k < bins; ++k)
                {
                    // a frequency that is ringing on after its sound has gone keeps the pitch it had
                    if (mag[(size_t) k] > 0.3f * s.smooth[(size_t) k]) s.blurFreq[(size_t) k] = freq[(size_t) k];
                    else freq[(size_t) k] = s.blurFreq[(size_t) k];
                    s.smooth[(size_t) k] = jmax (mag[(size_t) k] * (1.0f - b) + s.smooth[(size_t) k] * b, mag[(size_t) k] * (1.0f - blur));
                    mag[(size_t) k] = s.smooth[(size_t) k];
                }
            }
            else s.smooth = mag;

            // (with nothing switched on, the sound's own phases are kept and the round trip changes nothing)
            const bool rebuild = shifting || held || blur > 0.001f;
            if (rebuild)
            {
                std::fill (outMag.begin(), outMag.end(), 0.0f);
                std::fill (outFreq.begin(), outFreq.end(), 0.0f);
                for (int k = 0; k < bins; ++k)
                {
                    const int to = roundToInt ((float) k * ratio);
                    if (to >= bins) break;
                    if (mag[(size_t) k] >= outMag[(size_t) to]) outFreq[(size_t) to] = freq[(size_t) k] * ratio;   // the louder partial names the frequency
                    outMag[(size_t) to] += mag[(size_t) k];
                }
                for (int k = 0; k < bins; ++k)
                {
                    s.sumPhase[(size_t) k] += expect * outFreq[(size_t) k];
                    s.sumPhase[(size_t) k] -= MathConstants<float>::twoPi * std::floor (s.sumPhase[(size_t) k] / MathConstants<float>::twoPi);
                    work[(size_t) (2 * k)] = outMag[(size_t) k] * std::cos (s.sumPhase[(size_t) k]);
                    work[(size_t) (2 * k + 1)] = outMag[(size_t) k] * std::sin (s.sumPhase[(size_t) k]);
                }
            }
            else
            {
                for (int k = 0; k < bins; ++k) s.sumPhase[(size_t) k] = s.lastPhase[(size_t) k];   // so that switching something on carries on from here
            }

            // the spectral delay: each frequency is read from its own number of steps ago
            if (get (pDelay) > 0.5f || feedback > 0.001f)
            {
                const float framesPerMs = (float) (sampleRate * 0.001 / hop);
                float* now = s.ring.data() + (size_t) s.ringPos * (size_t) bins * 2;
                for (int k = 0; k < bins; ++k)
                {
                    const int back = jlimit (0, ringFrames - 1, roundToInt (delayAt (posForBin (k)) * framesPerMs));
                    const float* then = s.ring.data() + (size_t) ((s.ringPos - back + ringFrames) % ringFrames) * (size_t) bins * 2;
                    const float inRe = work[(size_t) (2 * k)], inIm = work[(size_t) (2 * k + 1)];
                    const float outRe = back == 0 ? inRe : then[2 * k], outIm = back == 0 ? inIm : then[2 * k + 1];
                    float keepRe = inRe + (back == 0 ? 0.0f : outRe * feedback), keepIm = inIm + (back == 0 ? 0.0f : outIm * feedback);
                    // A held sound fed back on itself would grow without end: no frequency is kept louder than a full-level tone.
                    if (const float m2 = keepRe * keepRe + keepIm * keepIm; m2 > loudest * loudest) { const float sc = loudest / std::sqrt (m2); keepRe *= sc; keepIm *= sc; }
                    now[2 * k] = keepRe;
                    now[2 * k + 1] = keepIm;
                    work[(size_t) (2 * k)] = outRe;
                    work[(size_t) (2 * k + 1)] = outIm;
                }
                if (++s.ringPos >= ringFrames) s.ringPos = 0;
            }
            if (show)
            {
                for (int k = 0; k < bins; ++k) outMag[(size_t) k] = std::sqrt (work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)]);
                showLevels (outMag, shownOut);
            }

            // back to a wave, and onto the output where the overlapping steps add up
            for (int k = bins; k < size; ++k) { work[(size_t) (2 * k)] = work[(size_t) (2 * (size - k))]; work[(size_t) (2 * k + 1)] = -work[(size_t) (2 * (size - k) + 1)]; }
            fft.performRealOnlyInverseTransform (work.data());
            for (int i = 0; i < size; ++i) s.out[(size_t) ((pos + i) % size)] += work[(size_t) i] * window[(size_t) i] * 0.5f;   // four overlapping squared windows add up to 2
        }

        void showLevels (const std::vector<float>& m, std::array<std::atomic<float>, shownPoints>& into)
        {
            for (int p = 0; p < shownPoints; ++p)
            {
                const double f0 = 20.0 * std::pow (1000.0, (double) p / shownPoints), f1 = 20.0 * std::pow (1000.0, (double) (p + 1) / shownPoints);
                const int a = jlimit (1, bins - 1, (int) (f0 / sampleRate * size)), b = jlimit (a + 1, bins, (int) (f1 / sampleRate * size) + 1);
                float pk = 0.0f;
                for (int k = a; k < b; ++k) pk = jmax (pk, m[(size_t) k]);
                // at the bottom, where the points are closer together than the frequencies worked out, read between them
                const float at = (float) (std::sqrt (f0 * f1) / sampleRate * size);
                if (f1 - f0 < sampleRate / size && at >= 1.0f && at < (float) (bins - 1))
                    pk = m[(size_t) at] + (m[(size_t) at + 1] - m[(size_t) at]) * (at - std::floor (at));
                const float db = Decibels::gainToDecibels (pk * 4.0f / (float) size, -100.0f);
                into[(size_t) p].store (jmax (db, into[(size_t) p].load() - 1.5f));   // falls back slowly, so it can be read
            }
        }

        dsp::FFT fft { order };
        std::array<Channel, 2> ch;
        std::vector<float> window, work, mag, freq, outMag, outFreq;
        int pos = 0, ringFrames = 4;
    };
}

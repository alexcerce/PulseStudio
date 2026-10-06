#pragma once

#include "AmpFx.h"

/** Effects built on delay lines: Resonators (tuned, ringing strings), Shimmer (a reverb whose tail climbs in
    pitch) and Grain Delay (a delay that plays back in small, re-pitched, scattered pieces). */
namespace pulse
{
    /** Changes the pitch of what passes through, by reading a short delay line with two overlapping pieces. */
    struct GrainShift
    {
        void prepare (double sr, double windowMs = 70.0)
        {
            window = jmax (64.0f, (float) (sr * windowMs * 0.001));
            line.setMax ((int) window + 16);
            pos = 0.0f;
        }
        /** ratio: 2 = an octave up, 0.5 = an octave down. */
        inline float process (float x, float ratio) noexcept
        {
            line.push (x);
            pos += 1.0f - ratio;   // how late the reading point is, moving against the pitch change
            if (pos < 0.0f) pos += window;
            if (pos >= window) pos -= window;
            float pos2 = pos + window * 0.5f;
            if (pos2 >= window) pos2 -= window;
            // each piece fades in from the join and out towards it, so the join itself is never heard
            const float w1 = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * pos / window);
            return line.read (1.0f + pos) * w1 + line.read (1.0f + pos2) * (1.0f - w1);
        }
        DelayLine line;
        float window = 3000.0f, pos = 0.0f;
    };

    //==============================================================================
    /** Five strings tuned to a note and a chord above it, set ringing by whatever is played through them. */
    class Resonators : public PulseEffect
    {
    public:
        enum { pNote, pChord, pDecay, pColour, pDetune, pWidth, pInput, pMix };
        static inline const char* xmlTypeName = "pulseResonators";
        static const char* getPluginName() { return "Resonators"; }
        static constexpr int num = 5;
        static inline const StringArray chordNames { "Octaves", "Fifths", "Major", "Minor", "Sus 4", "Major 7", "Minor 7", "Harmonics" };
        static constexpr float chords[8][num] = {
            { 0, 12, 24, -12, 36 }, { 0, 7, 12, 19, 24 }, { 0, 4, 7, 12, 16 }, { 0, 3, 7, 12, 15 },
            { 0, 5, 7, 12, 17 }, { 0, 4, 7, 11, 14 }, { 0, 3, 7, 10, 14 }, { 0, 12, 19.02f, 24, 27.86f } };

        explicit Resonators (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("note", "Note", { 24.0f, 84.0f, 1.0f }, 45.0f, [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); });
            add ("chord", "Chord", steps (chordNames.size()), 2.0f, fmt::choice (chordNames));
            add ("decay", "Decay", skewed (0.05f, 12.0f, 1.5f), 1.5f, [] (float v) { return String (v, 2) + " s"; });
            add ("colour", "Brightness", skewed (500.0f, 16000.0f, 4000.0f), 6000.0f, fmt::hz);
            add ("detune", "Detune", { 0.0f, 50.0f }, 4.0f, [] (float v) { return String (v, 1) + " ct"; });
            add ("width", "Width", { 0.0f, 1.0f }, 0.8f, fmt::pct);
            add ("input", "Input", { -24.0f, 12.0f }, 0.0f, fmt::db);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.5f, fmt::pct);
        }

        String getName() const override       { return "Resonators"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Tuning", { pNote, pChord, pDetune } }, { "Ring", { pDecay, pColour } }, { "Output", { pWidth, pInput, pMix } } }; }

        /** The frequency of string i (0..4), without the detune. */
        float frequency (int i) const
        {
            return 440.0f * std::pow (2.0f, (get (pNote) + chords[jlimit (0, 7, getInt (pChord))][jlimit (0, num - 1, i)] - 69.0f) / 12.0f);
        }

        std::array<std::atomic<float>, num> ringing {};   // how loud each string is right now (0..1), for the display

    protected:
        void prepare() override
        {
            for (auto& side : lines) for (auto& d : side) d.setMax ((int) (sampleRate / 14.0) + 16);
            for (auto& side : damp) side.fill (0.0f);
            for (auto& s : sFreq) s.reset();
            for (auto& p : peaks) p = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float decay = get (pDecay), width = get (pWidth), mix = get (pMix), in = Decibels::decibelsToGain (get (pInput));
            const float lpA = 1.0f - std::exp (-MathConstants<float>::twoPi * get (pColour) / (float) sampleRate);
            const float detune = get (pDetune);
            const float a = std::exp (-1.0f / (float) (0.03 * sampleRate));
            float target[num];
            for (int i = 0; i < num; ++i) target[i] = jlimit (16.0f, (float) sampleRate * 0.4f, frequency (i));
            // the strings sit across the stereo field: 1 in the middle, the others further out in turn
            static constexpr float place[num] = { 0.0f, -0.6f, 0.6f, -1.0f, 1.0f };
            float* d[2] = { l, r };

            for (int s = 0; s < n; ++s)
            {
                const float dry[2] = { l[s], r[s] };
                float wet[2] = { 0.0f, 0.0f };
                for (int i = 0; i < num; ++i)
                {
                    const float f = sFreq[(size_t) i].next (target[i], a);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        // the two sides of each string are a few cents apart, which makes it shimmer
                        const float len = (float) sampleRate / (f * std::pow (2.0f, (ch == 0 ? -detune : detune) / 2400.0f));
                        const float g = std::pow (10.0f, -3.0f * len / ((float) sampleRate * decay));
                        auto& line = lines[(size_t) ch][(size_t) i];
                        float fb = line.read (jmax (1.0f, len - 0.5f));   // (half a sample is taken up by the damping filter)
                        damp[(size_t) ch][(size_t) i] += lpA * (fb - damp[(size_t) ch][(size_t) i]);
                        fb = damp[(size_t) ch][(size_t) i];
                        // A string that rings longer builds up more from the same push: the push is made that much gentler, so
                        // Decay changes how long it rings and not how loud. And a string can only swing so far (a note played
                        // exactly on its pitch would otherwise build up without end).
                        const float y = std::tanh (dry[ch] * in * std::sqrt (1.0f - g * g) + fb * g);
                        line.push (y);
                        const float pan = place[i] * width;
                        wet[ch] += y * (ch == 0 ? jmin (1.0f, 1.0f - pan) : jmin (1.0f, 1.0f + pan));
                        peaks[(size_t) i] = jmax (peaks[(size_t) i], std::abs (y));
                    }
                }
                for (int ch = 0; ch < 2; ++ch) d[ch][s] = dry[ch] * (1.0f - mix) + wet[ch] * 0.45f * mix;
            }
            for (int i = 0; i < num; ++i)
            {
                ringing[(size_t) i].store (jlimit (0.0f, 1.0f, jmax (peaks[(size_t) i], ringing[(size_t) i].load() * 0.9f)));
                peaks[(size_t) i] = 0.0f;
            }
        }

    private:
        std::array<std::array<DelayLine, num>, 2> lines;
        std::array<std::array<float, num>, 2> damp {};
        std::array<ParamSmooth, num> sFreq;
        std::array<float, num> peaks {};
    };

    //==============================================================================
    /** Shimmer: a reverb whose tail is fed back into itself shifted up (or down) in pitch, so it climbs away. */
    class Shimmer : public PulseEffect
    {
    public:
        enum { pDecay, pShimmer, pInterval, pSize, pDamping, pLowCut, pWidth, pMix };
        static inline const char* xmlTypeName = "pulseShimmer";
        static const char* getPluginName() { return "Shimmer"; }
        static inline const StringArray intervalNames { "Octave down", "Fourth up", "Fifth up", "Octave up", "Octave + fifth", "Two octaves" };
        static constexpr float intervals[] = { -12.0f, 5.0f, 7.0f, 12.0f, 19.0f, 24.0f };

        explicit Shimmer (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("decay", "Decay", skewed (0.5f, 30.0f, 5.0f), 5.0f, [] (float v) { return String (v, 1) + " s"; });
            add ("shimmer", "Shimmer", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("interval", "Interval", steps (intervalNames.size()), 3.0f, fmt::choice (intervalNames));
            add ("size", "Size", { 0.2f, 1.0f }, 0.7f, fmt::pct);
            add ("damping", "Damping", skewed (1000.0f, 20000.0f, 6000.0f), 8000.0f, fmt::hz);
            add ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 150.0f), 150.0f, fmt::hz);
            add ("width", "Width", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.35f, fmt::pct);
        }

        String getName() const override       { return "Shimmer"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Tail", { pDecay, pSize, pDamping, pLowCut } }, { "Shimmer", { pShimmer, pInterval } }, { "Output", { pWidth, pMix } } }; }

        float semitones() const { return intervals[jlimit (0, 5, getInt (pInterval))]; }
        std::atomic<float> wetDb { -100.0f };

    protected:
        static constexpr int N = 8;
        static constexpr float lens[N] = { 1117.0f, 1361.0f, 1559.0f, 1823.0f, 2099.0f, 2411.0f, 2707.0f, 3001.0f };   // at 44.1 kHz
        static constexpr float diffLens[4] = { 151.0f, 113.0f, 389.0f, 283.0f };

        void prepare() override
        {
            const double scale = sampleRate / 44100.0;
            for (int i = 0; i < N; ++i) { lines[(size_t) i].setMax ((int) (lens[i] * scale) + 64); lp[(size_t) i] = 0.0f; }
            for (int i = 0; i < 4; ++i) diff[(size_t) i].setMax ((int) (diffLens[i] * scale) + 8);
            for (auto& s : shift) s.prepare (sampleRate);
            lowCut.reset(); fbCut.reset();
            lastLow = -1.0f;
            fb[0] = fb[1] = 0.0f;
            phase = 0.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const double scale = sampleRate / 44100.0;
            const float size = get (pSize), decay = get (pDecay), mix = get (pMix), width = get (pWidth), shimmer = get (pShimmer);
            const float ratio = std::pow (2.0f, semitones() / 12.0f);
            const float dampA = std::exp (-MathConstants<float>::twoPi * get (pDamping) / (float) sampleRate);
            if (std::abs (get (pLowCut) - lastLow) > 0.5f)
            {
                lowCut.set (Biquad::Type::lowCut, sampleRate, get (pLowCut), 0.707, 0.0);
                fbCut.set (Biquad::Type::highCut, sampleRate, 9000.0, 0.707, 0.0);   // the climbing copies lose their top each time round, as in the hardware
                lastLow = get (pLowCut);
            }
            float len[N], g[N];
            for (int i = 0; i < N; ++i)
            {
                len[i] = lens[i] * (0.4f + 0.6f * size) * (float) scale;
                g[i] = std::pow (10.0f, -3.0f * len[i] / ((float) sampleRate * decay));
            }
            // a longer tail builds up more from what is fed back into it, so less is fed back: the climb stays in hand at any decay
            const float climb = 0.6f * jmin (1.0f, std::sqrt (3.0f / decay));
            float peak = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                const float dryL = l[s], dryR = r[s];
                // what goes into the reverb: the sound, plus the reverb's own output shifted in pitch
                const float up = fbCut.process (0.5f * (shift[0].process (fb[0], ratio) + shift[1].process (fb[1], ratio)), 0);
                // (the low cut is inside the loop, so copies shifted down cannot pile up below hearing either)
                float x = lowCut.process (0.5f * (dryL + dryR) + std::tanh (up * shimmer * 2.5f) * climb, 0);
                for (int k = 0; k < 4; ++k)
                {
                    const float dl = diffLens[k] * (float) scale, delayed = diff[(size_t) k].read (dl), v = x + 0.65f * delayed;
                    diff[(size_t) k].push (v);
                    x = delayed - 0.65f * v;
                }
                phase += 0.5 / sampleRate;
                if (phase > 1.0) phase -= 1.0;
                float o[N], sum = 0.0f;
                for (int i = 0; i < N; ++i)
                {
                    const float m = 6.0f * (float) scale * (float) std::sin (MathConstants<double>::twoPi * (phase + i * 0.125));
                    const float y = lines[(size_t) i].read (len[i] + m);
                    lp[(size_t) i] = y + (lp[(size_t) i] - y) * dampA;
                    o[i] = lp[(size_t) i] * g[i];
                    sum += o[i];
                }
                sum *= 2.0f / (float) N;
                for (int i = 0; i < N; ++i) lines[(size_t) i].push (o[i] - sum + (i % 2 == 0 ? x : -x) * 0.35f);
                const float wl = (o[0] + o[2] + o[4] + o[6]) * 0.5f, wr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
                fb[0] = wl; fb[1] = wr;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                l[s] = dryL * (1.0f - mix) + (mid + side) * mix;
                r[s] = dryR * (1.0f - mix) + (mid - side) * mix;
                peak = jmax (peak, std::abs (wl), std::abs (wr));
            }
            wetDb = jmax (Decibels::gainToDecibels (peak, -100.0f), wetDb.load() - 1.5f);
        }

    private:
        std::array<DelayLine, N> lines;
        std::array<DelayLine, 4> diff;
        std::array<float, N> lp {};
        std::array<GrainShift, 2> shift;
        Biquad lowCut, fbCut;
        float lastLow = -1.0f, fb[2] {};
        double phase = 0.0;
    };

    //==============================================================================
    /** Grain Delay: the echo is played back in short pieces ("grains"), each of which can be re-pitched and can
        come from a slightly different moment (Spray), with some of it fed back in. */
    class GrainDelay : public ModEffect
    {
    public:
        enum { pTime, pSync, pPitch, pGrain, pSpray, pRandomPitch, pFeedback, pMix };
        static inline const char* xmlTypeName = "pulseGrainDelay";
        static const char* getPluginName() { return "Grain Delay"; }
        static constexpr float maxTimeMs = 2000.0f, maxSprayMs = 500.0f;
        static inline const StringArray timeSyncNames { "Free (ms)", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };

        explicit GrainDelay (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("time", "Time", skewed (1.0f, maxTimeMs, 200.0f), 250.0f, fmt::ms);
            add ("sync", "Sync", steps (timeSyncNames.size()), 0.0f, fmt::choice (timeSyncNames));
            add ("pitch", "Pitch", { -24.0f, 24.0f }, 0.0f, [] (float v) { return (v > 0.005f ? "+" : "") + String (v, 2) + " st"; });
            add ("grain", "Grain Size", skewed (10.0f, 250.0f, 60.0f), 70.0f, fmt::ms);
            add ("spray", "Spray", skewed (0.0f, maxSprayMs, 60.0f), 0.0f, fmt::ms);
            add ("randompitch", "Random Pitch", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("feedback", "Feedback", { 0.0f, 0.95f }, 0.3f, fmt::pct);
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.4f, fmt::pct);
        }

        String getName() const override       { return "Grain Delay"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override { return { { "Delay", { pTime, pSync, pFeedback } }, { "Grains", { pPitch, pGrain, pSpray, pRandomPitch } }, { "Output", { pMix } } }; }

        /** The delay in milliseconds: Time, or the note value at the song's tempo when synced. */
        float timeMs() const
        {
            const int sync = getInt (pSync);
            return sync > 0 ? jmin (maxTimeMs, (float) (syncBeats[jlimit (1, 7, sync)] * 60000.0 / bpm.load())) : get (pTime);
        }

        /** The two grains playing now: how far back each reads (ms) and its pitch (semitones), for the display. */
        struct Shown { std::atomic<float> ms { 0.0f }, semis { 0.0f }, level { 0.0f }; };
        std::array<Shown, 2> shown;

    protected:
        void prepare() override
        {
            for (auto& d : lines) d.setMax ((int) (sampleRate * (maxTimeMs + maxSprayMs + 600.0f) * 0.001) + 64);
            for (auto& gr : grains) gr = {};
            grains[1].age = -1.0f;   // starts half a grain after the first
            fb[0] = fb[1] = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float mix = get (pMix), feedback = get (pFeedback);
            const float grainLen = jmax (32.0f, get (pGrain) * 0.001f * (float) sampleRate);
            const float base = timeMs() * 0.001f * (float) sampleRate, spray = get (pSpray) * 0.001f * (float) sampleRate;
            const float pitch = get (pPitch), randomPitch = get (pRandomPitch);
            const float limit = (float) lines[0].buf.size() - 10.0f;

            for (int i = 0; i < n; ++i)
            {
                lines[0].push (l[i] + fb[0] * feedback);
                lines[1].push (r[i] + fb[1] * feedback);
                float wl = 0.0f, wr = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    auto& gr = grains[(size_t) k];
                    if (gr.age < 0.0f)   // the second grain waits half a grain so the two overlap evenly
                    {
                        if (grains[0].age >= grainLen * 0.5f) gr.age = grainLen;   // (starts fresh just below)
                        else continue;
                    }
                    if (gr.age >= gr.length)
                    {
                        // a new grain: from when, and at what pitch
                        gr.length = grainLen;
                        gr.age = 0.0f;
                        gr.semis = pitch + (random.nextFloat() * 2.0f - 1.0f) * randomPitch * 12.0f;
                        gr.ratio = std::pow (2.0f, gr.semis / 12.0f);
                        gr.offset = base + random.nextFloat() * spray;
                        // a grain played faster catches up with "now": it has to start that much further back
                        if (gr.ratio > 1.0f) gr.offset += (gr.ratio - 1.0f) * gr.length;
                        shown[(size_t) k].ms.store (gr.offset / (float) sampleRate * 1000.0f);
                        shown[(size_t) k].semis.store (gr.semis);
                    }
                    const float back = jlimit (1.0f, limit, gr.offset + (1.0f - gr.ratio) * gr.age);
                    const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * gr.age / gr.length);
                    wl += lines[0].read (back + 1.0f) * w;   // (1 is the sample just written)
                    wr += lines[1].read (back + 1.0f) * w;
                    gr.age += 1.0f;
                    if (i == n - 1) shown[(size_t) k].level.store (w);
                }
                fb[0] = wl; fb[1] = wr;
                l[i] = l[i] * (1.0f - mix) + wl * mix;
                r[i] = r[i] * (1.0f - mix) + wr * mix;
            }
        }

    private:
        struct Grain { float age = 0.0f, length = 0.0f, offset = 1.0f, ratio = 1.0f, semis = 0.0f; };
        std::array<DelayLine, 2> lines;
        std::array<Grain, 2> grains;
        float fb[2] {};
        Random random { 2024 };
    };
}

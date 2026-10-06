#pragma once

#include "PulseFx2.h"

/** Pulse Space: a convolution reverb. Its spaces are worked out by the program (rooms, halls, a plate,
    a spring, a canyon, gated and blooming tails), and any audio file can be loaded as a space too. */
namespace pulse
{
    /** Makes the impulse responses of the built-in spaces. */
    struct SpaceIR
    {
        enum Style { room, plate, spring, gated, bloom, canyon };
        struct Space { const char* name; float rt; float dense; float fc0, fc1; Style style; float early; };

        /** rt: seconds to fall 60 dB; dense: seconds until the reflections run together; fc0 / fc1: how bright
            it starts and ends; early: how strong the first distinct reflections are. */
        static inline const Space spaces[] = {
            { "Small Room", 0.35f, 0.008f, 14000.0f, 5000.0f, room,   1.0f },
            { "Tiled Room", 0.8f,  0.010f, 18000.0f, 9000.0f, room,   0.9f },
            { "Studio",     0.55f, 0.014f, 12000.0f, 3500.0f, room,   0.8f },
            { "Chamber",    1.2f,  0.022f, 13000.0f, 4000.0f, room,   0.7f },
            { "Plate",      1.9f,  0.0f,   16000.0f, 6000.0f, plate,  0.0f },
            { "Spring",     1.6f,  0.0f,   6000.0f,  2500.0f, spring, 0.0f },
            { "Hall",       2.4f,  0.040f, 11000.0f, 3000.0f, room,   0.6f },
            { "Large Hall", 3.6f,  0.060f, 10000.0f, 2500.0f, room,   0.5f },
            { "Cathedral",  6.5f,  0.100f, 9000.0f,  1800.0f, room,   0.4f },
            { "Canyon",     3.0f,  0.0f,   8000.0f,  2000.0f, canyon, 0.0f },
            { "Gated",      0.45f, 0.0f,   14000.0f, 9000.0f, gated,  0.0f },
            { "Bloom",      3.0f,  0.0f,   9000.0f,  2500.0f, bloom,  0.0f } };
        static constexpr int numSpaces = 12;
        static constexpr double maxSeconds = 12.0;

        static StringArray names()
        {
            StringArray n;
            for (auto& s : spaces) n.add (s.name);
            n.add ("File");
            return n;
        }

        /** The response of a built-in space at this sample rate. size stretches it in time, decay only its tail. */
        static AudioBuffer<float> make (int space, float size, float decay, double sr)
        {
            const auto& sp = spaces[jlimit (0, numSpaces - 1, space)];
            const float rt = sp.rt * size * decay;
            double total = sp.style == gated ? sp.rt * size : sp.style == bloom ? rt * 1.45 : rt * 1.15 + 0.05;
            if (sp.style == spring) total = jmin (total, 4.0);
            const int n = jmax (64, (int) (jlimit (0.05, maxSeconds, total) * sr));
            AudioBuffer<float> buf (2, n);
            buf.clear();
            const float hpA = (float) (MathConstants<double>::twoPi * 40.0 / sr);

            for (int ch = 0; ch < 2; ++ch)
            {
                Random rng (1234 + space * 17 + ch * 7919);
                auto* d = buf.getWritePointer (ch);
                if (sp.style == spring)
                {
                    // echoes going round the spring, each smeared into a chirp by a long chain of all-passes
                    const int period = jmax (8, (int) (0.037 * size * (ch == 0 ? 1.0 : 1.07) * sr));
                    float g = 1.0f;
                    for (int i = period / 3; i < n; i += period) { d[i] = g; g *= -0.78f; }
                    for (int stage = 0; stage < 36; ++stage)
                    {
                        float x1 = 0, y1 = 0;
                        const float a = 0.62f;
                        for (int i = 0; i < n; ++i) { const float x = d[i]; const float y = -a * x + x1 + a * y1; x1 = x; y1 = y; d[i] = y; }
                    }
                }
                float lp = 0, hp = 0;
                const float bloomAt = 0.25f * rt, period = 0.21f * size, denseAt = sp.dense * size;
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) (i / sr);
                    float x = sp.style == spring ? d[i] : rng.nextFloat() * 2.0f - 1.0f;
                    if (denseAt > 0.0f && t < denseAt)   // few reflections at first, each one stronger
                    {
                        const float p = jmax (0.03f, (t / denseAt) * (t / denseAt));
                        x = rng.nextFloat() < p ? x / std::sqrt (jmax (0.08f, p)) : 0.0f;
                    }
                    float e = std::pow (10.0f, -3.0f * t / jmax (0.02f, rt));
                    switch (sp.style)
                    {
                        case gated:  e = std::pow (10.0f, -0.6f * t / (float) total); break;
                        case bloom:  e = t < bloomAt ? (t / bloomAt) * (t / bloomAt) : std::pow (10.0f, -3.0f * (t - bloomAt) / jmax (0.02f, rt)); break;
                        case canyon: e *= 0.12f + std::exp (-std::fmod (t, period) * 45.0f); break;
                        case spring: e = std::pow (10.0f, -1.2f * t / jmax (0.02f, rt)); break;   // the echoes already die away
                        default: break;
                    }
                    const float fc = sp.fc0 * std::pow (sp.fc1 / sp.fc0, jmin (1.0f, t / jmax (0.02f, rt)));
                    lp += (1.0f - std::exp (-MathConstants<float>::twoPi * jmin (fc, (float) sr * 0.45f) / (float) sr)) * (x * e - lp);
                    hp += hpA * (lp - hp);
                    d[i] = lp - hp;
                }
                if (sp.early > 0.0f)   // the first distinct reflections, later and further apart in bigger spaces
                {
                    const float spread = size * std::sqrt (sp.rt);
                    const float ms[] = { 7.0f, 11.0f, 17.0f, 23.0f, 31.0f, 41.0f, 53.0f };
                    float g = sp.early * 2.5f;
                    for (int k = 0; k < 7; ++k, g *= -0.82f)
                        if (const int at = (int) ((ms[k] + ch * 1.3f * (k + 1)) * 0.001f * spread * sr); at < n) d[at] += g;
                }
            }
            return buf;
        }

        /** Makes both channels add up to the same energy whatever the space, and fades the very end. */
        static void finish (AudioBuffer<float>& buf, double sr)
        {
            const int n = buf.getNumSamples(), fade = jmin (n / 2, (int) (0.01 * sr));
            double energy = 0;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                buf.applyGainRamp (ch, n - fade, fade, 1.0f, 0.0f);
                auto* d = buf.getReadPointer (ch);
                for (int i = 0; i < n; ++i) energy += d[i] * d[i];
            }
            energy /= jmax (1, buf.getNumChannels());
            if (energy > 1.0e-12) buf.applyGain ((float) (1.0 / std::sqrt (energy)));   // steady sound comes out as loud as it went in
        }
    };

    //==============================================================================
    class ConvolutionReverb : public PulseEffect, private Timer
    {
    public:
        enum { pSpace, pSize, pDecay, pPredelay, pLowCut, pHighCut, pWidth, pReverse, pMix };
        static inline const char* xmlTypeName = "pulseSpace";
        static const char* getPluginName() { return "Pulse Space"; }
        static constexpr int fileSpace = SpaceIR::numSpaces;

        explicit ConvolutionReverb (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("space", "Space", steps (SpaceIR::numSpaces + 1), 6.0f, fmt::choice (SpaceIR::names()));
            add ("size", "Size", skewed (0.5f, 2.0f, 1.0f), 1.0f, fmt::pct);
            add ("decay", "Decay", skewed (0.2f, 1.5f, 1.0f), 1.0f, fmt::pct);
            add ("predelay", "Predelay", skewed (0.0f, 250.0f, 30.0f), 0.0f, fmt::ms);
            add ("lowcut", "Low Cut", skewed (20.0f, 1000.0f, 150.0f), 80.0f, fmt::hz);
            add ("highcut", "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 14000.0f, fmt::hz);
            add ("width", "Width", { 0.0f, 2.0f }, 1.0f, fmt::pct);
            add ("reverse", "Reverse", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("mix", "Dry/Wet", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            filePath = state.getProperty (fileProperty).toString();
            startTimerHz (12);
        }

        ~ConvolutionReverb() override { stopTimer(); }

        String getName() const override       { return "Pulse Space"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Space", { pSpace, pSize, pDecay, pReverse } }, { "Tone", { pPredelay, pLowCut, pHighCut } }, { "Output", { pWidth, pMix } } };
        }

        /** Loads an audio file as the space (undoable) and switches to it. An empty file goes back to the Hall. */
        void setFile (const File& f)
        {
            state.setProperty (fileProperty, f == File() ? String() : f.getFullPathName(), &edit.getUndoManager());
            set (pSpace, f == File() ? 6.0f : (float) fileSpace);
            timerCallback();
        }
        File getFile() const { return state.getProperty (fileProperty).toString().isEmpty() ? File() : File (state.getProperty (fileProperty).toString()); }
        /** True when "File" is chosen and its file could be read. */
        bool usingFile() const { return fileInUse.load(); }

        /** The response in use, for the display: its loudest level in each of `shapeSize` slices (dB under its peak), and its length. */
        static constexpr int shapeSize = 400;
        void getShape (std::array<float, shapeSize>& into, double& seconds) const
        {
            const ScopedLock sl (irLock);
            into = shape;
            seconds = irSeconds;
        }

        std::atomic<float> wetDb { -100.0f };

    protected:
        void prepare() override
        {
            const ScopedLock sl (irLock);
            rebuild (true);
            conv.prepare ({ sampleRate, (uint32) jmax (16, blockSize), 2 });   // also puts the response just loaded to work at once
            wet.setSize (2, jmax (16, blockSize));
            for (auto& p : pre) p.setMax ((int) (sampleRate * 0.26) + 8);
            for (auto& b : lowCut) b.reset();
            for (auto& b : highCut) b.reset();
            lastLow = lastHigh = -1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const float mix = get (pMix), width = get (pWidth);
            const float pd = get (pPredelay) * 0.001f * (float) sampleRate;
            if (std::abs (get (pLowCut) - lastLow) > 0.5f)   { for (auto& b : lowCut) b.set (Biquad::Type::lowCut, sampleRate, get (pLowCut), 0.707, 0.0); lastLow = get (pLowCut); }
            if (std::abs (get (pHighCut) - lastHigh) > 0.5f) { for (auto& b : highCut) b.set (Biquad::Type::highCut, sampleRate, get (pHighCut), 0.707, 0.0); lastHigh = get (pHighCut); }
            float peak = 0.0f;
            for (int done = 0; done < n;)
            {
                const int num = jmin (n - done, wet.getNumSamples());
                auto* wl = wet.getWritePointer (0);
                auto* wr = wet.getWritePointer (1);
                for (int i = 0; i < num; ++i)
                {
                    pre[0].push (l[done + i]); pre[1].push (r[done + i]);
                    wl[i] = pd >= 1.0f ? pre[0].read (pd) : l[done + i];
                    wr[i] = pd >= 1.0f ? pre[1].read (pd) : r[done + i];
                }
                float* chans[] = { wl, wr };
                dsp::AudioBlock<float> block (chans, 2, (size_t) num);
                conv.process (dsp::ProcessContextReplacing<float> (block));
                for (int i = 0; i < num; ++i)
                {
                    const float a = highCut[0].process (lowCut[0].process (wl[i], 0), 0), b = highCut[1].process (lowCut[1].process (wr[i], 0), 0);
                    const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * width;
                    peak = jmax (peak, std::abs (a), std::abs (b));
                    l[done + i] = l[done + i] * (1.0f - mix) + (mid + side) * mix;
                    r[done + i] = r[done + i] * (1.0f - mix) + (mid - side) * mix;
                }
                done += num;
            }
            wetDb = jmax (Decibels::gainToDecibels (peak, -100.0f), wetDb.load() - 1.5f);
        }

    private:
        static inline const Identifier fileProperty { "irFile" };

        /** Works the response out again when a setting that shapes it has changed (never on the audio thread). */
        void rebuild (bool force)
        {
            const int space = getInt (pSpace);
            const float size = get (pSize), decay = get (pDecay);
            const bool reverse = getInt (pReverse) > 0;
            const String sig = String (space) + "|" + String (roundToInt (size * 200.0f)) + "|" + String (roundToInt (decay * 200.0f)) + "|" + String ((int) reverse)
                               + "|" + (space == fileSpace ? filePath : String()) + "|" + String (roundToInt (sampleRate));
            if (sig == builtFor && ! force) return;
            if (sig == builtFor && force && irSeconds > 0.0) { conv.loadImpulseResponse (AudioBuffer<float> (lastIR), lastIRRate, dsp::Convolution::Stereo::yes, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no); return; }
            builtFor = sig;

            AudioBuffer<float> ir;
            double irRate = sampleRate;
            bool fromFile = false;
            if (space == fileSpace && filePath.isNotEmpty())
                if (std::unique_ptr<AudioFormatReader> reader { engine.getAudioFileFormatManager().readFormatManager.createReaderFor (File (filePath)) })
                    if (reader->lengthInSamples > 16 && reader->sampleRate > 0)
                    {
                        const int len = (int) jmin ((int64) (SpaceIR::maxSeconds * reader->sampleRate), reader->lengthInSamples);
                        ir.setSize (2, len);
                        reader->read (&ir, 0, len, 0, true, true);
                        if (reader->numChannels < 2) ir.copyFrom (1, 0, ir, 0, 0, len);
                        // Decay under 100% shortens the tail; Size plays the file slower or faster (as tape would)
                        if (decay < 0.995f)
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                auto* d = ir.getWritePointer (ch);
                                const float k = (1.0f / decay - 1.0f) * 6.9f / (float) len;
                                for (int i = 0; i < len; ++i) d[i] *= std::exp (-k * (float) i);
                            }
                        irRate = reader->sampleRate / size;
                        fromFile = true;
                    }
            if (! fromFile)
                ir = SpaceIR::make (space >= fileSpace ? 6 : space, size, decay, sampleRate);   // "File" with nothing loaded sounds like the Hall
            fileInUse = fromFile;
            SpaceIR::finish (ir, irRate);
            if (reverse) ir.reverse (0, ir.getNumSamples());

            // the shape for the display
            float top = 1.0e-9f;
            std::array<float, shapeSize> pk {};
            const int len = ir.getNumSamples();
            for (int s = 0; s < shapeSize; ++s)
            {
                const int from = (int) ((int64) len * s / shapeSize), to = jmax (from + 1, (int) ((int64) len * (s + 1) / shapeSize));
                pk[(size_t) s] = ir.getMagnitude (from, jmin (len, to) - from);
                top = jmax (top, pk[(size_t) s]);
            }
            for (int s = 0; s < shapeSize; ++s) shape[(size_t) s] = Decibels::gainToDecibels (pk[(size_t) s] / top, -100.0f);
            irSeconds = len / irRate;
            lastIR = ir;
            lastIRRate = irRate;
            conv.loadImpulseResponse (std::move (ir), irRate, dsp::Convolution::Stereo::yes, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
        }

        void timerCallback() override
        {
            const ScopedLock sl (irLock);
            filePath = state.getProperty (fileProperty).toString();
            rebuild (false);
        }

        dsp::Convolution conv { dsp::Convolution::NonUniform { 1024 } };
        AudioBuffer<float> wet, lastIR;
        std::array<DelayLine, 2> pre;
        std::array<Biquad, 2> lowCut, highCut;
        float lastLow = -1.0f, lastHigh = -1.0f;
        CriticalSection irLock;
        String builtFor, filePath;
        std::array<float, shapeSize> shape {};
        double irSeconds = 0.0, lastIRRate = 48000.0;
        std::atomic<bool> fileInUse { false };
    };
}

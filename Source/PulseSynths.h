#pragma once

#include "PulseDevices.h"
#include "MidiFx.h"
#include "Sandbox.h"

/** Pulse's instruments: a wavetable synth and a Simpler-style sampler. */
namespace pulse
{
    //==============================================================================
    /** Plays everything (the voices decide what to do with a note). */
    struct AnySound : public SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    /** juce::Synthesiser with an optional monophonic mode. */
    struct PulseSynthesiser : public Synthesiser
    {
        std::function<bool()> isMono;
        /** Optional filters: return false to ignore the note (used by Drum Pads). */
        std::function<bool (int note)> acceptNoteOn, acceptNoteOff;

        void noteOff (int ch, int note, float vel, bool allowTailOff) override
        {
            if (acceptNoteOff && ! acceptNoteOff (note)) return;
            Synthesiser::noteOff (ch, note, vel, allowTailOff);
        }

        void noteOn (int ch, int note, float vel) override
        {
            if (acceptNoteOn && ! acceptNoteOn (note)) return;
            if (isMono && isMono())
                for (auto* v : voices)
                    if (v->isVoiceActive())
                        v->stopNote (0.0f, false);
            Synthesiser::noteOn (ch, note, vel);
        }
    };

    /** MIDI-in / stereo-out instrument plumbing around a juce::Synthesiser. */
    class PulseSynth : public PulsePlugin
    {
    public:
        using PulsePlugin::PulsePlugin;

        bool isSynth() override                              { return true; }
        bool takesMidiInput() override                       { return true; }
        bool takesAudioInput() override                      { return true; }
        bool producesAudioWhenNoAudioInput() override        { return true; }
        int getNumOutputChannelsGivenInputs (int) override   { return 2; }
        BusLayout getBusses() const override                 { return BusLayout::singleStereoInOut(); }
        void deinitialise() override                         {}

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            sampleRate = info.sampleRate;
            blockSize = info.blockSizeSamples;
            synth.setCurrentPlaybackSampleRate (sampleRate);
        }

        void applyToBuffer (const te::PluginRenderContext& fc) override
        {
            if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
                return;

            auto& buffer = *fc.destBuffer;
            buffer.clear (fc.bufferStartSample, fc.bufferNumSamples);

            // One buffer per thread, reused between blocks (no allocation once it has grown). Not a member:
            // a render and the live engine can both be inside the same instrument for a moment.
            static thread_local MidiBuffer midi;
            midi.clear();
            if (auto* m = fc.bufferForMidiMessages)
            {
                if (m->isAllNotesOff)
                    synth.allNotesOff (0, false);

                for (auto& msg : *m)
                {
                    const int pos = jlimit (0, fc.bufferNumSamples - 1, roundToInt (msg.getTimeStamp() * sampleRate));
                    midi.addEvent (msg, fc.bufferStartSample + pos);
                }
            }

            addExtraMidi (midi, fc.bufferStartSample, fc.bufferNumSamples);
            beforeBlock();
            synth.renderNextBlock (buffer, midi, fc.bufferStartSample, fc.bufferNumSamples);
            afterBlock (buffer, fc.bufferStartSample, fc.bufferNumSamples);
            te::sanitiseValues (buffer, fc.bufferStartSample, fc.bufferNumSamples, 4.0f);
        }

    protected:
        virtual void beforeBlock() {}
        virtual void addExtraMidi (MidiBuffer&, int /*start*/, int /*num*/) {}
        virtual void afterBlock (AudioBuffer<float>&, int /*start*/, int /*num*/) {}
        PulseSynthesiser synth;
    };

    inline ADSR::Parameters adsrFrom (float aMs, float dMs, float s, float rMs)
    {
        return { aMs * 0.001f, dMs * 0.001f, s, rMs * 0.001f };
    }

    //==============================================================================
    /** Band-limited wavetables (16 morph frames per set, one mip level per octave). */
    struct WavetableBank
    {
        static constexpr int order = 11, tableSize = 1 << order, numFrames = 16, numLevels = 11;
        static inline const StringArray setNames { "Basic shapes", "Pulse width", "Harmonics", "Vowels", "Sync saw", "User (drawn)",
                                                   "Resonant saw", "Organ", "FM bell", "FM bright", "Wavefold", "Phase reso", "Digital steps",
                                                   "Hollow to full", "Comb sweep", "Growl", "Glass" };
        static constexpr int userSet = 5;

        /** Band-limited frames morphing between two drawn single cycles (one per Wavetable instance). */
        struct UserTable
        {
            std::vector<float> data;
            const float* table (int frame, int level) const
            {
                frame = jlimit (0, numFrames - 1, frame);
                level = jlimit (0, numLevels - 1, level);
                return data.data() + ((size_t) frame * numLevels + (size_t) level) * (tableSize + 1);
            }
        };

        /** Turns two drawn cycles (any number of points, -1..1) into band-limited morph frames. */
        static std::shared_ptr<UserTable> buildUser (const std::vector<float>& a, const std::vector<float>& b)
        {
            auto u = std::make_shared<UserTable>();
            u->data.resize ((size_t) numFrames * numLevels * (tableSize + 1));
            dsp::FFT fft (order);
            auto sampleAt = [] (const std::vector<float>& w, float ph)
            {
                if (w.empty()) return std::sin (MathConstants<float>::twoPi * ph);
                const float x = ph * (float) w.size();
                const int i = (int) x;
                const float f = x - (float) i;
                return w[(size_t) (i % (int) w.size())] * (1.0f - f) + w[(size_t) ((i + 1) % (int) w.size())] * f;
            };
            for (int frame = 0; frame < numFrames; ++frame)
            {
                const float t = frame / (float) (numFrames - 1);
                std::vector<float> time ((size_t) tableSize * 2, 0.0f);
                double mean = 0.0;
                for (int i = 0; i < tableSize; ++i)
                {
                    const float ph = (float) i / tableSize;
                    time[(size_t) i] = sampleAt (a, ph) * (1.0f - t) + sampleAt (b, ph) * t;
                    mean += time[(size_t) i];
                }
                mean /= tableSize;
                for (int i = 0; i < tableSize; ++i) time[(size_t) i] -= (float) mean;   // no DC
                fft.performRealOnlyForwardTransform (time.data());
                float scale = 0.0f;
                for (int level = 0; level < numLevels; ++level)
                {
                    const int maxH = level == 0 ? 1023 : (1024 >> level);
                    std::vector<float> buf ((size_t) tableSize * 2, 0.0f);
                    for (int h = 1; h <= maxH; ++h)
                    {
                        buf[(size_t) h * 2] = time[(size_t) h * 2];
                        buf[(size_t) h * 2 + 1] = time[(size_t) h * 2 + 1];
                    }
                    fft.performRealOnlyInverseTransform (buf.data());
                    if (level == 0)
                    {
                        float peak = 0.0f;
                        for (int i = 0; i < tableSize; ++i) peak = jmax (peak, std::abs (buf[(size_t) i]));
                        scale = peak > 0.0f ? 0.9f / peak : 1.0f;
                    }
                    auto* dst = const_cast<float*> (u->table (frame, level));
                    for (int i = 0; i < tableSize; ++i) dst[i] = buf[(size_t) i] * scale;
                    dst[tableSize] = dst[0];
                }
            }
            return u;
        }
        static int numSets() { return setNames.size(); }

        static const WavetableBank& get()
        {
            static WavetableBank bank;
            return bank;
        }

        const float* table (int set, int frame, int level) const
        {
            set = jlimit (0, numSets() - 1, set);
            frame = jlimit (0, numFrames - 1, frame);
            level = jlimit (0, numLevels - 1, level);
            return data.data() + (((size_t) set * numFrames + (size_t) frame) * numLevels + (size_t) level) * (tableSize + 1);
        }

        /** Mip level with no harmonics above Nyquist for this frequency. */
        static int levelFor (double freq, double sr)
        {
            const double maxHarmonic = sr * 0.5 / jmax (1.0, freq);
            int level = 0;
            while (level < numLevels - 1 && (double) (1024 >> level) > maxHarmonic)
                ++level;
            return level;
        }

        static inline float read (const float* t, double phase) noexcept
        {
            const int i = (int) phase;
            const float f = (float) (phase - i);
            return t[i] + f * (t[i + 1] - t[i]);
        }

    private:
        std::vector<float> data;
        using Spectrum = std::vector<std::complex<float>>;   // [harmonic] = (cos amp, sin amp)

        WavetableBank()
        {
            data.resize ((size_t) numSets() * numFrames * numLevels * (tableSize + 1));
            dsp::FFT fft (order);

            for (int set = 0; set < numSets(); ++set)
                for (int frame = 0; frame < numFrames; ++frame)
                {
                    if (set == userSet) continue;   // drawn per instance (see UserTable)
                    auto spectrum = makeSpectrum (fft, set, frame / (float) (numFrames - 1));
                    float scale = 0.0f;
                    for (int level = 0; level < numLevels; ++level)
                    {
                        const int maxH = level == 0 ? 1023 : (1024 >> level);
                        std::vector<float> buf ((size_t) tableSize * 2, 0.0f);
                        for (int h = 1; h <= maxH && h < (int) spectrum.size(); ++h)
                        {
                            buf[(size_t) h * 2]     = spectrum[(size_t) h].real();
                            buf[(size_t) h * 2 + 1] = -spectrum[(size_t) h].imag();
                        }
                        fft.performRealOnlyInverseTransform (buf.data());

                        if (level == 0)   // normalise every level of a frame by the full-band peak
                        {
                            float peak = 0.0f;
                            for (int i = 0; i < tableSize; ++i) peak = jmax (peak, std::abs (buf[(size_t) i]));
                            scale = peak > 0.0f ? 0.9f / peak : 1.0f;
                        }

                        auto* t = const_cast<float*> (table (set, frame, level));
                        for (int i = 0; i < tableSize; ++i) t[i] = buf[(size_t) i] * scale;
                        t[tableSize] = t[0];
                    }
                }
        }

        static Spectrum makeSpectrum (dsp::FFT& fft, int set, float t)
        {
            Spectrum s (1024);
            auto sine = [&s] (int h, float a) { if (h > 0 && h < 1024) s[(size_t) h] += std::complex<float> (0.0f, a); };

            switch (set)
            {
                case 0:   // sine → triangle → saw → square
                {
                    auto shapeAmp = [] (int shape, int h) -> float
                    {
                        switch (shape)
                        {
                            case 0:  return h == 1 ? 1.0f : 0.0f;
                            case 1:  return (h % 2 == 1) ? (((h / 2) % 2 == 0) ? 1.0f : -1.0f) / (float) (h * h) : 0.0f;
                            case 2:  return 1.0f / (float) h;
                            default: return (h % 2 == 1) ? 1.0f / (float) h : 0.0f;
                        }
                    };
                    const float u = t * 3.0f;
                    const int seg = jmin (2, (int) u);
                    const float f = u - (float) seg;
                    for (int h = 1; h < 1024; ++h)
                        sine (h, shapeAmp (seg, h) * (1.0f - f) + shapeAmp (seg + 1, h) * f);
                    break;
                }
                case 1:   // pulse, 50% → 5% duty
                {
                    const float duty = 0.5f - t * 0.45f;
                    for (int h = 1; h < 1024; ++h)
                        s[(size_t) h] += std::complex<float> (std::sin (MathConstants<float>::pi * h * duty) / (float) h, 0.0f);
                    break;
                }
                case 2:   // additive: more and brighter harmonics as the position rises
                {
                    const int n = 1 + roundToInt (t * 40.0f);
                    for (int h = 1; h <= n; ++h)
                    {
                        const bool octave = (h & (h - 1)) == 0;
                        sine (h, (octave ? 1.5f : 1.0f) / std::pow ((float) h, 1.3f - t * 0.7f));
                    }
                    break;
                }
                case 3:   // vowel formants A → E → I → O → U on a 130 Hz pulse
                {
                    const float vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                    const float u = t * 4.0f;
                    const int seg = jmin (3, (int) u);
                    const float f = u - (float) seg;
                    float formants[3];
                    for (int k = 0; k < 3; ++k) formants[k] = vowels[seg][k] * (1.0f - f) + vowels[seg + 1][k] * f;
                    const float gains[3] = { 1.0f, 0.6f, 0.3f }, widths[3] = { 90.0f, 110.0f, 150.0f };
                    for (int h = 1; h < 200; ++h)
                    {
                        float a = 0.02f / (float) h;
                        for (int k = 0; k < 3; ++k)
                            a += gains[k] * std::exp (-0.5f * square ((h * 130.0f - formants[k]) / widths[k]));
                        s[(size_t) h] += std::complex<float> (a, 0.0f);
                    }
                    break;
                }
                case 6:   // resonant saw: a saw through a sweeping resonant low pass
                {
                    const float c = 1.5f + t * t * 70.0f, q = 5.0f;
                    for (int h = 1; h < 1024; ++h)
                    {
                        const float x = (float) h / c;
                        sine (h, 1.0f / (float) h / std::sqrt (square (1.0f - x * x) + square (x / q)));
                    }
                    break;
                }
                case 7:   // organ: drawbars coming in one after another
                {
                    const int bars[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
                    for (int k = 0; k < 9; ++k)
                    {
                        const float in = jlimit (0.0f, 1.0f, t * 9.0f - (float) k * 0.8f + 1.0f);
                        sine (bars[k], in * (k == 0 ? 1.0f : 0.7f) * ((k % 2) ? 0.8f : 1.0f));
                    }
                    break;
                }
                case 8:   // FM bell: a sine modulated by a sine twice as fast, index 0 → 8
                    fromTime (fft, s, [t] (float ph) { return std::sin (ph + t * 8.0f * std::sin (2.0f * ph)); });
                    break;
                case 9:   // FM bright: two modulators (x3 and x7)
                    fromTime (fft, s, [t] (float ph) { return std::sin (ph + t * 5.0f * std::sin (3.0f * ph) + t * 2.0f * std::sin (7.0f * ph)); });
                    break;
                case 10:  // wavefold: a sine pushed through a folder
                    fromTime (fft, s, [t] (float ph) { return std::sin ((1.0f + t * 9.0f) * MathConstants<float>::halfPi * std::sin (ph)); });
                    break;
                case 11:  // phase reso: a windowed sine at a rising multiple (the "resonant" sound of phase distortion synths)
                    fromTime (fft, s, [t] (float ph)
                    {
                        const float x = ph / MathConstants<float>::twoPi;
                        return (1.0f - x) * std::sin (ph * (1.0f + t * 15.0f));
                    });
                    break;
                case 12:  // digital steps: a sine held at fewer and fewer levels
                    fromTime (fft, s, [t] (float ph)
                    {
                        const float steps = 2.0f + (1.0f - t) * 30.0f;
                        const float held = std::floor (ph / MathConstants<float>::twoPi * steps * 2.0f) / (steps * 2.0f) * MathConstants<float>::twoPi;
                        return std::round (std::sin (held) * steps) / steps;
                    });
                    break;
                case 13:  // hollow to full: odd harmonics only, the even ones fading in
                    for (int h = 1; h < 1024; ++h) sine (h, ((h % 2) ? 1.0f : t) / (float) h);
                    break;
                case 14:  // comb sweep: a saw with moving notches
                {
                    const float period = 2.0f + t * 14.0f;
                    for (int h = 1; h < 1024; ++h)
                        sine (h, (0.08f + 0.92f * square (std::cos (MathConstants<float>::pi * (float) h / period))) / (float) h);
                    break;
                }
                case 15:  // growl: two strong formants crossing over a saw
                {
                    const float f1 = 2.0f + t * 11.0f, f2 = 22.0f - t * 14.0f;
                    for (int h = 1; h < 400; ++h)
                        sine (h, 0.15f / (float) h + std::exp (-0.5f * square (((float) h - f1) / 1.4f)) + 0.7f * std::exp (-0.5f * square (((float) h - f2) / 2.0f)));
                    break;
                }
                case 16:  // glass: harmonics with scattered levels and phases, brighter as the position rises
                {
                    Random rnd (4242);
                    for (int h = 1; h < 300; ++h)
                    {
                        const float a = (0.3f + 0.7f * rnd.nextFloat()) / std::pow ((float) h, 1.6f - t * 0.9f);
                        const float phi = rnd.nextFloat() * MathConstants<float>::twoPi;
                        s[(size_t) h] += std::complex<float> (a * std::cos (phi), a * std::sin (phi));
                    }
                    break;
                }
                default:  // hard-synced saw (ratio 1 → 8), made in the time domain
                {
                    const float ratio = 1.0f + t * 7.0f;
                    fromTime (fft, s, [ratio] (float ph)
                    {
                        const float x = ph / MathConstants<float>::twoPi * ratio;
                        return 2.0f * (x - std::floor (x)) - 1.0f;
                    });
                    break;
                }
            }
            return s;
        }

        /** A spectrum from one cycle given as a function of the phase (0..2 pi). */
        template <typename F> static void fromTime (dsp::FFT& fft, Spectrum& s, F&& f)
        {
            std::vector<float> buf ((size_t) tableSize * 2, 0.0f);
            for (int i = 0; i < tableSize; ++i)
                buf[(size_t) i] = f ((float) i / tableSize * MathConstants<float>::twoPi);
            fft.performRealOnlyForwardTransform (buf.data());
            for (int h = 1; h < 1024; ++h)
                s[(size_t) h] = std::complex<float> (buf[(size_t) h * 2], -buf[(size_t) h * 2 + 1]);
        }
    };

    //==============================================================================
    /** The flagship synth: two wavetable oscillators (warp modes, unison, sub), two filters with
        routing, an amp, filter and modulation envelope, four LFOs (drawable), a modulation matrix
        (any source to any knob - drag a source onto a knob), and built-in drive, chorus, delay
        and reverb. Parameter ids of older versions are kept, so old projects load unchanged. */
    class WavetableSynth : public PulseSynth
    {
    public:
        enum { pSet1, pPos1, pSet2, pPos2, pSemi2, pFine2, pMix, pVoices, pDetune, pSub,
               pFilterType, pCutoff, pReso, pFilterEnv, pFA, pFD, pFS, pFR, pA, pD, pS, pR,
               pLfoRate, pLfoPos, pLfoCut, pGlide, pLevel, pBendRange, pPressure, pSlide,
               // v0.47
               pWarp1, pWarpAmt1, pWarp2, pWarpAmt2,
               pF2Type, pF2Cutoff, pF2Reso, pF2Env, pRouting,
               pL1Sync, pL1Shape, pL1Retrig,
               pL2Rate, pL2Sync, pL2Shape, pL2Retrig,
               pL3Rate, pL3Sync, pL3Shape, pL3Retrig,
               pL4Rate, pL4Sync, pL4Shape, pL4Retrig,
               pMA, pMD, pMS, pMR,
               pDrive, pChorusMix, pChorusRate, pChorusDepth, pDelayMix, pDelayTime, pDelayFb, pReverbMix, pReverbSize,
               pSemi1, pFine1,   // v0.54
               // v0.55
               pOct1, pOct2, pLevel1, pLevel2, pPan1, pPan2, pSpread, pSubShape, pSubOct, pNoise, pNoiseColour,
               pFDrive, pKeyTrack, pMode, pMacro1, pMacro2, pMacro3, pMacro4,
               pDriveType, pPhaserMix, pPhaserRate, pPhaserDepth, pEqLow, pEqHigh,
               pOutput,   // v0.92: after the effects (Level is before them)
               numParamsTotal };
        static inline const char* xmlTypeName = "pulseWavetable";
        static const char* getPluginName() { return "Wavetable"; }
        static inline const StringArray filterNames { "Low pass 12", "Low pass 24", "Band pass", "High pass", "Notch" };
        static inline const StringArray subShapes { "Sine", "Triangle", "Saw", "Square" };
        static inline const StringArray driveTypes { "Soft", "Hard", "Fold", "Crush" };
        static constexpr int numMacros = 4;
        static inline const StringArray filter2Names { "Off", "Low pass 12", "Low pass 24", "Band pass", "High pass", "Notch" };
        static inline const StringArray routingNames { "Serial (1 then 2)", "Parallel", "Split (osc 1 > F1, osc 2 > F2)" };
        static inline const StringArray warpNames { "Off", "Bend +", "Bend -", "Sync", "Mirror", "Quantize", "FM from Osc 2" };
        static inline const StringArray lfoShapes { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Drawn" };
        static inline const StringArray syncNames { "Free (Hz)", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        static inline const double syncBeats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        static inline const StringArray delayNotes { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" };
        static inline const double delayBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        static constexpr int numLfos = 4, lfoPoints = 16, maxRoutes = 32;

        /** Modulation sources (index 0 = none). */
        static inline const StringArray sourceNames { "None", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Mod Env", "Filter Env", "Amp Env",
                                                      "Velocity", "Mod Wheel", "Pressure", "Key", "Random", "Slide",
                                                      "Macro 1", "Macro 2", "Macro 3", "Macro 4" };

        explicit WavetableSynth (te::PluginCreationInfo info) : PulseSynth (info)
        {
            auto setChoice = fmt::choice (WavetableBank::setNames);
            add ("set1", "Osc 1 Table", steps (WavetableBank::numSets()), 0.0f, setChoice);
            add ("pos1", "Osc 1 Position", { 0.0f, 1.0f }, 0.66f, fmt::pct);
            add ("set2", "Osc 2 Table", steps (WavetableBank::numSets()), 1.0f, setChoice);
            add ("pos2", "Osc 2 Position", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("semi2", "Osc 2 Semitones", { -24.0f, 24.0f, 1.0f }, 0.0f, fmt::st);
            add ("fine2", "Osc 2 Fine", { -50.0f, 50.0f }, 7.0f, fmt::ct);
            add ("mix", "Osc Mix", { 0.0f, 1.0f }, 0.35f, [] (float v) { return String (roundToInt ((1.0f - v) * 100)) + "/" + String (roundToInt (v * 100)); });
            add ("voices", "Unison", { 1.0f, 7.0f, 1.0f }, 3.0f, [] (float v) { return String (roundToInt (v)) + " voices"; });
            add ("detune", "Unison Detune", { 0.0f, 1.0f }, 0.25f, fmt::pct);
            add ("sub", "Sub Osc", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("ftype", "Filter Type", steps (filterNames.size()), 1.0f, fmt::choice (filterNames));
            add ("cutoff", "Cutoff", skewed (30.0f, 20000.0f, 1500.0f), 3000.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("fenv", "Filter Env", { -1.0f, 1.0f }, 0.35f, fmt::pct);
            add ("fa", "Filter Attack", skewed (0.5f, 10000.0f, 200.0f), 2.0f, fmt::ms);
            add ("fd", "Filter Decay", skewed (1.0f, 10000.0f, 400.0f), 350.0f, fmt::ms);
            add ("fs", "Filter Sustain", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("fr", "Filter Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);
            add ("a", "Attack", skewed (0.5f, 10000.0f, 200.0f), 3.0f, fmt::ms);
            add ("d", "Decay", skewed (1.0f, 10000.0f, 400.0f), 400.0f, fmt::ms);
            add ("s", "Sustain", { 0.0f, 1.0f }, 0.75f, fmt::pct);
            add ("r", "Release", skewed (1.0f, 10000.0f, 400.0f), 250.0f, fmt::ms);
            add ("lforate", "LFO 1 Rate", skewed (0.05f, 20.0f, 2.0f), 0.5f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("lfopos", "LFO 1 > Position", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("lfocut", "LFO 1 > Cutoff", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("glide", "Glide", skewed (0.0f, 1000.0f, 100.0f), 0.0f, fmt::ms);
            add ("level", "Level", { -36.0f, 6.0f }, -6.0f, fmt::db);
            // MPE / expression: per-note bend (each note on its own channel), pressure and slide (CC74)
            add ("bendrange", "Bend Range", { 1.0f, 48.0f, 1.0f }, 2.0f, fmt::st);
            add ("pressure", "Pressure", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("slide", "Slide", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            // v0.47: warp, second filter, LFOs 1-4, mod envelope, effects
            add ("warp1", "Osc 1 Warp", steps (warpNames.size()), 0.0f, fmt::choice (warpNames));
            add ("warpamt1", "Osc 1 Warp Amount", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("warp2", "Osc 2 Warp", steps (warpNames.size() - 1), 0.0f, fmt::choice ([] { auto s = warpNames; s.remove (s.size() - 1); return s; }()));
            add ("warpamt2", "Osc 2 Warp Amount", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("f2type", "Filter 2 Type", steps (filter2Names.size()), 0.0f, fmt::choice (filter2Names));
            add ("f2cutoff", "Filter 2 Cutoff", skewed (30.0f, 20000.0f, 1500.0f), 1500.0f, fmt::hz);
            add ("f2reso", "Filter 2 Resonance", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("f2env", "Filter 2 Env", { -1.0f, 1.0f }, 0.0f, fmt::pct);
            add ("routing", "Filter Routing", steps (routingNames.size()), 0.0f, fmt::choice (routingNames));
            add ("l1sync", "LFO 1 Sync", steps (syncNames.size()), 0.0f, fmt::choice (syncNames));
            add ("l1shape", "LFO 1 Shape", steps (lfoShapes.size()), 0.0f, fmt::choice (lfoShapes));
            add ("l1retrig", "LFO 1 Retrigger", steps (2), 0.0f, fmt::choice ({ "Free", "Per note" }));
            for (int k = 2; k <= numLfos; ++k)
            {
                const auto n = String (k);
                add ("l" + n + "rate", "LFO " + n + " Rate", skewed (0.05f, 20.0f, 2.0f), 1.0f, [] (float v) { return String (v, 2) + " Hz"; });
                add ("l" + n + "sync", "LFO " + n + " Sync", steps (syncNames.size()), 0.0f, fmt::choice (syncNames));
                add ("l" + n + "shape", "LFO " + n + " Shape", steps (lfoShapes.size()), 0.0f, fmt::choice (lfoShapes));
                add ("l" + n + "retrig", "LFO " + n + " Retrigger", steps (2), 0.0f, fmt::choice ({ "Free", "Per note" }));
            }
            add ("ma", "Mod Env Attack", skewed (0.5f, 10000.0f, 200.0f), 5.0f, fmt::ms);
            add ("md", "Mod Env Decay", skewed (1.0f, 10000.0f, 400.0f), 500.0f, fmt::ms);
            add ("ms", "Mod Env Sustain", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("mr", "Mod Env Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);
            add ("drive", "Drive", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("chorusmix", "Chorus", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("chorusrate", "Chorus Rate", skewed (0.05f, 5.0f, 0.8f), 0.6f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("chorusdepth", "Chorus Depth", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("delaymix", "Delay", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("delaytime", "Delay Time", steps (delayNotes.size()), 2.0f, fmt::choice (delayNotes));
            add ("delayfb", "Delay Feedback", { 0.0f, 0.95f }, 0.35f, fmt::pct);
            add ("reverbmix", "Reverb", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("reverbsize", "Reverb Size", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("semi1", "Osc 1 Semitones", { -24.0f, 24.0f, 1.0f }, 0.0f, fmt::st);
            add ("fine1", "Osc 1 Fine", { -50.0f, 50.0f }, 0.0f, fmt::ct);
            auto octText = [] (float v) { const int o = roundToInt (v); return (o > 0 ? "+" : "") + String (o) + " oct"; };
            add ("oct1", "Osc 1 Octave", { -3.0f, 3.0f, 1.0f }, 0.0f, octText);
            add ("oct2", "Osc 2 Octave", { -3.0f, 3.0f, 1.0f }, 0.0f, octText);
            add ("level1", "Osc 1 Level", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("level2", "Osc 2 Level", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            auto panText = [] (float v) { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); };
            add ("pan1", "Osc 1 Pan", { -1.0f, 1.0f }, 0.0f, panText);
            add ("pan2", "Osc 2 Pan", { -1.0f, 1.0f }, 0.0f, panText);
            add ("spread", "Unison Width", { 0.0f, 1.0f }, 0.8f, fmt::pct);
            add ("subshape", "Sub Shape", steps (subShapes.size()), 0.0f, fmt::choice (subShapes));
            add ("suboct", "Sub Octave", steps (2), 0.0f, fmt::choice ({ "-1 oct", "-2 oct" }));
            add ("noise", "Noise", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("noisecolour", "Noise Colour", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v < -0.02f ? "Dark " + fmt::pct (-v) : v > 0.02f ? "Bright " + fmt::pct (v) : String ("White"); });
            add ("fdrive", "Filter Drive", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("keytrack", "Key Tracking", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("mode", "Voice Mode", steps (2), 0.0f, fmt::choice ({ "Poly", "Mono" }));
            for (int m = 1; m <= numMacros; ++m) add ("macro" + String (m), "Macro " + String (m), { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("drivetype", "Drive Type", steps (driveTypes.size()), 0.0f, fmt::choice (driveTypes));
            add ("phasermix", "Phaser", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("phaserrate", "Phaser Rate", skewed (0.02f, 8.0f, 0.5f), 0.3f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("phaserdepth", "Phaser Depth", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("eqlow", "EQ Low", { -12.0f, 12.0f }, 0.0f, fmt::db);
            add ("eqhigh", "EQ High", { -12.0f, 12.0f }, 0.0f, fmt::db);
            add ("output", "Output", { -24.0f, 12.0f }, 0.0f, fmt::db);
            jassert (numParams() == numParamsTotal);

            WavetableBank::get();   // build the tables now rather than on the audio thread
            for (int i = 0; i < 16; ++i)
                synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            synth.isMono = [this] { return getInt (pMode) > 0; };
            userWatcher.owner = this;
            state.addListener (&userWatcher);
            rebuildUserTable();
            rebuildRoutes();
            rebuildLfoShapes();
        }

        ~WavetableSynth() override { state.removeListener (&userWatcher); }

        //==============================================================================
        // The drawn "User" table: two cycles (start / end of the Position sweep), stored in the project.
        static inline const Identifier userAID { "userWaveA" }, userBID { "userWaveB" };
        static constexpr int userPoints = 128;

        std::vector<float> getUserWave (int which) const
        {
            std::vector<float> w;
            for (auto& t : StringArray::fromTokens (state[which == 0 ? userAID : userBID].toString(), ",", {}))
                w.push_back (t.getFloatValue());
            if ((int) w.size() != userPoints)
            {
                w.resize (userPoints);
                for (int i = 0; i < userPoints; ++i)
                {
                    const float ph = (float) i / userPoints;
                    w[(size_t) i] = which == 0 ? std::sin (MathConstants<float>::twoPi * ph) : 2.0f * ph - 1.0f;   // sine -> saw
                }
            }
            return w;
        }

        void setUserWave (int which, const std::vector<float>& w)
        {
            StringArray s;
            for (auto v : w) s.add (String (jlimit (-1.0f, 1.0f, v), 4));
            state.setProperty (which == 0 ? userAID : userBID, s.joinIntoString (","), getUndoManager());
        }

        std::shared_ptr<const WavetableBank::UserTable> getUserTable() const
        {
            const SpinLock::ScopedLockType sl (userLock);
            return userTable;
        }

        /** Table for a set / frame / level (the drawn set comes from this instance). */
        const float* tableFor (int set, int frame, int level, const WavetableBank::UserTable* user) const
        {
            if (set == WavetableBank::userSet && user != nullptr) return user->table (frame, level);
            return WavetableBank::get().table (set == WavetableBank::userSet ? 0 : set, frame, level);
        }

        //==============================================================================
        // Modulation matrix: routes stored in the project ("MODMATRIX" / "ROUTE": src, dest, amount).
        static inline const Identifier matrixID { "MODMATRIX" }, routeID { "ROUTE" }, rSrc { "src" }, rDest { "dest" }, rAmt { "amount" };
        struct Route { int source = 0; int dest = -1; float amount = 0.0f; };

        std::vector<Route> getRoutes() const
        {
            std::vector<Route> out;
            for (auto r : state.getChildWithName (matrixID))
            {
                Route rt;
                rt.source = (int) r[rSrc];
                rt.dest = indexOfParam (r[rDest].toString());
                rt.amount = (float) r[rAmt];
                out.push_back (rt);
            }
            return out;
        }

        int indexOfParam (const String& id) const
        {
            for (int i = 0; i < numParams(); ++i) if (param (i)->paramID == id) return i;
            return -1;
        }

        /** Adds (or updates) a route from a source to a parameter. */
        void setRoute (int source, const String& destId, float amount)
        {
            auto* um = getUndoManager();
            auto m = state.getOrCreateChildWithName (matrixID, um);
            for (auto r : m)
                if ((int) r[rSrc] == source && r[rDest].toString() == destId) { r.setProperty (rAmt, amount, um); return; }
            if (m.getNumChildren() >= maxRoutes) return;
            ValueTree r (routeID);
            r.setProperty (rSrc, source, nullptr);
            r.setProperty (rDest, destId, nullptr);
            r.setProperty (rAmt, amount, nullptr);
            m.appendChild (r, um);
        }

        void removeRoute (int index)
        {
            auto m = state.getChildWithName (matrixID);
            if (isPositiveAndBelow (index, m.getNumChildren())) m.removeChild (index, getUndoManager());
        }

        void setRouteAt (int index, int source, const String& destId, float amount)
        {
            auto m = state.getChildWithName (matrixID);
            if (! isPositiveAndBelow (index, m.getNumChildren())) return;
            auto r = m.getChild (index);
            auto* um = getUndoManager();
            r.setProperty (rSrc, source, um);
            r.setProperty (rDest, destId, um);
            r.setProperty (rAmt, amount, um);
        }

        /** Total modulation depth on a parameter (for drawing rings on knobs). */
        float modulationDepth (int paramIndex) const
        {
            float d = 0.0f;
            for (auto& r : getRoutes()) if (r.dest == paramIndex && r.source > 0) d += r.amount;
            return d;
        }

        /** Parameters that can be modulated (continuous ones). */
        bool isModulatable (int i) const
        {
            if (i < 0 || i >= numParams()) return false;
            switch (i)
            {
                case pSet1: case pSet2: case pVoices: case pFilterType: case pF2Type: case pRouting: case pWarp1: case pWarp2:
                case pL1Sync: case pL1Shape: case pL1Retrig: case pL2Sync: case pL2Shape: case pL2Retrig:
                case pL3Sync: case pL3Shape: case pL3Retrig: case pL4Sync: case pL4Shape: case pL4Retrig:
                case pDelayTime: case pBendRange: case pGlide:
                case pOct1: case pOct2: case pSubShape: case pSubOct: case pMode: case pDriveType:
                    return false;
                default: return true;
            }
        }

        //==============================================================================
        // LFO shapes: the "Drawn" shape of each LFO is 16 points stored in the project.
        static Identifier lfoPointsID (int lfo) { return Identifier ("lfo" + String (lfo + 1) + "Points"); }

        std::vector<float> getLfoPoints (int lfo) const
        {
            std::vector<float> p;
            for (auto& t : StringArray::fromTokens (state[lfoPointsID (lfo)].toString(), ",", {})) p.push_back (t.getFloatValue());
            if ((int) p.size() != lfoPoints)
            {
                p.resize (lfoPoints);
                for (int i = 0; i < lfoPoints; ++i) p[(size_t) i] = (i % 4 == 0) ? 1.0f : (i % 4 == 2 ? -0.5f : 0.0f);   // a little rhythm
            }
            return p;
        }

        void setLfoPoints (int lfo, const std::vector<float>& pts)
        {
            StringArray s;
            for (auto v : pts) s.add (String (jlimit (-1.0f, 1.0f, v), 3));
            state.setProperty (lfoPointsID (lfo), s.joinIntoString (","), getUndoManager());
        }

        int lfoParam (int lfo, int which) const   // which: 0 rate, 1 sync, 2 shape, 3 retrig
        {
            if (lfo == 0) { const int m[] = { pLfoRate, pL1Sync, pL1Shape, pL1Retrig }; return m[which]; }
            return pL2Rate + (lfo - 1) * 4 + which;
        }

        /** An LFO's value (-1..1) at a phase (0..1); `random` is the held value for Random. */
        float lfoShape (int lfo, double phase, float random = 0.0f) const
        {
            const double ph = phase - std::floor (phase);
            switch (getInt (lfoParam (lfo, 2)))
            {
                case 1:  return (float) (1.0 - 4.0 * std::abs (ph - 0.5));
                case 2:  return (float) (2.0 * ph - 1.0);
                case 3:  return (float) (1.0 - 2.0 * ph);
                case 4:  return ph < 0.5 ? 1.0f : -1.0f;
                case 5:  return random;
                case 6:
                {
                    const double x = ph * lfoPoints;
                    const int i = (int) x % lfoPoints;
                    const float f = (float) (x - std::floor (x));
                    const auto& pts = drawnLfo[(size_t) lfo];
                    return pts[(size_t) i].load() * (1.0f - f) + pts[(size_t) ((i + 1) % lfoPoints)].load() * f;
                }
                default: return (float) std::sin (ph * MathConstants<double>::twoPi);
            }
        }

        /** An LFO's rate in Hz (synced ones follow the tempo). */
        double lfoHz (int lfo) const
        {
            const int sync = getInt (lfoParam (lfo, 1));
            return sync > 0 ? bpm.load() / 60.0 / syncBeats[jlimit (1, 7, sync)] : (double) get (lfoParam (lfo, 0));
        }

        /** The free-running phase of an LFO (shared by voices when not retriggered), for displays too. */
        double lfoClockPhase (int lfo) const { return lfoClock[(size_t) lfo].load(); }

        String getName() const override      { return "Wavetable"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 190; }

        std::vector<Section> getSections() const override
        {
            return { { "Osc 1", { pSet1, pPos1, pOct1, pSemi1, pFine1, pLevel1, pPan1, pWarp1, pWarpAmt1 } },
                     { "Osc 2", { pSet2, pPos2, pOct2, pSemi2, pFine2, pLevel2, pPan2, pWarp2, pWarpAmt2 } },
                     { "Unison / Sub", { pMix, pVoices, pDetune, pSpread, pSub, pSubShape, pSubOct, pNoise, pNoiseColour } },
                     { "Filter", { pFilterType, pCutoff, pReso, pFilterEnv, pFDrive, pKeyTrack } },
                     { "Filter 2", { pF2Type, pF2Cutoff, pF2Reso, pF2Env, pRouting } },
                     { "Filter env", { pFA, pFD, pFS, pFR } }, { "Amp env", { pA, pD, pS, pR } },
                     { "Mod env", { pMA, pMD, pMS, pMR } },
                     { "LFO 1", { pLfoRate, pL1Sync, pL1Shape, pL1Retrig, pLfoPos, pLfoCut } },
                     { "LFO 2", { pL2Rate, pL2Sync, pL2Shape, pL2Retrig } }, { "LFO 3", { pL3Rate, pL3Sync, pL3Shape, pL3Retrig } },
                     { "LFO 4", { pL4Rate, pL4Sync, pL4Shape, pL4Retrig } },
                     { "Effects", { pDrive, pDriveType, pPhaserMix, pPhaserRate, pPhaserDepth, pChorusMix, pChorusRate, pChorusDepth,
                                    pDelayMix, pDelayTime, pDelayFb, pReverbMix, pReverbSize, pEqLow, pEqHigh } },
                     { "Macros", { pMacro1, pMacro2, pMacro3, pMacro4 } },
                     { "Play", { pMode, pGlide, pBendRange, pPressure, pSlide, pLevel, pOutput } } };
        }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            PulseSynth::initialise (info);
            fx.prepare (info.sampleRate);
            outGain = -1.0f;
        }

    private:
        struct UserWatcher : public ValueTree::Listener
        {
            WavetableSynth* owner = nullptr;
            void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
            {
                if (v == owner->state && (id == userAID || id == userBID)) owner->rebuildUserTable();
                if (v == owner->state && id.toString().startsWith ("lfo") && id.toString().endsWith ("Points")) owner->rebuildLfoShapes();
                if (v.hasType (routeID)) owner->rebuildRoutes();
            }
            void valueTreeChildAdded (ValueTree& p, ValueTree&) override        { if (p.hasType (matrixID) || p == owner->state) owner->rebuildRoutes(); }
            void valueTreeChildRemoved (ValueTree& p, ValueTree&, int) override { if (p.hasType (matrixID) || p == owner->state) owner->rebuildRoutes(); }
        } userWatcher;

        void rebuildUserTable()
        {
            auto t = WavetableBank::buildUser (getUserWave (0), getUserWave (1));
            const SpinLock::ScopedLockType sl (userLock);
            userTable = t;
        }

        void rebuildRoutes()
        {
            auto r = std::make_shared<std::vector<Route>>();
            for (auto& rt : getRoutes()) if (rt.dest >= 0 && rt.source > 0 && isModulatable (rt.dest)) r->push_back (rt);
            const SpinLock::ScopedLockType sl (userLock);
            routes = r;
        }

        void rebuildLfoShapes()
        {
            for (int l = 0; l < numLfos; ++l)
            {
                auto pts = getLfoPoints (l);
                for (int i = 0; i < lfoPoints; ++i) drawnLfo[(size_t) l][(size_t) i].store (pts[(size_t) i]);
            }
        }

        void beforeBlock() override
        {
            {
                const SpinLock::ScopedTryLockType sl (userLock);
                if (sl.isLocked()) { audioUserTable = userTable; audioRoutes = routes; }
            }
            bpm.store (edit.tempoSequence.getInternalSequence().getBeatsPerSecondAt (te::TimePosition()).v * 60.0);
            // free-running LFO clocks (one block ahead; voices read them)
            for (int l = 0; l < numLfos; ++l)
            {
                double ph = lfoClock[(size_t) l].load() + lfoHz (l) * blockSize / jmax (1.0, sampleRate);
                ph -= std::floor (ph);
                lfoClock[(size_t) l].store (ph);
            }
        }

        void addExtraMidi (MidiBuffer& midi, int start, int num) override { keyboard.processNextMidiBuffer (midi, start, num, true); }

        void afterBlock (AudioBuffer<float>& b, int start, int n) override
        {
            fx.process (*this, b, start, n);
            // the output trim comes after the effects (a ramp between blocks, so turning it makes no clicks)
            if (const float target = Decibels::decibelsToGain (get (pOutput)); std::abs (target - 1.0f) > 1.0e-4f || std::abs (outGain - 1.0f) > 1.0e-4f)
            {
                if (outGain < 0.0f) outGain = target;   // the first block starts at the set value
                for (int ch = 0; ch < jmin (2, b.getNumChannels()); ++ch) b.applyGainRamp (ch, start, n, outGain, target);
                outGain = target;
            }
            activeVoices.store (countActive());
            // the output for the panel's scope and meter
            const float* l = b.getReadPointer (0, start);
            const float* r = b.getNumChannels() > 1 ? b.getReadPointer (1, start) : l;
            float pl = outPeak[0].load() * 0.9f, pr = outPeak[1].load() * 0.9f;
            int pos = scopePos.load();
            for (int i = 0; i < n; ++i)
            {
                pl = jmax (pl, std::abs (l[i]));
                pr = jmax (pr, std::abs (r[i]));
                if ((scopeSkip++ & 1) == 0) { scope[(size_t) pos].store (0.5f * (l[i] + r[i])); pos = (pos + 1) % scopeSize; }
            }
            scopePos.store (pos);
            outPeak[0].store (pl);
            outPeak[1].store (pr);
        }

        int countActive() const
        {
            int c = 0;
            for (int i = 0; i < synth.getNumVoices(); ++i) if (synth.getVoice (i)->isVoiceActive()) ++c;
            return c;
        }

    public:
        MidiKeyboardState keyboard;   // the window's keyboard: its notes join the incoming MIDI (and it lights up with it)
        std::atomic<int> activeVoices { 0 };
        std::atomic<float> lastNoteHz { 0.0f };
        static constexpr int scopeSize = 1024;
        std::array<std::atomic<float>, scopeSize> scope {};
        std::atomic<int> scopePos { 0 };
        std::array<std::atomic<float>, 2> outPeak {};

    private:
        int scopeSkip = 0;
        float outGain = -1.0f;

        mutable SpinLock userLock;
        std::shared_ptr<const WavetableBank::UserTable> userTable, audioUserTable;
        std::shared_ptr<const std::vector<Route>> routes, audioRoutes;
        std::array<std::array<std::atomic<float>, lfoPoints>, numLfos> drawnLfo {};
        std::array<std::atomic<double>, numLfos> lfoClock {};
        std::atomic<double> bpm { 120.0 };

        //==============================================================================
        /** Drive, chorus, delay and reverb after the voices. */
        struct Effects
        {
            double sr = 48000.0;
            std::vector<float> chorusBuf[2], delayBuf[2];
            std::array<std::vector<float>, 4> rev;
            std::array<int, 4> revPos {}, revLen {};
            float revLp[4] {}, delayLp[2] {};
            int chorusPos = 0, delayPos = 0;
            double chorusPhase = 0.0, phaserPhase = 0.0;
            float apState[2][6] {}, phaserFb[2] {}, eqLp[2] {}, eqLp2[2] {};

            void prepare (double sampleRate)
            {
                sr = sampleRate;
                for (auto& c : chorusBuf) c.assign ((size_t) (sr * 0.05) + 4, 0.0f);
                for (auto& d : delayBuf) d.assign ((size_t) (sr * 4.0) + 4, 0.0f);
                const int lens[] = { 1557, 1617, 1491, 1422 };
                for (int k = 0; k < 4; ++k)
                {
                    revLen[(size_t) k] = (int) (lens[k] * sr / 44100.0);
                    rev[(size_t) k].assign ((size_t) revLen[(size_t) k] * 3 + 4, 0.0f);
                    revPos[(size_t) k] = 0;
                    revLp[k] = 0.0f;
                }
                chorusPos = delayPos = 0;
                delayLp[0] = delayLp[1] = 0.0f;
            }

            void process (const WavetableSynth& o, AudioBuffer<float>& b, int start, int n)
            {
                if (b.getNumChannels() < 2 || chorusBuf[0].empty()) return;
                const float drive = o.get (pDrive), chMix = o.get (pChorusMix), dMix = o.get (pDelayMix), rMix = o.get (pReverbMix);
                const float phMix = o.get (pPhaserMix), eqLo = o.get (pEqLow), eqHi = o.get (pEqHigh);
                const bool eqOn = std::abs (eqLo) > 0.05f || std::abs (eqHi) > 0.05f;
                if (drive < 0.001f && chMix < 0.001f && dMix < 0.001f && rMix < 0.001f && phMix < 0.001f && ! eqOn) return;
                const int driveType = o.getInt (pDriveType);
                const double phInc = o.get (pPhaserRate) / sr;
                const float phDepth = o.get (pPhaserDepth);
                const float gLo = Decibels::decibelsToGain (eqLo) - 1.0f, gHi = Decibels::decibelsToGain (eqHi) - 1.0f;
                const float kLo = 1.0f - std::exp (-MathConstants<float>::twoPi * 220.0f / (float) sr), kHi = 1.0f - std::exp (-MathConstants<float>::twoPi * 4500.0f / (float) sr);
                auto shapeDrive = [driveType, drive] (float x, float dg, float comp)
                {
                    switch (driveType)
                    {
                        case 1:  return jlimit (-1.0f, 1.0f, x * dg) * (0.6f + 0.4f * comp);
                        case 2:  return std::sin (x * dg * MathConstants<float>::halfPi) ;
                        case 3:  { const float steps = std::pow (2.0f, 7.0f - drive * 5.5f); return std::round (std::tanh (x * (1.0f + drive * 3.0f)) * steps) / steps; }
                        default: return std::tanh (x * dg) * comp;
                    }
                };
                auto* L = b.getWritePointer (0, start);
                auto* R = b.getWritePointer (1, start);
                const float dg = 1.0f + drive * 12.0f, comp = 1.0f / std::tanh (dg);
                const double chInc = o.get (pChorusRate) / sr;
                const float chDepth = o.get (pChorusDepth) * (float) sr * 0.006f, chBase = (float) sr * 0.008f;
                const int csize = (int) chorusBuf[0].size(), dsize = (int) delayBuf[0].size();
                const int dTime = jlimit (1, dsize - 2, (int) (60.0 / jmax (20.0, o.bpm.load()) * delayBeats[jlimit (0, 5, o.getInt (pDelayTime))] * sr));
                const float fb = o.get (pDelayFb);
                const float size = o.get (pReverbSize);
                const float revFb = 0.70f + size * 0.27f, damp = 0.25f + (1.0f - size) * 0.4f;
                for (int k = 0; k < 4; ++k) revLen[(size_t) k] = jmin ((int) rev[(size_t) k].size() - 2, (int) ((1422 + 150 * k) * (0.5 + size) * sr / 44100.0));

                for (int i = 0; i < n; ++i)
                {
                    float l = L[i], r = R[i];
                    if (drive > 0.001f) { l = shapeDrive (l, dg, comp); r = shapeDrive (r, dg, comp); }
                    if (phMix > 0.001f)
                    {
                        // six all-pass stages swept by an LFO (the right channel a quarter cycle later)
                        float in[2] = { l, r };
                        for (int c = 0; c < 2; ++c)
                        {
                            const float m = 0.5f + 0.5f * (float) std::sin ((phaserPhase + c * 0.25) * MathConstants<double>::twoPi);
                            const float fc = 200.0f * std::pow (2.0f, (0.5f + (m - 0.5f) * phDepth) * 5.0f);
                            const float tn = std::tan (MathConstants<float>::pi * jmin (fc, (float) sr * 0.45f) / (float) sr);
                            const float a = (tn - 1.0f) / (tn + 1.0f);
                            float x = in[c] + phaserFb[c] * 0.5f;
                            for (int st = 0; st < 6; ++st)
                            {
                                const float y = a * x + apState[c][st];
                                apState[c][st] = x - a * y;
                                x = y;
                            }
                            phaserFb[c] = x;
                            in[c] = in[c] * (1.0f - phMix * 0.5f) + x * phMix * 0.5f;
                        }
                        phaserPhase += phInc;
                        phaserPhase -= std::floor (phaserPhase);
                        l = in[0]; r = in[1];
                    }
                    if (chMix > 0.001f)
                    {
                        chorusBuf[0][(size_t) chorusPos] = l;
                        chorusBuf[1][(size_t) chorusPos] = r;
                        const float m = (float) std::sin (chorusPhase * MathConstants<double>::twoPi);
                        float wet[2];
                        for (int c = 0; c < 2; ++c)
                        {
                            const float d = chBase + chDepth * (c == 0 ? m : -m);
                            const float rp = (float) chorusPos - d + (float) csize;
                            const int i0 = (int) rp % csize, i1 = (i0 + 1) % csize;
                            const float f = rp - std::floor (rp);
                            wet[c] = chorusBuf[(size_t) c][(size_t) i0] * (1.0f - f) + chorusBuf[(size_t) c][(size_t) i1] * f;
                        }
                        chorusPhase += chInc;
                        chorusPhase -= std::floor (chorusPhase);
                        chorusPos = (chorusPos + 1) % csize;
                        l = l * (1.0f - chMix * 0.5f) + wet[0] * chMix * 0.7f;
                        r = r * (1.0f - chMix * 0.5f) + wet[1] * chMix * 0.7f;
                    }
                    if (dMix > 0.001f)
                    {
                        const int rp = (delayPos - dTime + dsize) % dsize;
                        float dl = delayBuf[0][(size_t) rp], dr = delayBuf[1][(size_t) rp];
                        delayLp[0] += 0.35f * (dl - delayLp[0]);
                        delayLp[1] += 0.35f * (dr - delayLp[1]);
                        delayBuf[0][(size_t) delayPos] = l + delayLp[1] * fb;   // ping-pong
                        delayBuf[1][(size_t) delayPos] = r + delayLp[0] * fb;
                        delayPos = (delayPos + 1) % dsize;
                        l += dl * dMix;
                        r += dr * dMix;
                    }
                    if (rMix > 0.001f)
                    {
                        // four comb lines with a Householder mix and damping
                        float outs[4];
                        for (int k = 0; k < 4; ++k)
                        {
                            auto& buf = rev[(size_t) k];
                            const int len = revLen[(size_t) k], sz = (int) buf.size();
                            const int rp = (revPos[(size_t) k] - len + sz) % sz;
                            outs[k] = buf[(size_t) rp];
                        }
                        const float sum = (outs[0] + outs[1] + outs[2] + outs[3]) * 0.5f;
                        const float in = (l + r) * 0.25f;
                        for (int k = 0; k < 4; ++k)
                        {
                            auto& buf = rev[(size_t) k];
                            float x = (outs[k] - sum) * revFb;
                            revLp[k] += damp * (x - revLp[k]);
                            buf[(size_t) revPos[(size_t) k]] = in + revLp[k];
                            revPos[(size_t) k] = (revPos[(size_t) k] + 1) % (int) buf.size();
                        }
                        l += (outs[0] + outs[2]) * rMix * 0.6f;
                        r += (outs[1] + outs[3]) * rMix * 0.6f;
                    }
                    if (eqOn)
                    {
                        eqLp[0] += kLo * (l - eqLp[0]);  eqLp[1] += kLo * (r - eqLp[1]);
                        eqLp2[0] += kHi * (l - eqLp2[0]); eqLp2[1] += kHi * (r - eqLp2[1]);
                        l += gLo * eqLp[0] + gHi * (l - eqLp2[0]);
                        r += gLo * eqLp[1] + gHi * (r - eqLp2[1]);
                    }
                    L[i] = l;
                    R[i] = r;
                }
            }
        } fx;

        //==============================================================================
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (WavetableSynth& s) : o (s) {}

            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                const float target = (float) MidiMessage::getMidiNoteInHertz (note);
                // glide comes from the last note played (on any voice), or from this voice's pitch when it is retaken
                const float from = isVoiceActive() && lastFreq > 0.0f ? lastFreq : o.lastNoteHz.load();
                const bool glide = o.get (pGlide) > 0.5f && from > 0.0f;
                freq = glide ? from : target;
                o.lastNoteHz.store (target);
                targetFreq = target;
                velocity = vel;
                noteNumber = note;
                pitchWheelMoved (pitchWheel);
                Random r;
                for (auto& oscPhases : phases)
                    for (auto& p : oscPhases) p = r.nextDouble() * WavetableBank::tableSize;
                subPhase = 0.0;
                randomValue = r.nextFloat();
                for (int l = 0; l < numLfos; ++l)
                {
                    lfoPhase[l] = o.getInt (o.lfoParam (l, 3)) > 0 ? 0.0 : o.lfoClockPhase (l);
                    lfoRandom[l] = r.nextFloat() * 2.0f - 1.0f;
                }
                pressure = 0.0f;
                slide = 0.5f;
                for (auto& f : filter) f.reset();
                for (auto* e : { &amp, &fenv, &menv }) { e->setSampleRate (getSampleRate()); e->reset(); e->noteOn(); }
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (allowTailOff) { amp.noteOff(); fenv.noteOff(); menv.noteOff(); }
                else              { clearCurrentNote(); amp.reset(); }
            }

            void pitchWheelMoved (int v) override         { bend = (float) (v - 8192) / 8192.0f * o.get (pBendRange); }
            void controllerMoved (int cc, int v) override  { if (cc == 74) slide = (float) v / 127.0f; if (cc == 1) modWheel = (float) v / 127.0f; }
            void channelPressureChanged (int v) override   { pressure = (float) v / 127.0f; }
            void aftertouchChanged (int v) override        { pressure = (float) v / 127.0f; }

            /** A parameter with this voice's modulation added (in the parameter's 0..1 range). */
            float mv (int p) const
            {
                const float base = o.get (p);
                const float off = offsets[(size_t) p];
                if (off == 0.0f) return base;
                const auto& range = o.param (p)->valueRange;
                return range.convertFrom0to1 (jlimit (0.0f, 1.0f, range.convertTo0to1 (base) + off));
            }

            static float warpPhase (int mode, float amt, float ph) noexcept   // ph in 0..1
            {
                switch (mode)
                {
                    case 1: return std::pow (ph, 1.0f + amt * 4.0f);
                    case 2: return 1.0f - std::pow (1.0f - ph, 1.0f + amt * 4.0f);
                    case 3: { const float s = ph * (1.0f + amt * 7.0f); return s - std::floor (s); }
                    case 4: { const float m = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f; return ph + (m * 0.5f - ph) * amt; }
                    case 5: { const float steps = 2.0f + (1.0f - amt) * 62.0f; return std::floor (ph * steps) / steps; }
                    default: return ph;
                }
            }

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive())
                    return;

                const double sr = getSampleRate();
                amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));
                menv.setParameters (adsrFrom (o.get (pMA), o.get (pMD), o.get (pMS), o.get (pMR)));
                auto routes = o.audioRoutes;

                const int set[2] = { o.getInt (pSet1), o.getInt (pSet2) };
                const int warp[2] = { o.getInt (pWarp1), o.getInt (pWarp2) };
                const int voices = jlimit (1, 7, o.getInt (pVoices));
                const int ftype = o.getInt (pFilterType), f2type = o.getInt (pF2Type), routing = o.getInt (pRouting);
                const float pressureAmt = o.get (pPressure), slideAmt = o.get (pSlide);
                const float glideMs = o.get (pGlide);
                const float uniGain = 1.0f / std::sqrt ((float) voices);
                const int mode = ftype == 2 ? 1 : ftype == 3 ? 2 : 0;
                const int mode2 = f2type == 3 ? 1 : f2type == 4 ? 2 : 0;
                double lfoInc[numLfos];
                for (int l = 0; l < numLfos; ++l) lfoInc[l] = o.lfoHz (l) / sr;

                constexpr int chunk = 32;
                for (int c = 0; c < num; c += chunk)
                {
                    const int n = jmin (chunk, num - c);

                    if (glideMs > 0.5f && std::abs (freq - targetFreq) > 0.01f)
                    {
                        const float k = 1.0f - std::exp (-(float) n / (float) (glideMs * 0.001 * sr));
                        freq += (targetFreq - freq) * k;
                    }
                    else freq = targetFreq;
                    lastFreq = freq;

                    // modulation sources for this chunk
                    float lfo[numLfos];
                    for (int l = 0; l < numLfos; ++l)
                    {
                        const double before = lfoPhase[l];
                        lfoPhase[l] += lfoInc[l] * n;
                        if (std::floor (lfoPhase[l]) != std::floor (before)) lfoRandom[l] = rng.nextFloat() * 2.0f - 1.0f;
                        lfoPhase[l] -= std::floor (lfoPhase[l]);
                        lfo[l] = o.lfoShape (l, lfoPhase[l], lfoRandom[l]);
                    }
                    const float envNow = fenv.getNextSample();
                    for (int i = 1; i < n; ++i) fenv.getNextSample();
                    const float modEnv = menv.getNextSample();
                    for (int i = 1; i < n; ++i) menv.getNextSample();
                    const float src[] = { 0.0f, lfo[0], lfo[1], lfo[2], lfo[3], modEnv, envNow, lastAmp, velocity, modWheel, pressure,
                                          (noteNumber - 60) / 60.0f, randomValue, (slide - 0.5f) * 2.0f,
                                          o.get (pMacro1), o.get (pMacro2), o.get (pMacro3), o.get (pMacro4) };
                    std::fill (offsets.begin(), offsets.end(), 0.0f);
                    if (routes != nullptr)
                        for (auto& r : *routes)
                            if (isPositiveAndBelow (r.source, (int) std::size (src)) && isPositiveAndBelow (r.dest, (int) offsets.size()))
                                offsets[(size_t) r.dest] += r.amount * src[r.source];

                    const float slideOffset = (slide - 0.5f) * 2.0f * slideAmt;
                    const float basePos[2] = { jlimit (0.0f, 1.0f, mv (pPos1) + slideOffset), jlimit (0.0f, 1.0f, mv (pPos2) + slideOffset) };
                    const float mixV = mv (pMix);
                    const float oscGain[2] = { (1.0f - mixV) * mv (pLevel1), mixV * mv (pLevel2) };
                    const double osc2Ratio = std::pow (2.0, o.getInt (pOct2) + (mv (pSemi2) + mv (pFine2) / 100.0) / 12.0);
                    const double osc1Ratio = std::pow (2.0, o.getInt (pOct1) + (mv (pSemi1) + mv (pFine1) / 100.0) / 12.0);
                    float oscPanL[2], oscPanR[2];
                    for (int k = 0; k < 2; ++k)
                    {
                        const float ang = (jlimit (-1.0f, 1.0f, mv (k == 0 ? pPan1 : pPan2)) + 1.0f) * MathConstants<float>::pi * 0.25f;
                        oscPanL[k] = std::cos (ang) * 1.41421f;
                        oscPanR[k] = std::sin (ang) * 1.41421f;
                    }
                    const float spreadAmt = mv (pSpread), noiseLvl = mv (pNoise), noiseCol = mv (pNoiseColour);
                    const float fdrive = mv (pFDrive), fdg = 1.0f + fdrive * 6.0f, fdc = 1.0f / (1.0f + fdrive * 1.5f);
                    const float keyTrack = std::pow (2.0f, (noteNumber - 60) / 12.0f * mv (pKeyTrack));
                    const float detune = mv (pDetune), sub = mv (pSub);
                    const float warpAmt[2] = { mv (pWarpAmt1), mv (pWarpAmt2) };
                    const float level = Decibels::decibelsToGain (mv (pLevel)) * (0.3f + 0.7f * velocity) * (1.0f + pressure * pressureAmt * 0.6f);
                    const float lfoPos = o.get (pLfoPos), lfoCut = o.get (pLfoCut);

                    float cents[7], panL[7], panR[7];
                    for (int u = 0; u < voices; ++u)
                    {
                        const float spread = voices > 1 ? (float) u / (voices - 1) * 2.0f - 1.0f : 0.0f;
                        cents[u] = spread * detune * 50.0f;
                        const float p = spread * spreadAmt;
                        panL[u] = std::sqrt (0.5f * (1.0f - p));
                        panR[u] = std::sqrt (0.5f * (1.0f + p));
                    }

                    const double f = freq * std::pow (2.0, bend / 12.0);
                    const float* t0[2]; const float* t1[2]; float frameFrac[2]; double inc[2][7];
                    for (int osc = 0; osc < 2; ++osc)
                    {
                        const double of = osc == 0 ? f * osc1Ratio : f * osc2Ratio;
                        const float pos = jlimit (0.0f, 1.0f, basePos[osc] + lfo[0] * lfoPos * 0.5f) * (WavetableBank::numFrames - 1);
                        const int fr = jmin (WavetableBank::numFrames - 2, (int) pos);
                        frameFrac[osc] = pos - (float) fr;
                        // sync / quantize add harmonics: take a darker table so they stay clean
                        const double syncMul = (warp[osc] == 3) ? 1.0 + warpAmt[osc] * 7.0 : 1.0;
                        const int lev = WavetableBank::levelFor (of * syncMul * std::pow (2.0, detune * 50.0 / 1200.0), sr);
                        t0[osc] = o.tableFor (set[osc], fr, lev, o.audioUserTable.get());
                        t1[osc] = o.tableFor (set[osc], fr + 1, lev, o.audioUserTable.get());
                        for (int u = 0; u < voices; ++u)
                            inc[osc][u] = of * std::pow (2.0, cents[u] / 1200.0) / sr * WavetableBank::tableSize;
                    }

                    // filters
                    const float octaves = mv (pFilterEnv) * envNow * 6.0f + lfo[0] * lfoCut * 3.0f + pressure * pressureAmt * 3.0f;
                    const float fc = jlimit (30.0f, 20000.0f, mv (pCutoff) * keyTrack * std::pow (2.0f, octaves));
                    const float reso = mv (pReso);
                    filter[0].set (fc, reso, sr);
                    filter[1].set (fc, reso * 0.5f, sr);
                    const float fc2 = jlimit (30.0f, 20000.0f, mv (pF2Cutoff) * std::pow (2.0f, mv (pF2Env) * envNow * 6.0f));
                    const float reso2 = mv (pF2Reso);
                    filter[2].set (fc2, reso2, sr);
                    filter[3].set (fc2, reso2 * 0.5f, sr);
                    const double subHz = f * (o.getInt (pSubOct) > 0 ? 0.25 : 0.5);
                    const double subInc = subHz / sr;
                    const int subFrames[] = { 0, 5, 10, 15 };   // sine, triangle, saw, square in the basic shapes
                    const float* subTable = WavetableBank::get().table (0, subFrames[jlimit (0, 3, o.getInt (pSubShape))], WavetableBank::levelFor (subHz, sr));
                    const float noiseK = 1.0f - std::exp (-MathConstants<float>::twoPi * 900.0f / (float) sr);
                    const float fmAmt = warp[0] == 6 ? warpAmt[0] : 0.0f;

                    for (int i = 0; i < n; ++i)
                    {
                        float oL[2] = { 0, 0 }, oR[2] = { 0, 0 };
                        float osc2Mono = 0.0f;
                        for (int osc = 1; osc >= 0; --osc)   // osc 2 first (it can modulate osc 1)
                        {
                            const bool silent = oscGain[osc] < 0.001f && ! (osc == 1 && fmAmt > 0.0f);
                            for (int u = 0; u < voices; ++u)
                            {
                                auto& ph = phases[osc][u];
                                if (! silent)
                                {
                                    float rp = (float) ph;
                                    if (warp[osc] > 0 && warp[osc] < 6)
                                        rp = warpPhase (warp[osc], warpAmt[osc], rp / WavetableBank::tableSize) * WavetableBank::tableSize;
                                    if (osc == 0 && fmAmt > 0.0f)
                                    {
                                        rp += osc2Mono * fmAmt * WavetableBank::tableSize * 0.5f;
                                        rp -= std::floor (rp / WavetableBank::tableSize) * WavetableBank::tableSize;
                                    }
                                    rp = jlimit (0.0f, (float) WavetableBank::tableSize - 0.001f, rp);
                                    const float a = WavetableBank::read (t0[osc], rp), b = WavetableBank::read (t1[osc], rp);
                                    const float s = (a + frameFrac[osc] * (b - a));
                                    if (osc == 1 && u == 0) osc2Mono = s;
                                    const float sOut = s * oscGain[osc] * uniGain;
                                    oL[osc] += sOut * panL[u];
                                    oR[osc] += sOut * panR[u];
                                }
                                advance (ph, inc[osc][u]);
                            }
                            oL[osc] *= oscPanL[osc];
                            oR[osc] *= oscPanR[osc];
                        }
                        float l = oL[0] + oL[1], r = oR[0] + oR[1];
                        if (noiseLvl > 0.001f)
                        {
                            float nz[2];
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                const float w = rng.nextFloat() * 2.0f - 1.0f;
                                noiseLp[ch] += noiseK * (w - noiseLp[ch]);
                                nz[ch] = (noiseCol < 0.0f ? w + (noiseLp[ch] * 3.0f - w) * -noiseCol : w - noiseLp[ch] * noiseCol) * noiseLvl * 0.4f;
                            }
                            l += nz[0]; r += nz[1]; oL[0] += nz[0]; oR[0] += nz[1];
                        }
                        if (sub > 0.001f)
                        {
                            const float sv = WavetableBank::read (subTable, subPhase * WavetableBank::tableSize) * sub * 0.78f;
                            l += sv; r += sv; oL[0] += sv; oR[0] += sv;
                            subPhase += subInc;
                            subPhase -= std::floor (subPhase);
                        }

                        auto f1 = [&] (float x, int ch)
                        {
                            if (fdrive > 0.001f) x = std::tanh (x * fdg) * fdc * (1.0f + fdrive);
                            if (ftype == 4) return x - filter[0].process (x, ch, 1);   // notch
                            x = filter[0].process (x, ch, mode);
                            if (ftype == 1) x = filter[1].process (x, ch, 0);
                            return x;
                        };
                        auto f2 = [&] (float x, int ch)
                        {
                            if (f2type == 0) return x;
                            if (f2type == 5) return x - filter[2].process (x, ch, 1);   // notch: everything but the band
                            x = filter[2].process (x, ch, mode2);
                            if (f2type == 2) x = filter[3].process (x, ch, 0);
                            return x;
                        };
                        if (f2type == 0 || routing == 0)      { l = f2 (f1 (l, 0), 0); r = f2 (f1 (r, 1), 1); }
                        else if (routing == 1)                { l = 0.5f * (f1 (l, 0) + f2 (l, 0)) * 1.4f; r = 0.5f * (f1 (r, 1) + f2 (r, 1)) * 1.4f; }
                        else                                  { l = f1 (oL[0], 0) + f2 (oL[1], 0); r = f1 (oR[0], 1) + f2 (oR[1], 1); }

                        lastAmp = amp.getNextSample();
                        const float e = lastAmp * level;
                        out.addSample (0, start + c + i, l * e);
                        if (out.getNumChannels() > 1)
                            out.addSample (1, start + c + i, r * e);
                    }

                    if (! amp.isActive())
                    {
                        clearCurrentNote();
                        return;
                    }
                }
            }

            static inline void advance (double& ph, double inc) noexcept
            {
                ph += inc;
                if (ph >= WavetableBank::tableSize) ph -= WavetableBank::tableSize;
            }

            WavetableSynth& o;
            double phases[2][7] {};
            double subPhase = 0, lfoPhase[numLfos] {};
            float lfoRandom[numLfos] {};
            float noiseLp[2] {};
            float freq = 440, targetFreq = 440, lastFreq = 0, velocity = 1, bend = 0, pressure = 0, slide = 0.5f, modWheel = 0, randomValue = 0, lastAmp = 0;
            int noteNumber = 60;
            ADSR amp, fenv, menv;
            SVF filter[4];
            std::array<float, numParamsTotal> offsets {};
            Random rng;
        };
    };

    //==============================================================================
    /** A decoded sample plus a min/max overview for drawing. */
    struct SampleData
    {
        AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        File file;
        std::vector<std::pair<float, float>> overview;   // 1024 min/max pairs

        static std::shared_ptr<SampleData> load (te::Engine& engine, const File& f)
        {
            std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
            if (r == nullptr || r->lengthInSamples <= 0)
                return {};

            auto d = std::make_shared<SampleData>();
            const int len = (int) jmin<int64> (r->lengthInSamples, (int64) (r->sampleRate * 600.0));
            d->buffer.setSize (2, len);
            r->read (&d->buffer, 0, len, 0, true, true);
            if (r->numChannels == 1)
                d->buffer.copyFrom (1, 0, d->buffer, 0, 0, len);
            d->sampleRate = r->sampleRate;
            d->file = f;

            constexpr int points = 1024;
            d->overview.resize (points);
            for (int p = 0; p < points; ++p)
            {
                const int s0 = (int) ((int64) p * len / points), s1 = jmax (s0 + 1, (int) ((int64) (p + 1) * len / points));
                float lo = 0, hi = 0;
                for (int i = s0; i < s1 && i < len; ++i)
                {
                    const float v = 0.5f * (d->buffer.getSample (0, i) + d->buffer.getSample (1, i));
                    lo = jmin (lo, v); hi = jmax (hi, v);
                }
                d->overview[(size_t) p] = { lo, hi };
            }
            return d;
        }
    };

    /** Simpler-style sampler: one sample, start/end, loop with crossfade, ADSR, filter. */
    class Simpler : public PulseSynth
    {
    public:
        enum { pStart, pEnd, pLoop, pLoopStart, pXfade, pRoot, pTranspose, pFine, pMode,
               pA, pD, pS, pR, pFilterType, pCutoff, pReso, pVelSens, pGain, pPan,
               pGrainSize, pDensity, pSpray, pGrainPos, pFEnv, pFA, pFD, pFS, pFR };
        static inline const char* xmlTypeName = "pulseSimpler";
        static const char* getPluginName() { return "Simpler"; }
        static inline const Identifier sampleFileID { "sampleFile" };
        static inline const StringArray modeNames { "Classic", "One-shot", "Mono", "Granular" };
        static inline const StringArray filterNames { "Off", "Low pass", "Band pass", "High pass" };

        explicit Simpler (te::PluginCreationInfo info) : PulseSynth (info)
        {
            add ("start", "Start", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("end", "End", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("loop", "Loop", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("loopstart", "Loop Start", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("xfade", "Crossfade", { 0.0f, 0.5f }, 0.02f, fmt::pct);
            add ("root", "Root Note", { 0.0f, 127.0f, 1.0f }, 60.0f, [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); });
            add ("transpose", "Transpose", { -48.0f, 48.0f, 1.0f }, 0.0f, fmt::st);
            add ("fine", "Detune", { -100.0f, 100.0f }, 0.0f, fmt::ct);
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("a", "Attack", skewed (0.1f, 10000.0f, 200.0f), 0.5f, fmt::ms);
            add ("d", "Decay", skewed (1.0f, 10000.0f, 400.0f), 600.0f, fmt::ms);
            add ("s", "Sustain", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("r", "Release", skewed (1.0f, 10000.0f, 400.0f), 80.0f, fmt::ms);
            add ("ftype", "Filter", steps (filterNames.size()), 0.0f, fmt::choice (filterNames));
            add ("cutoff", "Cutoff", skewed (30.0f, 20000.0f, 1500.0f), 8000.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("velsens", "Velocity", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("gain", "Gain", { -36.0f, 12.0f }, 0.0f, fmt::db);
            add ("pan", "Pan", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); });
            add ("grainsize", "Grain Size", skewed (10.0f, 500.0f, 80.0f), 80.0f, fmt::ms);
            add ("density", "Density", skewed (2.0f, 100.0f, 20.0f), 20.0f, [] (float v) { return String (v, 1) + " /s"; });
            add ("spray", "Spray", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("grainpos", "Scan", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("fenv", "Filter Env", { -1.0f, 1.0f }, 0.0f, fmt::pct);
            add ("fa", "Filter Attack", skewed (0.5f, 10000.0f, 200.0f), 2.0f, fmt::ms);
            add ("fd", "Filter Decay", skewed (1.0f, 10000.0f, 400.0f), 400.0f, fmt::ms);
            add ("fs", "Filter Sustain", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("fr", "Filter Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);

            for (int i = 0; i < 16; ++i)
                synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            synth.isMono = [this] { return getInt (pMode) == 2; };

            stateWatcher.owner = this;
            state.addListener (&stateWatcher);
            reloadFromState();
            reloadZones();
        }

        ~Simpler() override { state.removeListener (&stateWatcher); }

        String getName() const override      { return "Simpler"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 320; }

        std::vector<Section> getSections() const override
        {
            return { { "Region", { pStart, pEnd, pLoop, pLoopStart, pXfade } }, { "Pitch", { pRoot, pTranspose, pFine, pMode } },
                     { "Amp env", { pA, pD, pS, pR } }, { "Filter", { pFilterType, pCutoff, pReso, pFEnv } },
                     { "Filter env", { pFA, pFD, pFS, pFR } },
                     { "Output", { pVelSens, pGain, pPan } }, { "Granular", { pGrainSize, pDensity, pSpray, pGrainPos } } };
        }

        /** Loads a sample (undoable; stored in the project as the file path). */
        void setSampleFile (const File& f)
        {
            state.setProperty (sampleFileID, f.getFullPathName(), getUndoManager());
        }

        //==============================================================================
        // Multi-sample zones: when there are zones, each note / velocity plays the matching
        // zone's sample, pitched from the zone's root key (the single sample is used otherwise).
        static inline const Identifier zoneType { "ZONE" }, zFile { "file" }, zKeyLo { "keylo" }, zKeyHi { "keyhi" },
                                       zVelLo { "vello" }, zVelHi { "velhi" }, zRoot { "root" };

        struct ZoneInfo { File file; int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127, root = 60; };

        int numZones() const
        {
            int n = 0;
            for (auto c : state) if (c.hasType (zoneType)) ++n;
            return n;
        }

        ZoneInfo getZone (int i) const
        {
            for (auto c : state)
                if (c.hasType (zoneType) && i-- == 0)
                    return { File (c[zFile].toString()), (int) c[zKeyLo], (int) c[zKeyHi], (int) c.getProperty (zVelLo, 1),
                             (int) c.getProperty (zVelHi, 127), (int) c[zRoot] };
            return {};
        }

        void addZone (const ZoneInfo& z)
        {
            ValueTree v (zoneType);
            v.setProperty (zFile, z.file.getFullPathName(), nullptr);
            v.setProperty (zKeyLo, z.keyLo, nullptr);
            v.setProperty (zKeyHi, z.keyHi, nullptr);
            v.setProperty (zVelLo, z.velLo, nullptr);
            v.setProperty (zVelHi, z.velHi, nullptr);
            v.setProperty (zRoot, z.root, nullptr);
            state.appendChild (v, getUndoManager());
        }

        void clearZones()
        {
            for (int i = state.getNumChildren(); --i >= 0;)
                if (state.getChild (i).hasType (zoneType))
                    state.removeChild (i, getUndoManager());
        }

        /** Root key from a file name: "Piano C#3", "Strings_A2", "vln-60" (MIDI number); -1 = none. */
        static int rootFromName (const String& name)
        {
            static const char* letters = "C D EF G A B";
            const auto n = name.toUpperCase();
            for (int i = n.length(); --i >= 0;)   // the last note name in the string wins
            {
                const juce_wchar ch = n[i];
                const char* pos = std::strchr (letters, (char) ch);
                if (ch < 'A' || ch > 'G' || pos == nullptr) continue;
                if (i > 0 && CharacterFunctions::isLetter (n[i - 1])) continue;
                int semis = (int) (pos - letters), k = i + 1;
                if (n[k] == '#') { ++semis; ++k; }
                else if (n[k] == 'B' && (CharacterFunctions::isDigit (n[k + 1]) || n[k + 1] == '-')) { --semis; ++k; }
                bool neg = false;
                if (n[k] == '-') { neg = true; ++k; }
                if (! CharacterFunctions::isDigit (n[k])) continue;
                const int octave = (n[k] - '0') * (neg ? -1 : 1);
                if (CharacterFunctions::isDigit (n[k + 1])) continue;   // "A10" etc: not a note
                return jlimit (0, 127, (octave + 2) * 12 + semis);       // C3 = 60
            }
            // a trailing MIDI number ("_60")
            auto digits = n.retainCharacters ("0123456789_- ").trim();
            auto tail = n.fromLastOccurrenceOf ("_", false, false).trim();
            if (tail.isNotEmpty() && tail.containsOnly ("0123456789") && tail.getIntValue() <= 127) return tail.getIntValue();
            ignoreUnused (digits);
            return -1;
        }

        /** Maps several samples across the keyboard by their root keys (read from the file names;
            files without one are spread upwards from C3). Each zone reaches halfway to its neighbours. */
        void autoMap (const juce::Array<File>& files)
        {
            struct Item { File f; int root; };
            std::vector<Item> items;
            int next = 60;
            for (auto& f : files)
            {
                int r = rootFromName (f.getFileNameWithoutExtension());
                if (r < 0) r = next++;
                items.push_back ({ f, r });
            }
            std::sort (items.begin(), items.end(), [] (auto& a, auto& b) { return a.root < b.root; });
            clearZones();
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int lo = i == 0 ? 0 : (items[i - 1].root + items[i].root) / 2 + 1;
                const int hi = i + 1 == items.size() ? 127 : (items[i].root + items[i + 1].root) / 2;
                addZone ({ items[i].f, lo, jmax (lo, hi), 1, 127, items[i].root });
            }
        }

        struct LoadedZone { std::shared_ptr<SampleData> data; int keyLo, keyHi, velLo, velHi, root; };
        std::shared_ptr<const std::vector<LoadedZone>> getZones() const
        {
            const SpinLock::ScopedLockType sl (sampleLock);
            return zones;
        }

        std::shared_ptr<SampleData> getSample() const
        {
            const SpinLock::ScopedLockType sl (sampleLock);
            return sample;
        }

        /** Increments whenever a new sample is loaded (for the display). */
        std::atomic<int> sampleVersion { 0 };

        /** Normalised positions of playing voices (for the display), -1 = idle. */
        std::array<std::atomic<float>, 4> playheads { -1.0f, -1.0f, -1.0f, -1.0f };

    private:
        struct StateWatcher : public ValueTree::Listener
        {
            Simpler* owner = nullptr;
            void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
            {
                if (v == owner->state && id == sampleFileID)
                    owner->reloadFromState();
                else if (v.hasType (zoneType) && v.getParent() == owner->state)
                    owner->reloadZones();
            }
            void valueTreeChildAdded (ValueTree& p, ValueTree& c) override        { if (p == owner->state && c.hasType (zoneType)) owner->reloadZones(); }
            void valueTreeChildRemoved (ValueTree& p, ValueTree& c, int) override { if (p == owner->state && c.hasType (zoneType)) owner->reloadZones(); }
        } stateWatcher;

        void reloadZones()
        {
            auto old = getZones();
            auto set = std::make_shared<std::vector<LoadedZone>>();
            for (int i = 0; i < numZones(); ++i)
            {
                auto z = getZone (i);
                std::shared_ptr<SampleData> d;
                if (old != nullptr)
                    for (auto& o : *old) if (o.data != nullptr && o.data->file == z.file) { d = o.data; break; }
                if (d == nullptr && z.file.existsAsFile()) d = SampleData::load (engine, z.file);
                if (d != nullptr) set->push_back ({ d, z.keyLo, z.keyHi, z.velLo, z.velHi, z.root });
            }
            {
                const SpinLock::ScopedLockType sl (sampleLock);
                zones = set->empty() ? nullptr : set;
            }
            ++sampleVersion;
        }

        void reloadFromState()
        {
            const auto path = state[sampleFileID].toString();
            std::shared_ptr<SampleData> d;
            if (path.isNotEmpty() && File::isAbsolutePath (path) && File (path).existsAsFile())
                d = SampleData::load (engine, File (path));
            {
                const SpinLock::ScopedLockType sl (sampleLock);
                sample = d;
            }
            ++sampleVersion;
        }

        void beforeBlock() override
        {
            const SpinLock::ScopedTryLockType sl (sampleLock);
            if (sl.isLocked())
            {
                audioSample = sample;
                audioZones = zones;
            }
            for (auto& p : playheads) p.store (-1.0f);
        }

        struct Voice : public SynthesiserVoice
        {
            explicit Voice (Simpler& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                data = o.audioSample;
                zoneRoot = -1;
                if (auto& zs = o.audioZones; zs != nullptr)
                {
                    const int v = jlimit (1, 127, roundToInt (vel * 127.0f));
                    const LoadedZone* best = nullptr;
                    int bestDist = 1000;
                    for (auto& z : *zs)
                    {
                        if (note >= z.keyLo && note <= z.keyHi && v >= z.velLo && v <= z.velHi) { best = &z; break; }
                        const int d = note < z.keyLo ? z.keyLo - note : note > z.keyHi ? note - z.keyHi : 0;
                        if (d < bestDist) { bestDist = d; best = &z; }
                    }
                    if (best != nullptr) { data = best->data; zoneRoot = best->root; }
                }
                if (data == nullptr || data->buffer.getNumSamples() < 2)
                {
                    clearCurrentNote();
                    return;
                }
                noteNumber = note;
                velocity = vel;
                pitchWheelMoved (pitchWheel);
                const int len = data->buffer.getNumSamples();
                pos = jlimit (0.0, (double) len - 2, (double) jmin (o.get (pStart), o.get (pEnd)) * len);
                filter.reset();
                amp.setSampleRate (getSampleRate());
                amp.reset();
                amp.noteOn();
                fenv.setSampleRate (getSampleRate());
                fenv.reset();
                fenv.noteOn();
                releasing = false;
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (! allowTailOff) { clearCurrentNote(); amp.reset(); data.reset(); return; }
                if (o.getInt (pMode) == 1) return;   // one-shot plays to the end
                amp.noteOff();
                fenv.noteOff();
                releasing = true;
            }

            void pitchWheelMoved (int v) override   { bend = (float) (v - 8192) / 8192.0f * 2.0f; }
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive() || data == nullptr)
                    return;

                if (o.getInt (pMode) == 3) { renderGranular (out, start, num); return; }

                const int len = data->buffer.getNumSamples();
                const float* L = data->buffer.getReadPointer (0);
                const float* R = data->buffer.getReadPointer (1);

                const double s0 = jlimit (0.0, (double) len - 2, (double) jmin (o.get (pStart), o.get (pEnd)) * len);
                const double s1 = jlimit (s0 + 1, (double) len - 1, (double) jmax (o.get (pStart), o.get (pEnd)) * len);
                const bool oneShot = o.getInt (pMode) == 1;
                const bool loop = o.getInt (pLoop) > 0 && ! oneShot;
                const double loopStart = s0 + o.get (pLoopStart) * (s1 - s0);
                const double loopLen = jmax (1.0, s1 - loopStart);
                const double xfade = jmin ((double) o.get (pXfade) * loopLen, loopStart - s0);

                const double root = zoneRoot >= 0 ? (double) zoneRoot : (double) o.get (pRoot);
                const double semis = noteNumber - root + o.get (pTranspose) + o.get (pFine) / 100.0 + bend;
                const double inc = std::pow (2.0, semis / 12.0) * data->sampleRate / getSampleRate();

                if (oneShot) amp.setParameters (adsrFrom (o.get (pA), 1.0f, 1.0f, 30.0f));
                else         amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));

                const int ftype = o.getInt (pFilterType);
                const float cutoff = o.get (pCutoff), reso = o.get (pReso), fenvAmt = o.get (pFEnv);
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));
                if (ftype > 0) filter.set (cutoff, reso, getSampleRate());
                const float gain = Decibels::decibelsToGain (o.get (pGain)) * (1.0f - o.get (pVelSens) * (1.0f - velocity));
                const float pan = o.get (pPan);
                const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.4142f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;

                for (int i = 0; i < num; ++i)
                {
                    const float fe = fenv.getNextSample();
                    if (ftype > 0 && std::abs (fenvAmt) > 0.001f && (i & 15) == 0)
                        filter.set (jlimit (20.0f, 20000.0f, cutoff * std::pow (2.0f, fenvAmt * fe * 6.0f)), reso, getSampleRate());
                    if (loop && pos >= s1)
                        pos -= loopLen;

                    if (pos >= s1 || pos >= len - 2)
                    {
                        if (oneShot && ! releasing) { amp.noteOff(); releasing = true; }
                        if (! loop) { clearCurrentNote(); data.reset(); return; }
                    }

                    float l = interp (L, pos, len), r = interp (R, pos, len);
                    if (loop && xfade > 1.0 && pos > s1 - xfade)   // blend into the material before the loop start
                    {
                        const float w = (float) ((pos - (s1 - xfade)) / xfade);
                        const double other = pos - loopLen;
                        l = l * (1.0f - w) + interp (L, other, len) * w;
                        r = r * (1.0f - w) + interp (R, other, len) * w;
                    }

                    if (ftype > 0)
                    {
                        const int mode = ftype == 1 ? 0 : ftype == 2 ? 1 : 2;
                        l = filter.process (l, 0, mode);
                        r = filter.process (r, 1, mode);
                    }

                    const float e = amp.getNextSample() * gain;
                    out.addSample (0, start + i, l * e * gl);
                    if (out.getNumChannels() > 1) out.addSample (1, start + i, r * e * gr);
                    pos += inc;

                    if (! amp.isActive())
                    {
                        clearCurrentNote();
                        data.reset();
                        return;
                    }
                }

                const int slot = getCurrentlyPlayingNote() % (int) o.playheads.size();
                o.playheads[(size_t) slot].store ((float) (pos / len));
            }

            /** Granular mode: short windowed grains read from around the Scan position. */
            void renderGranular (AudioBuffer<float>& out, int start, int num)
            {
                const int len = data->buffer.getNumSamples();
                const float* L = data->buffer.getReadPointer (0);
                const float* R = data->buffer.getReadPointer (1);
                const double s0 = jlimit (0.0, (double) len - 2, (double) jmin (o.get (pStart), o.get (pEnd)) * len);
                const double s1 = jlimit (s0 + 1, (double) len - 1, (double) jmax (o.get (pStart), o.get (pEnd)) * len);
                const double semis = noteNumber - o.get (pRoot) + o.get (pTranspose) + o.get (pFine) / 100.0 + bend;
                const double rate = std::pow (2.0, semis / 12.0) * data->sampleRate / getSampleRate();
                const double grainLen = jmax (16.0, o.get (pGrainSize) * 0.001 * getSampleRate());
                const double interval = getSampleRate() / jmax (1.0f, o.get (pDensity));
                const float spray = o.get (pSpray), scan = o.get (pGrainPos);
                const float norm = 1.0f / std::sqrt (jmax (1.0f, (float) (grainLen / interval)));
                amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));
                const int ftype = o.getInt (pFilterType);
                const float cutoff = o.get (pCutoff), reso = o.get (pReso), fenvAmt = o.get (pFEnv);
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));
                if (ftype > 0) filter.set (cutoff, reso, getSampleRate());
                const float gain = Decibels::decibelsToGain (o.get (pGain)) * (1.0f - o.get (pVelSens) * (1.0f - velocity));
                const float pan = o.get (pPan);
                const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.4142f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;

                for (int i = 0; i < num; ++i)
                {
                    const float fe = fenv.getNextSample();
                    if (ftype > 0 && std::abs (fenvAmt) > 0.001f && (i & 15) == 0)
                        filter.set (jlimit (20.0f, 20000.0f, cutoff * std::pow (2.0f, fenvAmt * fe * 6.0f)), reso, getSampleRate());
                    if ((untilNextGrain -= 1.0) <= 0.0)
                    {
                        untilNextGrain += interval;
                        for (auto& g : grains)
                            if (! g.active)
                            {
                                const double centre = s0 + (scan + (rng.nextFloat() - 0.5f) * spray) * (s1 - s0);
                                g = { true, jlimit (s0, s1 - 2.0, centre), rate, 0.0, grainLen };
                                break;
                            }
                    }
                    float l = 0.0f, r = 0.0f;
                    for (auto& g : grains)
                    {
                        if (! g.active) continue;
                        const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) (g.age / g.length));
                        l += interp (L, g.pos, len) * w;
                        r += interp (R, g.pos, len) * w;
                        g.pos += g.inc;
                        if (g.pos >= s1) g.pos -= (s1 - s0);
                        if ((g.age += 1.0) >= g.length) g.active = false;
                    }
                    if (ftype > 0)
                    {
                        const int mode = ftype == 1 ? 0 : ftype == 2 ? 1 : 2;
                        l = filter.process (l, 0, mode);
                        r = filter.process (r, 1, mode);
                    }
                    const float e = amp.getNextSample() * gain * norm;
                    out.addSample (0, start + i, l * e * gl);
                    if (out.getNumChannels() > 1) out.addSample (1, start + i, r * e * gr);
                    if (! amp.isActive()) { clearCurrentNote(); data.reset(); for (auto& g : grains) g.active = false; return; }
                }
            }

            struct Grain { bool active = false; double pos = 0, inc = 1, age = 0, length = 1; };
            std::array<Grain, 48> grains;
            double untilNextGrain = 0.0;
            Random rng;

            static inline float interp (const float* d, double p, int len) noexcept   // 4-point Hermite
            {
                const int i = (int) p;
                const float f = (float) (p - i);
                auto at = [d, len] (int k) { return d[jlimit (0, len - 1, k)]; };
                const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            }

            Simpler& o;
            ADSR fenv;
            std::shared_ptr<SampleData> data;
            double pos = 0;
            int noteNumber = 60, zoneRoot = -1;
            float velocity = 1, bend = 0;
            bool releasing = false;
            ADSR amp;
            SVF filter;
        };

        mutable SpinLock sampleLock;
        std::shared_ptr<SampleData> sample, audioSample;
        std::shared_ptr<const std::vector<LoadedZone>> zones, audioZones;
    };

}

#include "DrumPads.h"
#include "UtilityDevices.h"
#include "Loudness.h"
#include "PulseFx2.h"
#include "FMSynth.h"
#include "PrismEQ.h"
#include "Dynamics.h"
#include "Dynamics2.h"
#include "Convolution.h"
#include "ModFx.h"
#include "ColourFx.h"
#include "AmpFx.h"
#include "TimeFx.h"

namespace pulse
{
    //==============================================================================
    /** Registers Pulse's devices with the engine and turns on swing-amount grooves. */
    inline void registerDevices (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();
        pm.createBuiltInType<WavetableSynth>();
        pm.createBuiltInType<Simpler>();
        pm.createBuiltInType<GlueCompressor>();
        pm.createBuiltInType<Saturator>();
        pm.createBuiltInType<PulseEQ>();
        pm.createBuiltInType<PrismEQ>();
        pm.createBuiltInType<ProCompressor>();
        pm.createBuiltInType<MultibandDynamics>();
        pm.createBuiltInType<AutoFilter>();
        pm.createBuiltInType<Limiter>();
        pm.createBuiltInType<Gate>();
        pm.createBuiltInType<TransientShaper>();
        pm.createBuiltInType<DeEsser>();
        pm.createBuiltInType<ConvolutionReverb>();
        pm.createBuiltInType<Chorus>();
        pm.createBuiltInType<PhaserFlanger>();
        pm.createBuiltInType<AutoPan>();
        pm.createBuiltInType<FreqShifter>();
        pm.createBuiltInType<Redux>();
        pm.createBuiltInType<Exciter>();
        pm.createBuiltInType<Amp>();
        pm.createBuiltInType<Resonators>();
        pm.createBuiltInType<Shimmer>();
        pm.createBuiltInType<GrainDelay>();
        pm.createBuiltInType<Arpeggiator>();
        pm.createBuiltInType<ChordFx>();
        pm.createBuiltInType<ScaleFx>();
        pm.createBuiltInType<NoteEcho>();
        pm.createBuiltInType<SandboxedPlugin>();
        pm.createBuiltInType<DrumPad>();
        pm.createBuiltInType<PadOut>();
        pm.createBuiltInType<DrumSynth>();
        pm.createBuiltInType<Utility>();
        pm.createBuiltInType<Tuner>();
        pm.createBuiltInType<Spectrum>();
        pm.createBuiltInType<LoudnessProbe>();
        pm.createBuiltInType<Reverb>();
        pm.createBuiltInType<Delay>();
        pm.createBuiltInType<Vocoder>();
        pm.createBuiltInType<PitchCorrect>();
        pm.createBuiltInType<FMSynth>();
        engine.getGrooveTemplateManager().useParameterizedGrooves (true);
    }
}

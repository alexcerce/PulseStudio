#pragma once

#include "Keys.h"

/** Pulse Analog (a two-oscillator subtractive synth with a four-pole ladder filter) and Pulse String (a plucked,
    struck or malleted string worked out from how a string behaves). */
namespace pulse
{
    class Analog : public PulseSynth
    {
    public:
        enum { pWave1, pOct1, pPw, pWave2, pOct2, pSemi2, pFine2, pSync, pMix, pSub, pNoise,
               pCutoff, pReso, pEnvAmt, pKeyTrack, pFilterDrive, pFA, pFD, pFS, pFR, pA, pD, pS, pR,
               pLfoRate, pLfoPitch, pLfoCutoff, pLfoPw, pMode, pGlide, pUnison, pDrift, pLevel };
        static inline const char* xmlTypeName = "pulseAnalog";
        static const char* getPluginName() { return "Pulse Analog"; }
        static inline const StringArray waveNames { "Saw", "Pulse", "Triangle", "Sine" };

        explicit Analog (te::PluginCreationInfo info) : PulseSynth (info)
        {
            auto octText = [] (float v) { const int o = roundToInt (v); return (o > 0 ? "+" : "") + String (o) + " oct"; };
            add ("wave1", "Osc 1 Wave", steps (waveNames.size()), 0.0f, fmt::choice (waveNames));
            add ("oct1", "Osc 1 Octave", { -2.0f, 2.0f, 1.0f }, 0.0f, octText);
            add ("pw", "Pulse Width", { 0.05f, 0.95f }, 0.5f, fmt::pct);
            add ("wave2", "Osc 2 Wave", steps (waveNames.size()), 0.0f, fmt::choice (waveNames));
            add ("oct2", "Osc 2 Octave", { -2.0f, 2.0f, 1.0f }, 0.0f, octText);
            add ("semi2", "Osc 2 Semitones", { -12.0f, 12.0f, 1.0f }, 0.0f, fmt::st);
            add ("fine2", "Osc 2 Fine", { -50.0f, 50.0f }, 8.0f, fmt::ct);
            add ("sync", "Sync Osc 2", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("mix", "Osc Mix", { 0.0f, 1.0f }, 0.5f, [] (float v) { return String (roundToInt ((1.0f - v) * 100)) + "/" + String (roundToInt (v * 100)); });
            add ("sub", "Sub", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("noise", "Noise", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("cutoff", "Cutoff", skewed (30.0f, 18000.0f, 1200.0f), 2500.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.25f, fmt::pct);
            add ("envamt", "Filter Env", { -1.0f, 1.0f }, 0.4f, fmt::pct);
            add ("keytrack", "Key Tracking", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("fdrive", "Filter Drive", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("fa", "Filter Attack", skewed (0.5f, 10000.0f, 200.0f), 2.0f, fmt::ms);
            add ("fd", "Filter Decay", skewed (2.0f, 10000.0f, 400.0f), 350.0f, fmt::ms);
            add ("fs", "Filter Sustain", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("fr", "Filter Release", skewed (2.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);
            add ("a", "Attack", skewed (0.5f, 10000.0f, 200.0f), 3.0f, fmt::ms);
            add ("d", "Decay", skewed (2.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);
            add ("s", "Sustain", { 0.0f, 1.0f }, 0.8f, fmt::pct);
            add ("r", "Release", skewed (2.0f, 10000.0f, 400.0f), 250.0f, fmt::ms);
            add ("lforate", "LFO Rate", skewed (0.05f, 30.0f, 4.0f), 5.0f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("lfopitch", "LFO > Pitch", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("lfocutoff", "LFO > Cutoff", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("lfopw", "LFO > Pulse Width", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("mode", "Voice Mode", steps (2), 0.0f, fmt::choice ({ "Poly", "Mono" }));
            add ("glide", "Glide", skewed (0.0f, 2000.0f, 150.0f), 0.0f, fmt::ms);
            add ("unison", "Unison", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("drift", "Drift", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("level", "Level", { -24.0f, 12.0f }, -8.0f, fmt::db);
            for (int i = 0; i < 16; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            synth.isMono = [this] { return getInt (pMode) > 0; };
        }

        String getName() const override       { return "Pulse Analog"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Osc 1", { pWave1, pOct1, pPw } }, { "Osc 2", { pWave2, pOct2, pSemi2, pFine2, pSync } }, { "Mix", { pMix, pSub, pNoise } },
                     { "Filter", { pCutoff, pReso, pEnvAmt, pKeyTrack, pFilterDrive } }, { "Filter env", { pFA, pFD, pFS, pFR } }, { "Amp env", { pA, pD, pS, pR } },
                     { "LFO", { pLfoRate, pLfoPitch, pLfoCutoff, pLfoPw } }, { "Voice", { pMode, pGlide, pUnison, pDrift, pLevel } } };
        }

        /** A wave as it would be drawn (not band-limited): phase 0..1. */
        static float shape (int wave, double ph, float pw) noexcept
        {
            ph -= std::floor (ph);
            switch (wave)
            {
                case 0:  return (float) (2.0 * ph - 1.0);
                case 1:  return ph < pw ? 1.0f : -1.0f;
                case 2:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
                default: return (float) std::sin (ph * MathConstants<double>::twoPi);
            }
        }

        /** How loud the filter lets a frequency through (as a gain), with the cutoff at `fc`: four poles and the resonance fed back round them. */
        float filterResponse (double f, double fc) const
        {
            const std::complex<double> pole (1.0, f / jmax (1.0, fc));
            const double k = get (pReso) * 4.0;
            return (float) std::abs ((1.0 + k * 0.5) / (pole * pole * pole * pole + k));
        }

        std::atomic<float> cutoffNow { 1000.0f };   // where the envelope and the LFO have the cutoff right now, for the display
        std::atomic<float> lastNoteHz { 0.0f };

    private:
        /** Takes the step out of a saw or pulse edge, so that it does not fold back as hash (t = phase 0..1, dt = phase per sample). */
        static inline float blep (double t, double dt) noexcept
        {
            if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
            return 0.0f;
        }

        struct Osc
        {
            double phase = 0.0;
            /** One sample. `wrapped` is set when the phase passed the start of its cycle. */
            inline float next (int wave, double inc, float pw, bool& wrapped) noexcept
            {
                phase += inc;
                wrapped = phase >= 1.0;
                if (wrapped) phase -= 1.0;
                switch (wave)
                {
                    case 0:  return (float) (2.0 * phase - 1.0) - blep (phase, inc);
                    case 1:
                    {
                        double p2 = phase + 1.0 - pw;
                        if (p2 >= 1.0) p2 -= 1.0;
                        return (phase < pw ? 1.0f : -1.0f) + blep (phase, inc) - blep (p2, inc);
                    }
                    case 2:  return (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
                    default: return (float) std::sin (phase * MathConstants<double>::twoPi);
                }
            }
        };

        /** A four-pole ladder low-pass with the resonance fed back with no delay, and a soft limit on what goes in. */
        struct Ladder
        {
            float s[4] {};
            inline float process (float in, float g, float k, float drive) noexcept
            {
                const float G = g / (1.0f + g), G2 = G * G, d = 1.0f / (1.0f + g);
                const float sum = G2 * G * s[0] * d + G2 * s[1] * d + G * s[2] * d + s[3] * d;
                float u = (in * (1.0f + k * 0.5f) - k * sum) / (1.0f + k * G2 * G2);
                u = std::tanh (u * drive) / drive;   // the transistors only swing so far: this is what keeps a howling resonance in hand
                for (auto& st : s)
                {
                    const float v = (u - st) * G;
                    u = v + st;
                    st = u + v;
                }
                return u;
            }
            void reset() { for (auto& st : s) st = 0.0f; }
        };

        struct Voice : public SynthesiserVoice
        {
            explicit Voice (Analog& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                const float target = (float) MidiMessage::getMidiNoteInHertz (note);
                const float from = isVoiceActive() && freq > 0.0f ? freq : o.lastNoteHz.load();
                freq = o.get (pGlide) > 0.5f && from > 0.0f ? from : target;
                targetFreq = target;
                o.lastNoteHz.store (target);
                velocity = vel;
                noteNumber = note;
                pitchWheelMoved (pitchWheel);
                const double sr = getSampleRate();
                amp.setSampleRate (sr); fenv.setSampleRate (sr);
                amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));
                amp.noteOn(); fenv.noteOn();
                if (! wasActive)   // a new sound starts clean; a voice taken over mid-note carries on, so it does not click
                {
                    for (auto& f : filter) f.reset();
                    for (auto& oc : osc) for (auto& x : oc) x.phase = rng.nextDouble();
                    subPhase = 0.0; lfoPhase = 0.0;
                }
                wasActive = true;
            }
            void stopNote (float, bool allowTailOff) override
            {
                if (allowTailOff) { amp.noteOff(); fenv.noteOff(); }
                else { amp.reset(); fenv.reset(); wasActive = false; clearCurrentNote(); }
            }
            void pitchWheelMoved (int v) override { bend = (float) (v - 8192) / 8192.0f * 2.0f; }
            void controllerMoved (int cc, int v) override { if (cc == 1) modWheel = (float) v / 127.0f; }

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;
                const double sr = getSampleRate();
                amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));
                const int wave[2] = { o.getInt (pWave1), o.getInt (pWave2) };
                const bool sync = o.getInt (pSync) > 0;
                const float mix = o.get (pMix), sub = o.get (pSub), noise = o.get (pNoise), unison = o.get (pUnison), drift = o.get (pDrift);
                const float cutoff = o.get (pCutoff), reso = o.get (pReso), envAmt = o.get (pEnvAmt), keyTrack = o.get (pKeyTrack);
                const float drive = 0.4f + o.get (pFilterDrive) * 3.0f;
                const float lfoPitch = o.get (pLfoPitch) + modWheel * 0.3f, lfoCut = o.get (pLfoCutoff), lfoPw = o.get (pLfoPw);
                const float level = Decibels::decibelsToGain (o.get (pLevel)) * (0.35f + 0.65f * velocity);
                const float glideMs = o.get (pGlide);
                const double lfoInc = o.get (pLfoRate) / sr;
                const double ratio2 = std::pow (2.0, o.getInt (pOct2) + (o.getInt (pSemi2) + o.get (pFine2) / 100.0) / 12.0), ratio1 = std::pow (2.0, o.getInt (pOct1));
                const int copies = unison > 0.01f ? 2 : 1;
                auto* l = out.getWritePointer (0, start);
                auto* r = out.getWritePointer (1, start);
                constexpr int chunk = 16;

                for (int c = 0; c < num; c += chunk)
                {
                    const int n = jmin (chunk, num - c);
                    if (glideMs > 0.5f && std::abs (freq - targetFreq) > 0.01f) freq += (targetFreq - freq) * (1.0f - std::exp (-(float) n / (float) (glideMs * 0.001 * sr)));
                    else freq = targetFreq;
                    lfoPhase += lfoInc * n; lfoPhase -= std::floor (lfoPhase);
                    const float lfo = (float) std::sin (lfoPhase * MathConstants<double>::twoPi);
                    // a little wander in the tuning, different for every voice, as in circuits that are never quite still
                    driftNow += ((rng.nextFloat() * 2.0f - 1.0f) - driftNow) * 0.002f * (float) n;
                    const double f = freq * std::pow (2.0, (bend + lfo * lfoPitch * lfoPitch * 12.0 + driftNow * drift * 0.25) / 12.0);
                    const float pw = jlimit (0.05f, 0.95f, o.get (pPw) + lfo * lfoPw * 0.4f);
                    float envNow = 0.0f;
                    for (int i = 0; i < n; ++i) envNow = fenv.getNextSample();
                    const float octaves = envAmt * envNow * 7.0f + lfo * lfoCut * 3.0f + (noteNumber - 60) / 12.0f * keyTrack;
                    const float fc = jlimit (20.0f, (float) sr * 0.45f, cutoff * std::pow (2.0f, octaves));
                    o.cutoffNow.store (fc);
                    const float g = (float) std::tan (MathConstants<double>::pi * fc / sr), k = reso * 4.0f;
                    double inc1[2], inc2[2];
                    for (int u = 0; u < 2; ++u)
                    {
                        const double det = copies == 2 ? std::pow (2.0, (u == 0 ? -1.0 : 1.0) * unison * 25.0 / 1200.0) : 1.0;
                        inc1[u] = jmin (0.45, f * ratio1 * det / sr);
                        inc2[u] = jmin (0.45, f * ratio2 * det / sr);
                    }
                    const double subInc = f * ratio1 * 0.5 / sr;

                    for (int i = 0; i < n; ++i)
                    {
                        float side[2] = { 0.0f, 0.0f };
                        for (int u = 0; u < copies; ++u)
                        {
                            bool w1 = false, w2 = false;
                            const float a = osc[u][0].next (wave[0], inc1[u], pw, w1);
                            if (sync && w1) osc[u][1].phase = osc[u][0].phase * inc2[u] / inc1[u];   // osc 2 starts its cycle again with osc 1
                            const float b = osc[u][1].next (wave[1], inc2[u], pw, w2);
                            side[u] = a * (1.0f - mix) + b * mix;
                        }
                        subPhase += subInc; if (subPhase >= 1.0) subPhase -= 1.0;
                        const float common = (subPhase < 0.5 ? 1.0f : -1.0f) * sub * 0.7f + (rng.nextFloat() * 2.0f - 1.0f) * noise * 0.6f;
                        // with unison the two copies sit to the left and to the right
                        const float inL = (copies == 2 ? side[0] * 0.8f + side[1] * 0.35f : side[0]) + common;
                        const float inR = (copies == 2 ? side[1] * 0.8f + side[0] * 0.35f : side[0]) + common;
                        const float e = amp.getNextSample() * level * 0.35f;
                        l[c + i] += filter[0].process (inL * 0.6f, g, k, drive) * e;
                        r[c + i] += filter[1].process (inR * 0.6f, g, k, drive) * e;
                    }
                }
                if (! amp.isActive()) { wasActive = false; clearCurrentNote(); }
            }

            Analog& o;
            Osc osc[2][2];
            Ladder filter[2];
            ADSR amp, fenv;
            double subPhase = 0.0, lfoPhase = 0.0;
            float freq = 0.0f, targetFreq = 440.0f, velocity = 1.0f, bend = 0.0f, modWheel = 0.0f, driftNow = 0.0f;
            int noteNumber = 60;
            bool wasActive = false;
            Random rng;
        };
    };

    //==============================================================================
    /** Pulse String: a string set going by a pluck, a pick, a hammer or a mallet, and left to ring. The sound goes
        round a loop as long as one cycle of the note, losing a little each time (more of its highs than its lows). */
    class StringModel : public PulseSynth
    {
    public:
        enum { pExciter, pPosition, pBrightness, pHardness, pDecay, pDamping, pStiffness, pDetune, pBody, pRelease, pLevel };
        static inline const char* xmlTypeName = "pulseString";
        static const char* getPluginName() { return "Pulse String"; }
        static inline const StringArray exciterNames { "Pluck", "Pick", "Hammer", "Mallet" };

        explicit StringModel (te::PluginCreationInfo info) : PulseSynth (info)
        {
            add ("exciter", "Exciter", steps (exciterNames.size()), 0.0f, fmt::choice (exciterNames));
            add ("position", "Position", { 0.03f, 0.5f }, 0.2f, [] (float v) { return v > 0.45f ? String ("Middle") : String (roundToInt (v * 100)) + "% along"; });
            add ("brightness", "Brightness", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("hardness", "Velocity > Brightness", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("decay", "Decay", skewed (0.2f, 30.0f, 4.0f), 4.0f, [] (float v) { return String (v, 1) + " s"; });
            add ("damping", "Damping", { 0.0f, 1.0f }, 0.35f, fmt::pct);
            add ("stiffness", "Stiffness", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("detune", "Second String", { 0.0f, 30.0f }, 4.0f, [] (float v) { return v < 0.1f ? String ("Off") : String (v, 1) + " ct"; });
            add ("body", "Body", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("release", "Release", skewed (20.0f, 4000.0f, 300.0f), 250.0f, fmt::ms);
            add ("level", "Level", { -24.0f, 12.0f }, -6.0f, fmt::db);
            for (int i = 0; i < 16; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        String getName() const override       { return "Pulse String"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Exciter", { pExciter, pPosition, pBrightness, pHardness } }, { "String", { pDecay, pDamping, pStiffness, pDetune } }, { "Output", { pBody, pRelease, pLevel } } };
        }

        /** How loud harmonic n starts (0..1): the place the string is set going decides which harmonics are missing, the brightness
            how fast they fall away. */
        float harmonicStart (int n) const
        {
            const float pos = get (pPosition), bright = get (pBrightness);
            const int ex = getInt (pExciter);
            const float base = ex >= 2 ? 1.0f / (float) n : 1.0f / std::sqrt ((float) n);   // a hammer or mallet is softer than a string let go
            return std::abs (std::sin (MathConstants<float>::pi * (float) n * pos)) * base * std::pow ((float) n, -2.0f * (1.0f - bright));
        }
        /** How long (seconds, to -60 dB) harmonic n of a note at `hz` rings: Decay for the note itself, less for the ones above as Damping says. */
        float harmonicDecay (int n, float hz) const
        {
            return get (pDecay) * jlimit (0.3f, 2.5f, std::pow (220.0f / hz, 0.4f)) / (1.0f + get (pDamping) * 0.6f * (float) (n - 1) * (float) (n - 1) * hz / 2000.0f);
        }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            PulseSynth::initialise (info);
            static constexpr float modes[3] = { 105.0f, 210.0f, 390.0f };
            for (int ch = 0; ch < 2; ++ch)
                for (int m = 0; m < 3; ++m) { body[(size_t) ch][(size_t) m].reset(); body[(size_t) ch][(size_t) m].set (Biquad::Type::bandPass, sampleRate, modes[m] * (ch == 0 ? 1.0f : 1.03f), 6.0, 0.0); }
            for (int v = 0; v < synth.getNumVoices(); ++v)
                if (auto* voice = dynamic_cast<Voice*> (synth.getVoice (v))) voice->prepare (sampleRate);   // (nothing is allocated while playing)
        }

    protected:
        void afterBlock (AudioBuffer<float>& buffer, int start, int num) override
        {
            const float amount = get (pBody), level = Decibels::decibelsToGain (get (pLevel));
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buffer.getWritePointer (ch, start);
                for (int i = 0; i < num; ++i)
                {
                    // the wooden box the string sits on rings at a few pitches of its own
                    float box = 0.0f;
                    for (auto& m : body[(size_t) ch]) box += m.process (d[i], 0);
                    d[i] = (d[i] + box * amount * 1.6f) * level;
                }
            }
        }

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (StringModel& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void prepare (double sr)
            {
                const int size = (int) (sr / 20.0) + 16;   // room for the lowest note
                for (auto& s : str)
                {
                    s.line.setMax (size);
                    s.burst.assign ((size_t) size, 0.0f);
                    s.scratch.assign ((size_t) size, 0.0f);
                    s.burstLen = s.burstPos = 0;
                }
                active = false;
            }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                const double sr = getSampleRate();
                if (str[0].burst.empty()) prepare (sr);
                hz = (float) MidiMessage::getMidiNoteInHertz (note);
                pitchWheelMoved (pitchWheel);
                released = false;
                const int ex = o.getInt (pExciter);
                const float pos = o.get (pPosition);
                const float bright = jlimit (0.0f, 1.0f, o.get (pBrightness) * (1.0f - o.get (pHardness) * (1.0f - vel)));
                for (int st = 0; st < 2; ++st)
                {
                    auto& s = str[st];
                    s.line.clear();
                    s.lp = 0.0f; s.ap1x = s.ap1y = s.ap2x = s.ap2y = 0.0f;
                    // what sets the string going: one cycle long, shaped by the exciter
                    const int len = jlimit (4, (int) s.burst.size() - 8, (int) (sr / hz));
                    std::vector<float>& e = s.burst;
                    std::vector<float>& shaped = s.scratch;
                    float lp = 0.0f;
                    const float colour = 0.03f + 0.97f * bright * bright;
                    for (int i = 0; i < len; ++i)
                    {
                        float x;
                        if (ex <= 1) x = rng.nextFloat() * 2.0f - 1.0f;                                                           // let go, or picked: every frequency at once
                        else { const float w = (ex == 2 ? 0.25f : 0.08f) * (1.3f - bright); const float t = (float) i / (float) len / w; x = t < 1.0f ? std::sin (MathConstants<float>::pi * t) : 0.0f; }   // struck: one soft bump
                        lp += colour * (x - lp);
                        e[(size_t) i] = ex == 1 ? x * 0.6f + lp * 0.4f : lp;
                    }
                    // where along the string: the harmonics that have a still point there are not set going
                    const int off = jmax (1, (int) (pos * (float) len));
                    float peak = 1.0e-6f;
                    for (int i = 0; i < len; ++i) { shaped[(size_t) i] = e[(size_t) i] - e[(size_t) ((i + len - off) % len)]; peak = jmax (peak, std::abs (shaped[(size_t) i])); }
                    for (int i = 0; i < len; ++i) e[(size_t) i] = shaped[(size_t) i] / peak * (0.2f + 0.8f * vel);
                    s.burstLen = len;
                    s.burstPos = 0;
                }
                active = true;
                quiet = 0;
            }
            void stopNote (float, bool allowTailOff) override
            {
                released = true;
                if (! allowTailOff) { active = false; clearCurrentNote(); }
            }
            void pitchWheelMoved (int v) override { bend = (float) (v - 8192) / 8192.0f * 2.0f; }
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive() || ! active) return;
                const double sr = getSampleRate();
                const float detune = o.get (pDetune), damping = o.get (pDamping), stiff = o.get (pStiffness);
                const float decay = (released ? jmin (o.get (pDecay), o.get (pRelease) * 0.001f) : o.get (pDecay)) * (released ? 1.0f : jlimit (0.3f, 2.5f, std::pow (220.0f / hz, 0.4f)));
                // the highs are lost a little each time round: a gentle low-pass in the loop (more of it with Damping)
                const float lpA = jlimit (0.05f, 1.0f, 1.0f - damping * 0.93f);
                const float apC = stiff * 0.7f;   // stiffness: the highs travel faster than the lows, so the upper harmonics go sharp
                auto* l = out.getWritePointer (0, start);
                auto* r = out.getWritePointer (1, start);
                float loudest = 0.0f;
                for (int st = 0; st < 2; ++st)
                {
                    if (st == 1 && detune < 0.1f) break;
                    auto& s = str[st];
                    const double f = hz * std::pow (2.0, (bend + (st == 0 ? 0.0 : detune / 100.0)) / 12.0);
                    // the loop must be one cycle long in all: the filters in it take up some of that
                    const float lpDelay = (1.0f - lpA) / lpA;
                    const float apDelay = apC > 0.001f ? 2.0f * (1.0f - apC) / (1.0f + apC) : 0.0f;
                    const float len = jlimit (2.0f, (float) s.line.buf.size() - 4.0f, (float) (sr / f) - lpDelay - apDelay);
                    const float g = std::pow (10.0f, -3.0f / (float) (f * decay));   // per trip round, for -60 dB after Decay
                    const float gainL = st == 0 ? (detune < 0.1f ? 1.0f : 0.85f) : 0.35f, gainR = st == 0 ? (detune < 0.1f ? 1.0f : 0.35f) : 0.85f;
                    for (int i = 0; i < num; ++i)
                    {
                        float y = s.line.read (len);
                        s.lp += lpA * (y - s.lp);
                        y = s.lp;
                        if (apC > 0.001f)
                        {
                            const float a1 = apC * y + s.ap1x - apC * s.ap1y; s.ap1x = y; s.ap1y = a1;
                            const float a2 = apC * a1 + s.ap2x - apC * s.ap2y; s.ap2x = a1; s.ap2y = a2;
                            y = a2;
                        }
                        float in = y * g;
                        if (s.burstPos < s.burstLen) in += s.burst[(size_t) s.burstPos++];
                        s.line.push (in);
                        l[i] += in * gainL * 0.5f;
                        r[i] += in * gainR * 0.5f;
                        loudest = jmax (loudest, std::abs (in));
                    }
                }
                quiet = loudest < 1.0e-4f ? quiet + num : 0;
                if (quiet > (int) (sr * 0.05)) { active = false; clearCurrentNote(); }
            }

            struct String1
            {
                DelayLine line;
                std::vector<float> burst, scratch;
                int burstPos = 0, burstLen = 0;
                float lp = 0.0f, ap1x = 0.0f, ap1y = 0.0f, ap2x = 0.0f, ap2y = 0.0f;
            };
            StringModel& o;
            String1 str[2];
            float hz = 220.0f, bend = 0.0f;
            bool released = false, active = false;
            int quiet = 0;
            Random rng { 73 };
        };

        std::array<std::array<Biquad, 3>, 2> body;
    };
}

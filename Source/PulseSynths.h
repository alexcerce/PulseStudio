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

            MidiBuffer midi;
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

            beforeBlock();
            synth.renderNextBlock (buffer, midi, fc.bufferStartSample, fc.bufferNumSamples);
            te::sanitiseValues (buffer, fc.bufferStartSample, fc.bufferNumSamples, 4.0f);
        }

    protected:
        virtual void beforeBlock() {}
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
        static inline const StringArray setNames { "Basic shapes", "Pulse width", "Harmonics", "Vowels", "Sync saw", "User (drawn)" };
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
                default:  // hard-synced saw (ratio 1 → 8), made in the time domain
                {
                    const float ratio = 1.0f + t * 7.0f;
                    std::vector<float> buf ((size_t) tableSize * 2, 0.0f);
                    for (int i = 0; i < tableSize; ++i)
                    {
                        const float ph = (float) i / tableSize * ratio;
                        buf[(size_t) i] = 2.0f * (ph - std::floor (ph)) - 1.0f;
                    }
                    fft.performRealOnlyForwardTransform (buf.data());
                    for (int h = 1; h < 1024; ++h)
                        s[(size_t) h] = std::complex<float> (buf[(size_t) h * 2], -buf[(size_t) h * 2 + 1]);
                    break;
                }
            }
            return s;
        }
    };

    //==============================================================================
    class WavetableSynth : public PulseSynth
    {
    public:
        enum { pSet1, pPos1, pSet2, pPos2, pSemi2, pFine2, pMix, pVoices, pDetune, pSub,
               pFilterType, pCutoff, pReso, pFilterEnv, pFA, pFD, pFS, pFR, pA, pD, pS, pR,
               pLfoRate, pLfoPos, pLfoCut, pGlide, pLevel, pBendRange, pPressure, pSlide };
        static inline const char* xmlTypeName = "pulseWavetable";
        static const char* getPluginName() { return "Wavetable"; }
        static inline const StringArray filterNames { "Low pass 12", "Low pass 24", "Band pass", "High pass" };

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
            add ("lforate", "LFO Rate", skewed (0.05f, 20.0f, 2.0f), 0.5f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("lfopos", "LFO > Position", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("lfocut", "LFO > Cutoff", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("glide", "Glide", skewed (0.0f, 1000.0f, 100.0f), 0.0f, fmt::ms);
            add ("level", "Level", { -36.0f, 6.0f }, -6.0f, fmt::db);
            // MPE / expression: per-note bend (each note on its own channel), pressure and slide (CC74)
            add ("bendrange", "Bend Range", { 1.0f, 48.0f, 1.0f }, 2.0f, fmt::st);
            add ("pressure", "Pressure", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("slide", "Slide", { 0.0f, 1.0f }, 0.5f, fmt::pct);

            WavetableBank::get();   // build the tables now rather than on the audio thread
            for (int i = 0; i < 16; ++i)
                synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            userWatcher.owner = this;
            state.addListener (&userWatcher);
            rebuildUserTable();
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
            return WavetableBank::get().table (jmin (set, WavetableBank::userSet - 1), frame, level);
        }

        String getName() const override      { return "Wavetable"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 190; }

        std::vector<Section> getSections() const override
        {
            return { { "Osc 1", { pSet1, pPos1 } }, { "Osc 2", { pSet2, pPos2, pSemi2, pFine2 } },
                     { "Unison / Sub", { pMix, pVoices, pDetune, pSub } },
                     { "Filter", { pFilterType, pCutoff, pReso, pFilterEnv } },
                     { "Filter env", { pFA, pFD, pFS, pFR } }, { "Amp env", { pA, pD, pS, pR } },
                     { "LFO", { pLfoRate, pLfoPos, pLfoCut, pGlide } }, { "Expression (MPE)", { pBendRange, pPressure, pSlide } },
                     { "Out", { pLevel } } };
        }

    private:
        struct UserWatcher : public ValueTree::Listener
        {
            WavetableSynth* owner = nullptr;
            void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
            {
                if (v == owner->state && (id == userAID || id == userBID)) owner->rebuildUserTable();
            }
        } userWatcher;

        void rebuildUserTable()
        {
            auto t = WavetableBank::buildUser (getUserWave (0), getUserWave (1));
            const SpinLock::ScopedLockType sl (userLock);
            userTable = t;
        }

        void beforeBlock() override
        {
            const SpinLock::ScopedTryLockType sl (userLock);
            if (sl.isLocked()) audioUserTable = userTable;
        }

        mutable SpinLock userLock;
        std::shared_ptr<const WavetableBank::UserTable> userTable, audioUserTable;

        struct Voice : public SynthesiserVoice
        {
            explicit Voice (WavetableSynth& s) : o (s) {}

            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                const float target = (float) MidiMessage::getMidiNoteInHertz (note);
                const bool glide = o.get (pGlide) > 0.5f && lastFreq > 0.0f && isVoiceActive();
                freq = glide ? lastFreq : target;
                targetFreq = target;
                velocity = vel;
                pitchWheelMoved (pitchWheel);
                Random r;
                for (auto& oscPhases : phases)
                    for (auto& p : oscPhases) p = r.nextDouble() * WavetableBank::tableSize;
                subPhase = 0.0;
                lfoPhase = 0.0;
                pressure = 0.0f;
                slide = 0.5f;
                filter[0].reset(); filter[1].reset();
                amp.setSampleRate (getSampleRate());
                fenv.setSampleRate (getSampleRate());
                amp.reset(); fenv.reset();
                amp.noteOn(); fenv.noteOn();
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (allowTailOff) { amp.noteOff(); fenv.noteOff(); }
                else              { clearCurrentNote(); amp.reset(); }
            }

            void pitchWheelMoved (int v) override         { bend = (float) (v - 8192) / 8192.0f * o.get (pBendRange); }
            void controllerMoved (int cc, int v) override  { if (cc == 74) slide = (float) v / 127.0f; }
            void channelPressureChanged (int v) override   { pressure = (float) v / 127.0f; }
            void aftertouchChanged (int v) override        { pressure = (float) v / 127.0f; }

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive())
                    return;

                const double sr = getSampleRate();
                const float pressureAmt = o.get (pPressure), slideAmt = o.get (pSlide);
                auto& bank = WavetableBank::get();
                amp.setParameters (adsrFrom (o.get (pA), o.get (pD), o.get (pS), o.get (pR)));
                fenv.setParameters (adsrFrom (o.get (pFA), o.get (pFD), o.get (pFS), o.get (pFR)));

                const int set[2] = { o.getInt (pSet1), o.getInt (pSet2) };
                const float slideOffset = (slide - 0.5f) * 2.0f * slideAmt;
                const float basePos[2] = { jlimit (0.0f, 1.0f, o.get (pPos1) + slideOffset), jlimit (0.0f, 1.0f, o.get (pPos2) + slideOffset) };
                const float oscGain[2] = { 1.0f - o.get (pMix), o.get (pMix) };
                const double osc2Ratio = std::pow (2.0, (o.get (pSemi2) + o.get (pFine2) / 100.0) / 12.0);
                const int voices = jlimit (1, 7, o.getInt (pVoices));
                const float detune = o.get (pDetune), sub = o.get (pSub);
                const int ftype = o.getInt (pFilterType);
                const float cutoff = o.get (pCutoff), reso = o.get (pReso), fenvAmt = o.get (pFilterEnv);
                const float lfoRate = o.get (pLfoRate), lfoPos = o.get (pLfoPos), lfoCut = o.get (pLfoCut);
                const float level = Decibels::decibelsToGain (o.get (pLevel)) * (0.3f + 0.7f * velocity) * (1.0f + pressure * pressureAmt * 0.6f);
                const float glideMs = o.get (pGlide);
                const float uniGain = 1.0f / std::sqrt ((float) voices);
                const int mode = ftype == 2 ? 1 : ftype == 3 ? 2 : 0;

                // unison offsets (cents) and pans
                float cents[7], panL[7], panR[7];
                for (int u = 0; u < voices; ++u)
                {
                    const float spread = voices > 1 ? (float) u / (voices - 1) * 2.0f - 1.0f : 0.0f;
                    cents[u] = spread * detune * 50.0f;
                    const float p = spread * 0.8f;
                    panL[u] = std::sqrt (0.5f * (1.0f - p));
                    panR[u] = std::sqrt (0.5f * (1.0f + p));
                }

                constexpr int chunk = 32;
                for (int c = 0; c < num; c += chunk)
                {
                    const int n = jmin (chunk, num - c);

                    if (glideMs > 0.5f && std::abs (freq - targetFreq) > 0.01f)
                    {
                        const float k = 1.0f - std::exp (-(float) n / (float) (glideMs * 0.001 * sr));
                        freq += (targetFreq - freq) * k;
                    }
                    else
                    {
                        freq = targetFreq;
                    }
                    lastFreq = freq;

                    const double f = freq * std::pow (2.0, bend / 12.0);
                    const float lfo = (float) std::sin (lfoPhase * MathConstants<double>::twoPi);
                    lfoPhase += lfoRate * n / sr;
                    lfoPhase -= std::floor (lfoPhase);

                    // per-chunk table selection
                    const float* t0[2]; const float* t1[2]; float frameFrac[2]; double inc[2][7];
                    for (int osc = 0; osc < 2; ++osc)
                    {
                        const double of = osc == 0 ? f : f * osc2Ratio;
                        const float pos = jlimit (0.0f, 1.0f, basePos[osc] + lfo * lfoPos * 0.5f) * (WavetableBank::numFrames - 1);
                        const int fr = jmin (WavetableBank::numFrames - 2, (int) pos);
                        frameFrac[osc] = pos - (float) fr;
                        const int level = WavetableBank::levelFor (of * std::pow (2.0, detune * 50.0 / 1200.0), sr);
                        t0[osc] = o.tableFor (set[osc], fr, level, o.audioUserTable.get());
                        t1[osc] = o.tableFor (set[osc], fr + 1, level, o.audioUserTable.get());
                        for (int u = 0; u < voices; ++u)
                            inc[osc][u] = of * std::pow (2.0, cents[u] / 1200.0) / sr * WavetableBank::tableSize;
                    }

                    const float envNow = fenv.getNextSample();
                    for (int i = 1; i < n; ++i) fenv.getNextSample();
                    const float octaves = fenvAmt * envNow * 6.0f + lfo * lfoCut * 3.0f + pressure * pressureAmt * 3.0f;
                    const float fc = jlimit (30.0f, 20000.0f, cutoff * std::pow (2.0f, octaves));
                    filter[0].set (fc, reso, sr);
                    filter[1].set (fc, reso * 0.5f, sr);
                    const double subInc = f * 0.5 / sr;

                    for (int i = 0; i < n; ++i)
                    {
                        float l = 0.0f, r = 0.0f;
                        for (int osc = 0; osc < 2; ++osc)
                        {
                            if (oscGain[osc] < 0.001f) { for (int u = 0; u < voices; ++u) advance (phases[osc][u], inc[osc][u]); continue; }
                            for (int u = 0; u < voices; ++u)
                            {
                                auto& ph = phases[osc][u];
                                const float a = WavetableBank::read (t0[osc], ph), b = WavetableBank::read (t1[osc], ph);
                                const float sOut = (a + frameFrac[osc] * (b - a)) * oscGain[osc] * uniGain;
                                l += sOut * panL[u];
                                r += sOut * panR[u];
                                advance (ph, inc[osc][u]);
                            }
                        }
                        if (sub > 0.001f)
                        {
                            const float sv = (float) std::sin (subPhase * MathConstants<double>::twoPi) * sub * 0.7f;
                            l += sv; r += sv;
                            subPhase += subInc;
                            subPhase -= std::floor (subPhase);
                        }

                        l = filter[0].process (l, 0, mode);
                        r = filter[0].process (r, 1, mode);
                        if (ftype == 1) { l = filter[1].process (l, 0, 0); r = filter[1].process (r, 1, 0); }

                        const float e = amp.getNextSample() * level;
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
            double subPhase = 0, lfoPhase = 0;
            float freq = 440, targetFreq = 440, lastFreq = 0, velocity = 1, bend = 0, pressure = 0, slide = 0.5f;
            ADSR amp, fenv;
            SVF filter[2];
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

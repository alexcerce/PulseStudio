#pragma once

// Included from PulseSynths.h (needs PulseSynth, AnySound, SVF, adsrFrom).

/** Four-operator FM synth (Operator-style): eight algorithms, per-operator ratio / fine / level /
    envelope, feedback on operator 4, a filter and glide. */
namespace pulse
{
    class FMSynth : public PulseSynth
    {
    public:
        static constexpr int numOps = 4, perOp = 7;   // ratio, fine, level, a, d, s, r
        enum { opBase = 0, pAlgorithm = numOps * perOp, pFeedback, pCutoff, pReso, pVelSens, pGlide, pVolume };
        static int opParam (int op, int k) { return op * perOp + k; }
        enum { kRatio, kFine, kLevel, kA, kD, kS, kR };
        static inline const char* xmlTypeName = "pulseFM";
        static const char* getPluginName() { return "FM Synth"; }

        /** Modulation edges (from -> to, 0-based) and which operators are heard. */
        struct Algorithm { std::vector<std::pair<int, int>> mods; std::vector<int> outs; const char* name; };
        static const std::vector<Algorithm>& algorithms()
        {
            static const std::vector<Algorithm> a {
                { { { 3, 2 }, { 2, 1 }, { 1, 0 } },          { 0 },          "4>3>2>1" },
                { { { 3, 1 }, { 2, 1 }, { 1, 0 } },          { 0 },          "(4+3)>2>1" },
                { { { 3, 0 }, { 2, 1 }, { 1, 0 } },          { 0 },          "4>1, 3>2>1" },
                { { { 3, 2 }, { 2, 0 }, { 1, 0 } },          { 0 },          "4>3>1, 2>1" },
                { { { 3, 2 }, { 1, 0 } },                    { 0, 2 },       "2 stacks" },
                { { { 3, 0 }, { 3, 1 }, { 3, 2 } },          { 0, 1, 2 },    "4 > 1,2,3" },
                { { { 3, 2 } },                              { 0, 1, 2 },    "4>3 + 2 + 1" },
                { {},                                         { 0, 1, 2, 3 }, "Additive" } };
            return a;
        }

        explicit FMSynth (te::PluginCreationInfo info) : PulseSynth (info)
        {
            const float ratios[] = { 1.0f, 2.0f, 1.0f, 3.0f };
            const float levels[] = { 1.0f, 0.5f, 0.3f, 0.2f };
            for (int op = 0; op < numOps; ++op)
            {
                const String o = "Op " + String (op + 1) + " ";
                add ("op" + String (op) + "ratio", o + "Ratio", { 0.5f, 16.0f, 0.5f }, ratios[op], [] (float v) { return "x" + String (v, v < 1.0f ? 1 : (std::fmod (v, 1.0f) > 0.01f ? 1 : 0)); });
                add ("op" + String (op) + "fine", o + "Fine", { -50.0f, 50.0f }, 0.0f, fmt::ct);
                add ("op" + String (op) + "level", o + "Level", { 0.0f, 1.0f }, levels[op], fmt::pct);
                add ("op" + String (op) + "a", o + "Attack", skewed (0.5f, 5000.0f, 100.0f), 2.0f, fmt::ms);
                add ("op" + String (op) + "d", o + "Decay", skewed (1.0f, 10000.0f, 500.0f), op == 0 ? 800.0f : 400.0f, fmt::ms);
                add ("op" + String (op) + "s", o + "Sustain", { 0.0f, 1.0f }, op == 0 ? 0.7f : 0.3f, fmt::pct);
                add ("op" + String (op) + "r", o + "Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, fmt::ms);
            }
            StringArray names;
            for (auto& a : algorithms()) names.add (a.name);
            add ("algorithm", "Algorithm", steps (names.size()), 0.0f, fmt::choice (names));
            add ("feedback", "Feedback", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("cutoff", "Cutoff", skewed (30.0f, 20000.0f, 1500.0f), 20000.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("velsens", "Velocity", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("glide", "Glide", skewed (0.0f, 1000.0f, 100.0f), 0.0f, fmt::ms);
            add ("volume", "Volume", { -36.0f, 6.0f }, -8.0f, fmt::db);

            for (int i = 0; i < 12; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        String getName() const override      { return "FM Synth"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 120; }

        std::vector<Section> getSections() const override
        {
            std::vector<Section> s;
            s.push_back ({ "Global", { pAlgorithm, pFeedback, pCutoff, pReso, pVelSens, pGlide, pVolume } });
            for (int op = 0; op < numOps; ++op)
                s.push_back ({ "Op " + String (op + 1), { opParam (op, kRatio), opParam (op, kFine), opParam (op, kLevel), opParam (op, kA), opParam (op, kD), opParam (op, kS), opParam (op, kR) } });
            return s;
        }

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (FMSynth& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int) override
            {
                const double hz = MidiMessage::getMidiNoteInHertz (note);
                currentHz = (o.get (pGlide) > 0.5f && lastHz > 0.0) ? lastHz : hz;
                targetHz = hz;
                lastHz = hz;
                velocity = vel;
                for (int i = 0; i < numOps; ++i)
                {
                    env[(size_t) i].setSampleRate (getSampleRate());
                    env[(size_t) i].reset();
                    env[(size_t) i].noteOn();
                    phase[(size_t) i] = 0.0;
                    out[(size_t) i] = 0.0f;
                }
                fbState = 0.0f;
                filter.reset();
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (! allowTailOff) { clearCurrentNote(); return; }
                for (auto& e : env) e.noteOff();
            }

            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& buf, int start, int num) override
            {
                if (! isVoiceActive()) return;
                const auto& alg = algorithms()[(size_t) jlimit (0, (int) algorithms().size() - 1, o.getInt (pAlgorithm))];
                double inc[numOps];
                float level[numOps];
                for (int i = 0; i < numOps; ++i)
                {
                    env[(size_t) i].setParameters (adsrFrom (o.get (opParam (i, kA)), o.get (opParam (i, kD)), o.get (opParam (i, kS)), o.get (opParam (i, kR))));
                    level[i] = o.get (opParam (i, kLevel));
                }
                const float fb = o.get (pFeedback);
                const float gain = Decibels::decibelsToGain (o.get (pVolume)) * (1.0f - o.get (pVelSens) * (1.0f - velocity));
                const bool filt = o.get (pCutoff) < 19500.0f;
                if (filt) filter.set (o.get (pCutoff), o.get (pReso), getSampleRate());
                const double glideCoeff = o.get (pGlide) > 0.5f ? std::exp (-1.0 / (o.get (pGlide) * 0.001 * getSampleRate())) : 0.0;
                const float outScale = 1.0f / std::sqrt ((float) alg.outs.size());

                for (int s = 0; s < num; ++s)
                {
                    currentHz = targetHz + (currentHz - targetHz) * glideCoeff;
                    for (int i = 0; i < numOps; ++i)
                        inc[i] = currentHz * o.get (opParam (i, kRatio)) * std::pow (2.0, o.get (opParam (i, kFine)) / 1200.0) / getSampleRate();

                    // operators 4 -> 1, each modulated by the outputs computed this sample
                    float mod[numOps] = { 0, 0, 0, 0 };
                    mod[3] = fbState * fb * 3.0f;
                    for (int i = numOps; --i >= 0;)
                    {
                        for (auto [from, to] : alg.mods)
                            if (to == i) mod[i] += out[(size_t) from] * 4.0f;
                        const float e = env[(size_t) i].getNextSample();
                        out[(size_t) i] = std::sin ((float) (MathConstants<double>::twoPi * phase[(size_t) i]) + mod[i]) * e * level[i];
                        phase[(size_t) i] += inc[i];
                        phase[(size_t) i] -= std::floor (phase[(size_t) i]);
                    }
                    fbState = 0.5f * (fbState + out[3]);
                    float y = 0.0f;
                    for (int c : alg.outs) y += out[(size_t) c];
                    y *= outScale * gain;
                    if (filt) y = filter.process (y, 0, 0);
                    buf.addSample (0, start + s, y);
                    if (buf.getNumChannels() > 1) buf.addSample (1, start + s, y);
                }

                bool anyActive = false;
                for (int c : alg.outs) anyActive = anyActive || env[(size_t) c].isActive();
                if (! anyActive) clearCurrentNote();
            }

            FMSynth& o;
            std::array<ADSR, numOps> env;
            std::array<double, numOps> phase {};
            std::array<float, numOps> out {};
            double currentHz = 440.0, targetHz = 440.0;
            static inline double lastHz = 0.0;
            float velocity = 1.0f, fbState = 0.0f;
            SVF filter;
        };
    };
}

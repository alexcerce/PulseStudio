#pragma once

#include "PulseSynths.h"

/** Keyboard instruments: the Tonewheel Organ (drawbars, percussion, vibrato and chorus, a rotary speaker) and
    the Electric Piano (tine and reed). */
namespace pulse
{
    class Organ : public PulseSynth
    {
    public:
        enum { pBar1, pBar2, pBar3, pBar4, pBar5, pBar6, pBar7, pBar8, pBar9, pPerc, pPercDecay, pPercLevel, pClick, pVibrato, pDrive, pRotary, pRotaryMix, pLevel };
        static inline const char* xmlTypeName = "pulseOrgan";
        static const char* getPluginName() { return "Tonewheel Organ"; }
        static constexpr int numBars = 9;
        static inline const StringArray barNames { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
        static constexpr float barRatios[numBars] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
        static inline const StringArray percNames { "Off", "2nd", "3rd" };
        static inline const StringArray vibratoNames { "Off", "V1", "V2", "V3", "C1", "C2", "C3" };
        static inline const StringArray rotaryNames { "Stop", "Slow", "Fast" };

        explicit Organ (te::PluginCreationInfo info) : PulseSynth (info)
        {
            const float defaults[numBars] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
            for (int i = 0; i < numBars; ++i)
                add ("bar" + String (i + 1), "Drawbar " + barNames[i], { 0.0f, 8.0f }, defaults[i], [] (float v) { return String (v, 1); });
            add ("perc", "Percussion", steps (percNames.size()), 0.0f, fmt::choice (percNames));
            add ("percdecay", "Percussion Decay", steps (2), 0.0f, fmt::choice ({ "Fast", "Slow" }));
            add ("perclevel", "Percussion Level", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("click", "Key Click", { 0.0f, 1.0f }, 0.3f, fmt::pct);
            add ("vibrato", "Vibrato / Chorus", steps (vibratoNames.size()), 0.0f, fmt::choice (vibratoNames));
            add ("drive", "Drive", { 0.0f, 1.0f }, 0.2f, fmt::pct);
            add ("rotary", "Rotary Speaker", steps (rotaryNames.size()), 1.0f, fmt::choice (rotaryNames));
            add ("rotarymix", "Rotary Amount", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("level", "Level", { -24.0f, 12.0f }, -6.0f, fmt::db);
            for (int i = 0; i < 24; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        String getName() const override       { return "Tonewheel Organ"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Drawbars", { pBar1, pBar2, pBar3, pBar4, pBar5, pBar6, pBar7, pBar8, pBar9 } }, { "Percussion", { pPerc, pPercDecay, pPercLevel, pClick } },
                     { "Tone", { pVibrato, pDrive } }, { "Speaker", { pRotary, pRotaryMix, pLevel } } };
        }

        /** How loud a drawbar setting is: each step in is about 3 dB quieter, all the way in is silent. */
        static float barGain (float v) noexcept { return v < 0.05f ? 0.0f : std::pow (10.0f, (v - 8.0f) * 3.0f / 20.0f); }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            PulseSynth::initialise (info);
            for (auto& d : vibLine) d.setMax ((int) (sampleRate * 0.01) + 16);
            for (auto& d : hornLine) d.setMax ((int) (sampleRate * 0.01) + 16);
            for (auto& f : split) f.reset();
            vibPhase = hornAngle = drumAngle = 0.0;
            hornSpeed = drumSpeed = 0.0;
            keysDown = 0;
        }

        // for the display: where the horn and the drum of the speaker are (turns), and how fast (turns per second)
        std::atomic<float> hornTurn { 0.0f }, drumTurn { 0.0f }, hornHz { 0.0f };

    protected:
        void afterBlock (AudioBuffer<float>& buffer, int start, int num) override
        {
            auto* l = buffer.getWritePointer (0, start);
            auto* r = buffer.getWritePointer (1, start);
            const int vib = getInt (pVibrato), rotary = getInt (pRotary);
            const float drive = get (pDrive), level = Decibels::decibelsToGain (get (pLevel)), rotMix = get (pRotaryMix);
            // the scanner vibrato: three depths; the C settings add the straight sound back (a chorus)
            const float vibDepth = vib == 0 ? 0.0f : (float) (sampleRate * 0.001) * (0.15f + 0.2f * (float) ((vib - 1) % 3));
            const bool chorus = vib >= 4;
            // the speaker: the horn (highs) and the drum (lows) each take their own time to reach a new speed
            const double hornTarget = rotary == 0 ? 0.0 : rotary == 1 ? 0.8 : 6.8, drumTarget = rotary == 0 ? 0.0 : rotary == 1 ? 0.7 : 5.9;
            const double hornA = 1.0 - std::exp (-1.0 / (0.8 * sampleRate)), drumA = 1.0 - std::exp (-1.0 / (3.5 * sampleRate));
            const float driveGain = 1.0f + drive * 8.0f, driveMakeup = 1.0f / (1.0f + drive * 2.5f);
            const float dopp = (float) (sampleRate * 0.00035);
            for (int ch = 0; ch < 2; ++ch) split[(size_t) ch].set (800.0f, 0.0f, sampleRate);

            for (int i = 0; i < num; ++i)
            {
                float x = 0.5f * (l[i] + r[i]);
                if (vibDepth > 0.0f)
                {
                    vibLine[0].push (x);
                    vibPhase += 6.9 / sampleRate;
                    if (vibPhase >= 1.0) vibPhase -= 1.0;
                    const float wob = vibLine[0].read (1.0f + vibDepth * (1.0f + (float) std::sin (vibPhase * MathConstants<double>::twoPi)));
                    x = chorus ? 0.6f * (x + wob) : wob;
                }
                x = std::tanh (x * driveGain) * driveMakeup;

                hornSpeed += (hornTarget - hornSpeed) * hornA;
                drumSpeed += (drumTarget - drumSpeed) * drumA;
                hornAngle += hornSpeed / sampleRate; hornAngle -= std::floor (hornAngle);
                drumAngle += drumSpeed / sampleRate; drumAngle -= std::floor (drumAngle);
                const float low = split[0].process (x, 0, 0), high = x - low;
                hornLine[0].push (high);
                const float hs = (float) std::sin (hornAngle * MathConstants<double>::twoPi), hc = (float) std::cos (hornAngle * MathConstants<double>::twoPi);
                const float ds = (float) std::sin (drumAngle * MathConstants<double>::twoPi);
                // two microphones on opposite sides: as the horn turns towards one it gets louder and higher there
                const float hornL = hornLine[0].read (1.0f + dopp * (1.0f + hs)) * (0.7f + 0.3f * hc);
                const float hornR = hornLine[0].read (1.0f + dopp * (1.0f - hs)) * (0.7f - 0.3f * hc);
                const float drumL = low * (0.85f + 0.15f * ds), drumR = low * (0.85f - 0.15f * ds);
                const float wetL = (hornL + drumL) * 1.25f, wetR = (hornR + drumR) * 1.25f;
                l[i] = (x + (wetL - x) * rotMix) * level;
                r[i] = (x + (wetR - x) * rotMix) * level;
            }
            hornTurn.store ((float) hornAngle); drumTurn.store ((float) drumAngle); hornHz.store ((float) hornSpeed);
        }

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (Organ& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float, SynthesiserSound*, int) override
            {
                hz = MidiMessage::getMidiNoteInHertz (note);
                for (auto& p : phase) p = 0.0;
                percPhase = 0.0;
                amp = 0.0f;
                held = true;
                // the percussion only sounds on the first key of a phrase: it does not fire again while any key is still down
                percLevel = o.keysDown == 0 ? 1.0f : 0.0f;
                ++o.keysDown;
                clickLeft = (int) (getSampleRate() * 0.004);
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (held) o.keysDown = jmax (0, o.keysDown - 1);
                held = false;
                if (! allowTailOff) { amp = 0.0f; clearCurrentNote(); }
                else clickLeft = (int) (getSampleRate() * 0.002);
            }

            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;
                const double sr = getSampleRate();
                float gains[numBars]; double inc[numBars];
                for (int b = 0; b < numBars; ++b)
                {
                    gains[b] = barGain (o.get (pBar1 + b));
                    double f = hz * barRatios[b];
                    while (f > 5900.0) f *= 0.5;   // the top tonewheels do not go that high: the note comes from an octave lower
                    inc[b] = f / sr;
                }
                const int perc = o.getInt (pPerc);
                const float percGain = perc == 0 ? 0.0f : o.get (pPercLevel) * 1.4f;
                const double percInc = hz * (perc == 2 ? 3.0 : 2.0) / sr;
                const float percFall = std::exp (-1.0f / (float) ((o.getInt (pPercDecay) > 0 ? 1.0 : 0.2) * 0.35 * sr));
                const float click = o.get (pClick) * 0.25f;
                const float up = 1.0f - std::exp (-1.0f / (float) (0.002 * sr)), down = 1.0f - std::exp (-1.0f / (float) (0.008 * sr));
                auto* l = out.getWritePointer (0, start);
                auto* r = out.getWritePointer (1, start);

                for (int i = 0; i < num; ++i)
                {
                    amp += ((held ? 1.0f : 0.0f) - amp) * (held ? up : down);
                    float s = 0.0f;
                    for (int b = 0; b < numBars; ++b)
                    {
                        if (gains[b] > 0.0f) s += gains[b] * (float) std::sin (phase[b] * MathConstants<double>::twoPi);
                        phase[b] += inc[b];
                        if (phase[b] >= 1.0) phase[b] -= 1.0;
                    }
                    if (percGain > 0.0f && percLevel > 1.0e-4f)
                    {
                        s += percGain * percLevel * (float) std::sin (percPhase * MathConstants<double>::twoPi);
                        percPhase += percInc;
                        if (percPhase >= 1.0) percPhase -= 1.0;
                        percLevel *= percFall;
                    }
                    if (clickLeft > 0) { s += click * (rng.nextFloat() * 2.0f - 1.0f); --clickLeft; }   // the contacts closing
                    const float v = s * amp * 0.14f;
                    l[i] += v; r[i] += v;
                }
                if (! held && amp < 1.0e-4f) clearCurrentNote();
            }

            Organ& o;
            double hz = 440.0, phase[numBars] {}, percPhase = 0.0;
            float amp = 0.0f, percLevel = 0.0f;
            bool held = false;
            int clickLeft = 0;
            Random rng { 1955 };
        };

        std::array<DelayLine, 1> vibLine, hornLine;
        std::array<SVF, 2> split;
        double vibPhase = 0.0, hornAngle = 0.0, drumAngle = 0.0, hornSpeed = 0.0, drumSpeed = 0.0;
        int keysDown = 0;
    };

    //==============================================================================
    /** Electric Piano: a hammer strikes a tine (a bell-like start over a round body) or a reed (nasal, barking
        when hit hard); the harder the key is played, the brighter. With tremolo that moves between the speakers. */
    class EPiano : public PulseSynth
    {
    public:
        enum { pModel, pBell, pBark, pHardness, pDecay, pRelease, pTone, pTremolo, pTremRate, pTremStereo, pDrive, pLevel };
        static inline const char* xmlTypeName = "pulseEPiano";
        static const char* getPluginName() { return "Electric Piano"; }
        static inline const StringArray modelNames { "Tine", "Reed" };

        explicit EPiano (te::PluginCreationInfo info) : PulseSynth (info)
        {
            add ("model", "Model", steps (modelNames.size()), 0.0f, fmt::choice (modelNames));
            add ("bell", "Bell", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("bark", "Bark", { 0.0f, 1.0f }, 0.45f, fmt::pct);
            add ("hardness", "Hardness", { 0.0f, 1.0f }, 0.7f, fmt::pct);
            add ("decay", "Decay", skewed (0.3f, 12.0f, 3.0f), 3.5f, [] (float v) { return String (v, 1) + " s"; });
            add ("release", "Release", skewed (20.0f, 2000.0f, 200.0f), 180.0f, fmt::ms);
            add ("tone", "Tone", skewed (600.0f, 16000.0f, 4000.0f), 7000.0f, fmt::hz);
            add ("tremolo", "Tremolo", { 0.0f, 1.0f }, 0.25f, fmt::pct);
            add ("tremrate", "Tremolo Rate", skewed (0.5f, 12.0f, 4.0f), 4.5f, [] (float v) { return String (v, 2) + " Hz"; });
            add ("tremstereo", "Tremolo Stereo", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("drive", "Drive", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("level", "Level", { -24.0f, 12.0f }, -6.0f, fmt::db);
            for (int i = 0; i < 24; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        String getName() const override       { return "Electric Piano"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Sound", { pModel, pBell, pBark, pHardness, pTone } }, { "Time", { pDecay, pRelease } }, { "Tremolo", { pTremolo, pTremRate, pTremStereo } }, { "Output", { pDrive, pLevel } } };
        }

        /** One moment of the sound, for the display and the voices alike: `cycle` 0..1 through one period of the note,
            `since` seconds after the key was struck, `vel` 0..1, for a note of `hz`. */
        float wave (double cycle, float since, float vel, float hz) const
        {
            const bool reed = getInt (pModel) == 1;
            const float hard = 1.0f - get (pHardness) * (1.0f - vel);                 // soft playing is duller, by as much as Hardness says
            const float keyScale = jlimit (0.35f, 1.3f, std::pow (261.6f / hz, 0.35f)); // high notes are purer
            const double ph = cycle * MathConstants<double>::twoPi;
            if (reed)
            {
                // a reed: the wave folds over itself more the harder it is hit, and that dies away quickly
                const float index = get (pBark) * 3.2f * hard * keyScale * (0.35f + 0.65f * std::exp (-since * 6.0f));
                const float body = (float) std::sin (ph + index * std::sin (ph) + 0.4f * index * std::sin (2.0 * ph));
                const float bell = get (pBell) * 0.25f * hard * std::exp (-since * 18.0f) * (float) std::sin (ph * 5.0);
                return body + bell;
            }
            // a tine: a round body that barks a little at the start, and a high bell from the tine itself that is gone in a moment
            const float index = get (pBark) * 2.2f * hard * keyScale * (0.2f + 0.8f * std::exp (-since * 4.0f));
            const float body = (float) std::sin (ph + index * std::sin (ph));
            const float bellIndex = 1.5f * hard * std::exp (-since * 9.0f);
            const float bell = get (pBell) * 0.45f * hard * std::exp (-since * 7.0f) * (float) std::sin (ph * 1.0 + bellIndex * std::sin (ph * 14.0));
            return body * 0.85f + bell;
        }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            PulseSynth::initialise (info);
            for (auto& f : toneLp) f.reset();
            lastTone = -1.0f;
            tremPhase = 0.0;
        }

        std::atomic<float> tremNow { 0.0f };   // where the tremolo is in its cycle (0..1), for the display

    protected:
        void afterBlock (AudioBuffer<float>& buffer, int start, int num) override
        {
            auto* l = buffer.getWritePointer (0, start);
            auto* r = buffer.getWritePointer (1, start);
            const float trem = get (pTremolo), stereo = get (pTremStereo), drive = get (pDrive), level = Decibels::decibelsToGain (get (pLevel));
            if (std::abs (get (pTone) - lastTone) > 1.0f) { for (auto& f : toneLp) f.set (Biquad::Type::highCut, sampleRate, get (pTone), 0.6, 0.0); lastTone = get (pTone); }
            const double inc = get (pTremRate) / sampleRate;
            const float dg = 1.0f + drive * 6.0f, dm = 1.0f / (1.0f + drive * 1.8f);
            for (int i = 0; i < num; ++i)
            {
                tremPhase += inc;
                if (tremPhase >= 1.0) tremPhase -= 1.0;
                const float t = (float) std::sin (tremPhase * MathConstants<double>::twoPi);
                // at full Stereo the two sides swell in turn; at none they swell together
                const float gl = 1.0f - trem * 0.5f * (1.0f + t), gr = 1.0f - trem * 0.5f * (1.0f + t * (1.0f - 2.0f * stereo));
                // the pickup and the amplifier are a little lopsided, which adds the warmth (and the bite when pushed)
                auto amp = [dg, dm] (float x) { x *= dg; return (std::tanh (x + 0.15f * x * x) ) * dm; };
                l[i] = toneLp[0].process (amp (l[i]), 0) * gl * level;
                r[i] = toneLp[1].process (amp (r[i]), 0) * gr * level;
            }
            tremNow.store ((float) tremPhase);
        }

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (EPiano& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int pitchWheel) override
            {
                hz = (float) MidiMessage::getMidiNoteInHertz (note);
                velocity = vel;
                cycle = 0.0;
                since = 0.0f;
                level = 0.0f;
                released = false;
                relGain = 1.0f;
                pitchWheelMoved (pitchWheel);
                // low notes ring longer than high ones, as the real bars do
                decayScale = jlimit (0.25f, 2.0f, std::pow (261.6f / hz, 0.45f));
            }
            void stopNote (float, bool allowTailOff) override
            {
                released = true;
                if (! allowTailOff) { relGain = 0.0f; clearCurrentNote(); }
            }
            void pitchWheelMoved (int v) override { bend = (float) (v - 8192) / 8192.0f * 2.0f; }
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;
                const double sr = getSampleRate();
                const double inc = hz * std::pow (2.0, bend / 12.0) / sr;
                const float decay = o.get (pDecay) * decayScale;
                const float fall = std::exp (-1.0f / (float) (decay / 6.9 * sr));           // 60 dB down after Decay
                const float relFall = std::exp (-1.0f / (float) (o.get (pRelease) * 0.001 / 6.9 * sr));
                const float attack = 1.0f - std::exp (-1.0f / (float) (0.0015 * sr));
                const float vol = 0.25f * (0.25f + 0.75f * velocity * velocity);
                auto* l = out.getWritePointer (0, start);
                auto* r = out.getWritePointer (1, start);
                for (int i = 0; i < num; ++i)
                {
                    level += (1.0f - level) * attack;
                    ring = since == 0.0f ? 1.0f : ring * fall;
                    if (released) relGain *= relFall;
                    const float v = o.wave (cycle, since, velocity, hz) * level * ring * relGain * vol;
                    l[i] += v; r[i] += v;
                    cycle += inc;
                    if (cycle >= 1.0) cycle -= 1.0;
                    since += (float) (1.0 / sr);
                }
                if (ring * relGain < 1.0e-4f) clearCurrentNote();
            }

            EPiano& o;
            float hz = 440.0f, velocity = 1.0f, since = 0.0f, level = 0.0f, ring = 1.0f, relGain = 1.0f, bend = 0.0f, decayScale = 1.0f;
            double cycle = 0.0;
            bool released = false;
        };

        std::array<Biquad, 2> toneLp;
        float lastTone = -1.0f;
        double tremPhase = 0.0;
    };
}

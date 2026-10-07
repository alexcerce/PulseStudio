#pragma once

#include "Synths2.h"

/** Pulse Kick: a kick drum made from nothing but a falling tone and a click. */
namespace pulse
{
    /** Everything that makes one kick, in one place, so that the picture in the window can make the very same
        kick the instrument plays. */
    struct KickShape
    {
        float tuneHz = 48.0f, pitchSemis = 36.0f, pitchMs = 60.0f, pitchCurve = 0.7f;
        float holdMs = 20.0f, decayMs = 450.0f, ampCurve = 0.5f;
        float click = 0.35f, clickMs = 6.0f, clickHz = 3500.0f;
        float shape = 0.1f, driveDb = 6.0f, phaseDeg = 0.0f;

        /** A fall from 1 to 0 over `length`: a straight line at curve 0, a quick drop with a long foot at curve 1. */
        static float fall (double t, double length, float curve) noexcept
        {
            if (t >= length || length <= 0.0) return 0.0f;
            const double u = t / length, line = 1.0 - u, bent = std::exp (-5.0 * u) * (1.0 - u * u * u);   // (the last factor lands it on 0)
            return (float) (line + (bent - line) * curve);
        }
        /** The pitch of the body, t seconds into the kick. */
        float pitchAt (double t) const noexcept { return tuneHz * std::pow (2.0f, pitchSemis / 12.0f * fall (t, pitchMs * 0.001, pitchCurve)); }
        /** How loud the body is (0..1), t seconds in: up within a millisecond, held, then falling. */
        float ampAt (double t) const noexcept
        {
            const double hold = holdMs * 0.001;
            const float rise = (float) jmin (1.0, t / 0.001);
            return rise * (t <= hold ? 1.0f : fall (t - hold, decayMs * 0.001, ampCurve));
        }
        double length() const noexcept { return (holdMs + decayMs) * 0.001; }
    };

    /** Makes the samples of a kick, one at a time. */
    struct KickMaker
    {
        void start (const KickShape& s, double sampleRate, float level, uint32 seed)
        {
            shape = s; sr = sampleRate; gain = level; t = 0;
            phase = s.phaseDeg / 360.0;
            rng = seed != 0 ? seed : 1u;
            bp1 = bp2 = 0.0f;
            const float g = Decibels::decibelsToGain (s.driveDb);
            drive = g; driveLevel = 1.0f / std::tanh (jmax (0.5f, g * 0.7f));
            clickA = 1.0f - std::exp (-MathConstants<float>::twoPi * jmin (0.45f, s.clickHz / (float) sr));
            clickB = 1.0f - std::exp (-MathConstants<float>::twoPi * jmin (0.45f, s.clickHz * 0.25f / (float) sr));
            active = true;
        }
        bool isActive() const noexcept { return active; }

        inline float next() noexcept
        {
            if (! active) return 0.0f;
            const double time = (double) t / sr;
            phase += shape.pitchAt (time) / sr;
            if (phase >= 1.0) phase -= std::floor (phase);
            float body = (float) std::sin (phase * MathConstants<double>::twoPi);
            // Shape: from a pure tone towards a squarer, fuller one
            if (shape.shape > 0.001f) { const float k = 1.0f + shape.shape * 7.0f; body = std::tanh (body * k) / std::tanh (k); }
            body *= shape.ampAt (time);
            // the click: a band of noise that is over in a few milliseconds
            float click = 0.0f;
            if (const double clickLen = shape.clickMs * 0.001 * 4.0; shape.click > 0.001f && time < clickLen)
            {
                rng = rng * 1664525u + 1013904223u;
                const float white = (float) (int32) rng / 2147483648.0f;
                bp1 += (white - bp1) * clickA;
                bp2 += (bp1 - bp2) * clickB;
                click = (bp1 - bp2) * shape.click * 1.6f * (float) std::exp (-time / (shape.clickMs * 0.001)) * (float) (1.0 - time / clickLen);
            }
            const float out = std::tanh ((body + click) * drive * 0.7f) * driveLevel;
            if (++t > (int64) ((shape.length() + 0.002) * sr)) active = false;
            return out * gain;
        }

        KickShape shape;
        double sr = 48000.0, phase = 0.0;
        int64 t = 0;
        uint32 rng = 1;
        float gain = 1.0f, drive = 1.0f, driveLevel = 1.0f, bp1 = 0.0f, bp2 = 0.0f, clickA = 0.3f, clickB = 0.1f;
        bool active = false;
    };

    //==============================================================================
    class KickSynth : public PulseSynth
    {
    public:
        enum { pTune, pKeys, pPitch, pPitchTime, pPitchCurve, pHold, pDecay, pAmpCurve, pClick, pClickTime, pClickTone, pShape, pDrive, pPhase, pVelocity, pLevel };
        static inline const char* xmlTypeName = "pulseKick";
        static const char* getPluginName() { return "Pulse Kick"; }

        explicit KickSynth (te::PluginCreationInfo info) : PulseSynth (info)
        {
            add ("tune", "Tune", skewed (25.0f, 150.0f, 55.0f), 48.0f, [] (float v) { return String (v, 1) + " Hz"; });
            add ("keys", "Keys", steps (2), 0.0f, fmt::choice ({ "One pitch", "Follows the keys" }));
            add ("pitch", "Pitch Fall", { 0.0f, 72.0f }, 36.0f, fmt::st);
            add ("pitchtime", "Pitch Time", skewed (5.0f, 600.0f, 80.0f), 60.0f, fmt::ms);
            add ("pitchcurve", "Pitch Curve", { 0.0f, 1.0f }, 0.7f, fmt::pct);
            add ("hold", "Hold", { 0.0f, 250.0f }, 20.0f, fmt::ms);
            add ("decay", "Decay", skewed (40.0f, 3000.0f, 500.0f), 450.0f, fmt::ms);
            add ("ampcurve", "Decay Curve", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("click", "Click", { 0.0f, 1.0f }, 0.35f, fmt::pct);
            add ("clicktime", "Click Time", skewed (1.0f, 40.0f, 8.0f), 6.0f, fmt::ms);
            add ("clicktone", "Click Tone", skewed (400.0f, 12000.0f, 3000.0f), 3500.0f, fmt::hz);
            add ("shape", "Shape", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("drive", "Drive", { 0.0f, 36.0f }, 6.0f, fmt::db);
            add ("phase", "Start Phase", { 0.0f, 180.0f }, 0.0f, [] (float v) { return String (roundToInt (v)) + String::fromUTF8 ("\xc2\xb0"); });
            add ("velocity", "Velocity", { 0.0f, 1.0f }, 0.4f, fmt::pct);
            add ("level", "Level", { -24.0f, 6.0f }, -6.0f, fmt::db);
            for (int i = 0; i < 4; ++i) synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            // one kick at a time: the one that is sounding is faded out (in a few milliseconds, not cut) as the next begins
            synth.acceptNoteOn = [this] (int)
            {
                for (int i = 0; i < synth.getNumVoices(); ++i) if (auto* v = dynamic_cast<Voice*> (synth.getVoice (i))) v->choke();
                return true;
            };
        }

        String getName() const override       { return "Pulse Kick"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "Body", { pTune, pKeys, pShape, pPhase } }, { "Pitch", { pPitch, pPitchTime, pPitchCurve } }, { "Amp", { pHold, pDecay, pAmpCurve } },
                     { "Click", { pClick, pClickTime, pClickTone } }, { "Output", { pDrive, pVelocity, pLevel } } };
        }

        /** The kick the settings make now (played on `note` when it follows the keys: C1 is the Tune itself). */
        KickShape shapeNow (int note = 36) const
        {
            KickShape s;
            s.tuneHz = get (pTune) * (getInt (pKeys) > 0 ? std::pow (2.0f, (float) (note - 36) / 12.0f) : 1.0f);
            s.pitchSemis = get (pPitch); s.pitchMs = get (pPitchTime); s.pitchCurve = get (pPitchCurve);
            s.holdMs = get (pHold); s.decayMs = get (pDecay); s.ampCurve = get (pAmpCurve);
            s.click = get (pClick); s.clickMs = get (pClickTime); s.clickHz = get (pClickTone);
            s.shape = get (pShape); s.driveDb = get (pDrive); s.phaseDeg = get (pPhase);
            return s;
        }

        std::atomic<int> hits { 0 };   // (for the display: a kick has just been played)

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (KickSynth& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int note, float vel, SynthesiserSound*, int) override
            {
                const float sens = o.get (pVelocity);
                const float level = Decibels::decibelsToGain (o.get (pLevel)) * (1.0f - sens + sens * vel * vel);
                maker.start (o.shapeNow (note), getSampleRate(), level, (uint32) (++o.hits) * 2654435761u);
                fade = 1.0f; fading = false;
            }
            // a kick is a one-shot: letting go of the key does not cut it short
            void stopNote (float, bool allowTailOff) override
            {
                if (! allowTailOff) { maker.active = false; clearCurrentNote(); }
            }
            void choke() { if (isVoiceActive() && maker.isActive()) fading = true; }
            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;
                auto* l = out.getWritePointer (0, start);
                auto* r = out.getWritePointer (1, start);
                const float fadeStep = 1.0f / (float) (0.004 * getSampleRate());
                for (int i = 0; i < num; ++i)
                {
                    float v = maker.next();
                    if (fading) { fade = jmax (0.0f, fade - fadeStep); v *= fade; }
                    l[i] += v; r[i] += v;
                }
                if (! maker.isActive() || (fading && fade <= 0.0f)) { maker.active = false; clearCurrentNote(); }
            }

            KickSynth& o;
            KickMaker maker;
            float fade = 1.0f;
            bool fading = false;
        };
    };
}

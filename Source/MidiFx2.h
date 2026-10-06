#pragma once

#include "MidiFx.h"

/** More MIDI effects: Note Length, Pitch, Random and Velocity. */
namespace pulse
{
    /** Gives every note a set length, whatever was played: a time, or a note value at the song's tempo. It can also
        play the note when the key is let go instead of when it is pressed. */
    class NoteLength : public PulseMidiEffect
    {
    public:
        enum { pMode, pTime, pRate, pGate, pTrigger };
        static inline const char* xmlTypeName = "pulseNoteLength";
        static const char* getPluginName() { return "Note Length"; }
        static inline const StringArray lengthNames { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar" };
        static constexpr double lengthBeats[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };

        explicit NoteLength (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            add ("mode", "In Time With The Song", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));   // off: Time in milliseconds; on: a note value
            add ("time", "Time", skewed (5.0f, 5000.0f, 200.0f), 120.0f, fmt::ms);
            add ("rate", "Length", steps (lengthNames.size()), 1.0f, fmt::choice (lengthNames));
            add ("gate", "Gate", { 0.05f, 2.0f }, 1.0f, fmt::pct);
            add ("trigger", "Play When The Key Comes Up", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override  { return "Note Length"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Length", { pMode, pTime, pRate, pGate, pTrigger } } }; }

        /** How long a note lasts, in samples. */
        int64 lengthSamples() const
        {
            const double base = getInt (pMode) == 0 ? get (pTime) * 0.001 * sampleRate : samplesPerBeat() * lengthBeats[jlimit (0, 5, getInt (pRate))];
            return jmax<int64> (16, (int64) (base * get (pGate)));
        }

    protected:
        void reset() override { heldVelocity.fill (0); }

        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            const bool onRelease = getInt (pTrigger) == 1;
            for (auto& m : in)
            {
                if (! m.isNoteOnOrOff()) { b.emit (m, offsetOf (m)); continue; }
                const int note = m.getNoteNumber();
                if (m.isNoteOn()) heldVelocity[(size_t) note] = m.getVelocity();
                if (m.isNoteOn() != onRelease)   // this is the moment the note is to start
                {
                    const int vel = m.isNoteOn() ? m.getVelocity() : jmax (1, heldVelocity[(size_t) note]);
                    // still going from last time (its end is still waiting to be sent): end it now, and forget that scheduled end
                    auto isItsEnd = [note] (auto& p) { return p.second.isNoteOff() && p.second.getNoteNumber() == note; };
                    if (std::find_if (pending.begin(), pending.end(), isItsEnd) != pending.end())
                    {
                        b.emit (MidiMessage::noteOff (m.getChannel(), note), offsetOf (m));
                        pending.erase (std::remove_if (pending.begin(), pending.end(), isItsEnd), pending.end());
                    }
                    b.emit (MidiMessage::noteOn (m.getChannel(), note, (uint8) jlimit (1, 127, vel)), offsetOf (m));
                    schedule (b.start + offsetOf (m) + lengthSamples(), MidiMessage::noteOff (m.getChannel(), note));
                }
                // (the note's own ending, or its beginning when it plays on key up, is not passed on)
            }
        }

    private:
        std::array<int, 128> heldVelocity {};
    };

    //==============================================================================
    /** Moves every note up or down, and can let through only the notes in a range (after moving them). */
    class PitchFx : public PulseMidiEffect
    {
    public:
        enum { pSemis, pOctaves, pLowest, pRange };
        static inline const char* xmlTypeName = "pulsePitch";
        static const char* getPluginName() { return "Pitch"; }

        explicit PitchFx (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            auto noteName = [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); };
            add ("semis", "Semitones", { -24.0f, 24.0f, 1.0f }, 0.0f, fmt::st);
            add ("octaves", "Octaves", { -4.0f, 4.0f, 1.0f }, 0.0f, [] (float v) { const int o = roundToInt (v); return (o > 0 ? "+" : "") + String (o) + " oct"; });
            add ("lowest", "Lowest Note", { 0.0f, 127.0f, 1.0f }, 0.0f, noteName);
            add ("range", "Range", { 1.0f, 128.0f, 1.0f }, 128.0f, [] (float v) { return roundToInt (v) >= 128 ? String ("All") : String (roundToInt (v)) + " notes"; });
        }

        String getName() const override  { return "Pitch"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Move", { pSemis, pOctaves } }, { "Let through", { pLowest, pRange } } }; }

        /** What a note becomes, or -1 if it is not let through. */
        int map (int note) const
        {
            const int p = note + getInt (pSemis) + 12 * getInt (pOctaves), lo = getInt (pLowest);
            return p < 0 || p > 127 || p < lo || p >= lo + getInt (pRange) ? -1 : p;
        }

    protected:
        void reset() override { mapped.fill (-1); }
        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            for (auto& m : in)
            {
                if (m.isNoteOn())
                {
                    const int p = map (m.getNoteNumber());
                    mapped[(size_t) m.getNoteNumber()] = p;
                    if (p >= 0) b.emit (MidiMessage::noteOn (m.getChannel(), p, m.getVelocity()), offsetOf (m));
                }
                else if (m.isNoteOff())
                {
                    if (const int p = mapped[(size_t) m.getNoteNumber()]; p >= 0) b.emit (MidiMessage::noteOff (m.getChannel(), p), offsetOf (m));
                    mapped[(size_t) m.getNoteNumber()] = -1;
                }
                else b.emit (m, offsetOf (m));
            }
        }

    private:
        std::array<int, 128> mapped {};
    };

    //==============================================================================
    /** Now and then moves a note by a random number of steps: Chance says how often, Choices how many different steps
        there are, Step how big each is. With a scale chosen, what comes out stays in it. */
    class RandomFx : public PulseMidiEffect
    {
    public:
        enum { pChance, pChoices, pStep, pDirection, pRoot, pScale };
        static inline const char* xmlTypeName = "pulseRandom";
        static const char* getPluginName() { return "Random"; }

        explicit RandomFx (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            auto scales = Scales::names();
            scales.insert (0, "Any note");
            add ("chance", "Chance", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("choices", "Choices", { 1.0f, 24.0f, 1.0f }, 3.0f, [] (float v) { return String (roundToInt (v)); });
            add ("step", "Step", { 1.0f, 12.0f, 1.0f }, 1.0f, fmt::st);
            add ("direction", "Direction", steps (3), 2.0f, fmt::choice ({ "Up", "Down", "Both" }));
            add ("root", "Root", steps (12), 0.0f, fmt::choice (Scales::rootNames()));
            add ("scale", "Stay In", steps (scales.size()), 0.0f, fmt::choice (scales));
        }

        String getName() const override  { return "Random"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Random", { pChance, pChoices, pStep, pDirection } }, { "Scale", { pRoot, pScale } } }; }

        /** The furthest a note can be moved, in semitones. */
        int reach() const { return getInt (pChoices) * getInt (pStep); }

    protected:
        void reset() override { mapped.fill (-1); }
        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            for (auto& m : in)
            {
                if (m.isNoteOn())
                {
                    int p = m.getNoteNumber();
                    if (random.nextFloat() < get (pChance))
                    {
                        const int steps = 1 + random.nextInt (jmax (1, getInt (pChoices))), dir = getInt (pDirection);
                        p += steps * getInt (pStep) * (dir == 0 ? 1 : dir == 1 ? -1 : (random.nextBool() ? 1 : -1));
                        p = jlimit (0, 127, p);
                        if (const int sc = getInt (pScale); sc > 0) p = Scales::snap (sc - 1, getInt (pRoot), p);
                    }
                    mapped[(size_t) m.getNoteNumber()] = p;
                    b.emit (MidiMessage::noteOn (m.getChannel(), p, m.getVelocity()), offsetOf (m));
                }
                else if (m.isNoteOff())
                {
                    const int p = mapped[(size_t) m.getNoteNumber()];
                    b.emit (MidiMessage::noteOff (m.getChannel(), p >= 0 ? p : m.getNoteNumber()), offsetOf (m));
                    mapped[(size_t) m.getNoteNumber()] = -1;
                }
                else b.emit (m, offsetOf (m));
            }
        }

    private:
        std::array<int, 128> mapped {};
        Random random { 4242 };
    };

    //==============================================================================
    /** Reshapes how hard the notes are played: a curve (soft playing brought up, or only hard playing left loud), a
        range the result is fitted into, some randomness, or every note the same. */
    class VelocityFx : public PulseMidiEffect
    {
    public:
        enum { pMode, pCurve, pOutLow, pOutHigh, pRandom };
        static inline const char* xmlTypeName = "pulseVelocity";
        static const char* getPluginName() { return "Velocity"; }

        explicit VelocityFx (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            auto whole = [] (float v) { return String (roundToInt (v)); };
            add ("mode", "All The Same", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));   // on: every note comes out at Highest
            add ("curve", "Curve", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v > 0.02f ? "Louder " + String (roundToInt (v * 100)) + "%" : v < -0.02f ? "Softer " + String (roundToInt (-v * 100)) + "%" : String ("Straight"); });
            add ("outlow", "Lowest", { 1.0f, 127.0f, 1.0f }, 1.0f, whole);
            add ("outhigh", "Highest", { 1.0f, 127.0f, 1.0f }, 127.0f, whole);
            add ("random", "Random", { 0.0f, 1.0f }, 0.0f, fmt::pct);
        }

        String getName() const override  { return "Velocity"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Shape", { pMode, pCurve, pRandom } }, { "Fit into", { pOutLow, pOutHigh } } }; }

        /** What a velocity (1..127) becomes, before the randomness. */
        int map (int vel) const
        {
            const int lo = jmin (getInt (pOutLow), getInt (pOutHigh)), hi = jmax (getInt (pOutLow), getInt (pOutHigh));
            if (getInt (pMode) == 1) return hi;
            const float x = jlimit (0.0f, 1.0f, (float) (vel - 1) / 126.0f);
            // the curve bends upwards for "louder" (soft notes come up) and downwards for "softer"
            const float shaped = std::pow (x, std::pow (4.0f, -get (pCurve)));
            return jlimit (1, 127, roundToInt ((float) lo + shaped * (float) (hi - lo)));
        }

    protected:
        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            const float rnd = get (pRandom);
            for (auto& m : in)
            {
                if (! m.isNoteOn()) { b.emit (m, offsetOf (m)); continue; }
                int v = map (m.getVelocity());
                if (rnd > 0.001f) v = jlimit (1, 127, v + roundToInt ((random.nextFloat() * 2.0f - 1.0f) * rnd * 64.0f));
                b.emit (MidiMessage::noteOn (m.getChannel(), m.getNoteNumber(), (uint8) v), offsetOf (m));
            }
        }

    private:
        Random random { 99 };
    };
}

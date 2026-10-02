#pragma once

#include "PulseDevices.h"
#include "Scales.h"

/** MIDI effects (placed before an instrument): Arpeggiator, Chord, Scale and Note Echo.
    They rewrite the MIDI passing through the chain; audio passes through untouched. */
namespace pulse
{
    class PulseMidiEffect : public PulsePlugin,
                            private Timer
    {
    public:
        explicit PulseMidiEffect (te::PluginCreationInfo info) : PulsePlugin (info)
        {
            bpm.store (edit.tempoSequence.getTempo (0)->getBpm());
            startTimer (250);
        }

        bool takesMidiInput() override                       { return true; }
        bool takesAudioInput() override                      { return true; }
        bool producesAudioWhenNoAudioInput() override        { return false; }
        bool isSynth() override                              { return false; }
        int getNumOutputChannelsGivenInputs (int n) override { return n; }
        BusLayout getBusses() const override                 { return BusLayout::singleStereoInOut(); }
        void deinitialise() override                         {}

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            sampleRate = info.sampleRate;
            blockSize = info.blockSizeSamples;
            pending.clear();
            samplePos = 0;
            reset();
        }

        void applyToBuffer (const te::PluginRenderContext& fc) override
        {
            auto* midi = fc.bufferForMidiMessages;
            const int n = fc.bufferNumSamples;
            if (midi == nullptr || n <= 0) { samplePos += jmax (0, n); return; }

            if (midi->isAllNotesOff)
            {
                pending.clear();
                reset();
            }

            Block b { *this, fc, samplePos, n };
            std::vector<te::MidiMessageWithSource> in (midi->begin(), midi->end());
            std::stable_sort (in.begin(), in.end(), [] (auto& x, auto& y) { return x.getTimeStamp() < y.getTimeStamp(); });
            sourceID = in.empty() ? sourceID : in.front().mpeSourceID;

            process (in, b);

            // scheduled events (echoes, arpeggio steps, note-offs) that fall in this block
            for (size_t i = 0; i < pending.size();)
            {
                if (pending[i].first < samplePos + n)
                {
                    b.emit (pending[i].second, (int) jmax<int64> (0, pending[i].first - samplePos));
                    pending.erase (pending.begin() + (long) i);
                }
                else ++i;
            }

            std::stable_sort (b.out.begin(), b.out.end(), [] (auto& x, auto& y) { return x.getTimeStamp() < y.getTimeStamp(); });
            const bool allOff = midi->isAllNotesOff;
            midi->clear();
            midi->isAllNotesOff = allOff;
            for (auto& m : b.out) midi->add (m);
            samplePos += n;
        }

    protected:
        struct Block
        {
            PulseMidiEffect& fx;
            const te::PluginRenderContext& fc;
            int64 start;
            int numSamples;
            std::vector<te::MidiMessageWithSource> out;

            void emit (const MidiMessage& m, int offset)
            {
                te::MidiMessageWithSource w (m, fx.sourceID);
                w.setTimeStamp (jlimit (0, numSamples - 1, offset) / fx.sampleRate);
                out.push_back (w);
            }
        };

        virtual void reset() {}
        virtual void process (const std::vector<te::MidiMessageWithSource>& in, Block&) = 0;

        int offsetOf (const MidiMessage& m) const     { return roundToInt (m.getTimeStamp() * sampleRate); }
        void schedule (int64 when, const MidiMessage& m) { if (pending.size() < 4096) pending.push_back ({ when, m }); }
        double samplesPerBeat() const                 { return sampleRate * 60.0 / jmax (20.0, bpm.load()); }

        int64 samplePos = 0;
        std::vector<std::pair<int64, MidiMessage>> pending;
        te::MPESourceID sourceID = te::createUniqueMPESourceID();
        std::atomic<double> bpm { 120.0 };

    private:
        void timerCallback() override { bpm.store (edit.tempoSequence.getTempo (0)->getBpm()); }
    };

    static inline const StringArray rateNames { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    static constexpr double rateBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

    //==============================================================================
    class Arpeggiator : public PulseMidiEffect
    {
    public:
        enum { pRate, pMode, pOctaves, pGate, pVelocity };
        static inline const char* xmlTypeName = "pulseArp";
        static const char* getPluginName() { return "Arpeggiator"; }
        static inline const StringArray modeNames { "Up", "Down", "Up/Down", "As played", "Random", "Chord" };

        explicit Arpeggiator (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            add ("rate", "Rate", steps (rateNames.size()), 3.0f, fmt::choice (rateNames));
            add ("mode", "Mode", steps (modeNames.size()), 0.0f, fmt::choice (modeNames));
            add ("octaves", "Octaves", { 1.0f, 4.0f, 1.0f }, 1.0f, [] (float v) { return String (roundToInt (v)); });
            add ("gate", "Gate", { 0.05f, 1.0f }, 0.6f, fmt::pct);
            add ("velocity", "Velocity", { 0.0f, 127.0f, 1.0f }, 0.0f, [] (float v) { return v < 1 ? String ("As played") : String (roundToInt (v)); });
        }

        String getName() const override  { return "Arpeggiator"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Arpeggiator", { pRate, pMode, pOctaves, pGate, pVelocity } } }; }

    protected:
        void reset() override { held.clear(); stepIndex = 0; nextStep = -1; }

        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            for (auto& m : in)
            {
                if (m.isNoteOn())
                {
                    if (held.empty()) { stepIndex = 0; nextStep = -1; }   // start on the next grid step
                    held.push_back ({ m.getNoteNumber(), m.getVelocity() });
                }
                else if (m.isNoteOff())
                {
                    held.erase (std::remove_if (held.begin(), held.end(), [&m] (auto& h) { return h.first == m.getNoteNumber(); }), held.end());
                }
                else b.emit (m, offsetOf (m));   // CCs, bend... pass through
            }

            if (held.empty()) return;

            const double stepSamples = samplesPerBeat() * rateBeats[jlimit (0, rateNames.size() - 1, getInt (pRate))];
            if (nextStep < 0)
            {
                // follow the song grid while playing, otherwise start right away
                if (b.fc.isPlaying)
                {
                    const double beat = b.fc.editTime.getStart().inSeconds() * bpm.load() / 60.0;
                    const double stepBeats = stepSamples / samplesPerBeat();
                    const double toNext = (std::ceil (beat / stepBeats - 1.0e-9) * stepBeats - beat) * samplesPerBeat();
                    nextStep = b.start + (int64) toNext;
                }
                else nextStep = b.start;
            }

            while (nextStep < b.start + b.numSamples)
            {
                fireStep (b, nextStep, stepSamples);
                nextStep += (int64) std::round (stepSamples);
            }
        }

    private:
        std::vector<int> sequence() const
        {
            std::vector<int> base;
            const int mode = getInt (pMode);
            for (auto& h : held) base.push_back (h.first);
            if (mode != 3) std::sort (base.begin(), base.end());
            std::vector<int> seq;
            for (int o = 0; o < jlimit (1, 4, getInt (pOctaves)); ++o)
                for (auto p : base) if (p + 12 * o <= 127) seq.push_back (p + 12 * o);
            if (mode == 1) std::reverse (seq.begin(), seq.end());
            if (mode == 2 && seq.size() > 2)
                for (int i = (int) seq.size() - 2; i > 0; --i) seq.push_back (seq[(size_t) i]);
            return seq;
        }

        void fireStep (Block& b, int64 when, double stepSamples)
        {
            auto seq = sequence();
            if (seq.empty()) return;
            const int mode = getInt (pMode);
            const int fixedVel = getInt (pVelocity);
            const auto vel = (uint8) jlimit (1, 127, fixedVel > 0 ? fixedVel : (int) held.back().second);
            const int64 off = when + (int64) jmax (1.0, stepSamples * get (pGate));

            std::vector<int> notes;
            if (mode == 5)      notes = seq;                                         // chord: all at once
            else if (mode == 4) notes = { seq[(size_t) random.nextInt ((int) seq.size())] };
            else                notes = { seq[(size_t) (stepIndex % (int) seq.size())] };
            ++stepIndex;

            for (auto p : notes)
            {
                auto on = MidiMessage::noteOn (1, p, vel);
                if (when < b.start + b.numSamples) b.emit (on, (int) (when - b.start)); else schedule (when, on);
                schedule (off, MidiMessage::noteOff (1, p));
            }
        }

        std::vector<std::pair<int, uint8>> held;
        int stepIndex = 0;
        int64 nextStep = -1;
        Random random;
    };

    //==============================================================================
    class ChordFx : public PulseMidiEffect
    {
    public:
        enum { pShift1, pShift2, pShift3, pShift4, pVelocity };
        static inline const char* xmlTypeName = "pulseChord";
        static const char* getPluginName() { return "Chord"; }

        explicit ChordFx (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            auto st = [] (float v) { return roundToInt (v) == 0 ? String ("Off") : (v > 0 ? "+" : "") + String (roundToInt (v)) + " st"; };
            add ("shift1", "Shift 1", { -24.0f, 24.0f, 1.0f }, 4.0f, st);
            add ("shift2", "Shift 2", { -24.0f, 24.0f, 1.0f }, 7.0f, st);
            add ("shift3", "Shift 3", { -24.0f, 24.0f, 1.0f }, 0.0f, st);
            add ("shift4", "Shift 4", { -24.0f, 24.0f, 1.0f }, 0.0f, st);
            add ("velocity", "Velocity", { 0.0f, 1.0f }, 0.8f, fmt::pct);
        }

        String getName() const override  { return "Chord"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Chord", { pShift1, pShift2, pShift3, pShift4, pVelocity } } }; }

    protected:
        void reset() override { for (auto& v : active) v.clear(); }

        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            for (auto& m : in)
            {
                b.emit (m, offsetOf (m));
                if (m.isNoteOn())
                {
                    auto& extra = active[(size_t) m.getNoteNumber()];
                    extra.clear();
                    for (int i = 0; i < 4; ++i)
                        if (const int s = getInt (pShift1 + i); s != 0)
                        {
                            const int p = m.getNoteNumber() + s;
                            if (p < 0 || p > 127 || std::find (extra.begin(), extra.end(), p) != extra.end()) continue;
                            extra.push_back (p);
                            b.emit (MidiMessage::noteOn (m.getChannel(), p, (uint8) jlimit (1, 127, roundToInt (m.getVelocity() * get (pVelocity)))), offsetOf (m));
                        }
                }
                else if (m.isNoteOff())
                {
                    for (auto p : active[(size_t) m.getNoteNumber()])
                        b.emit (MidiMessage::noteOff (m.getChannel(), p), offsetOf (m));
                    active[(size_t) m.getNoteNumber()].clear();
                }
            }
        }

    private:
        std::array<std::vector<int>, 128> active;
    };

    //==============================================================================
    class ScaleFx : public PulseMidiEffect
    {
    public:
        enum { pRoot, pScale, pTranspose };
        static inline const char* xmlTypeName = "pulseScale";
        static const char* getPluginName() { return "Scale"; }

        explicit ScaleFx (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            add ("root", "Root", steps (12), 0.0f, fmt::choice (Scales::rootNames()));
            add ("scale", "Scale", steps ((int) Scales::all().size()), 0.0f, fmt::choice (Scales::names()));
            add ("transpose", "Transpose", { -24.0f, 24.0f, 1.0f }, 0.0f, fmt::st);
        }

        String getName() const override  { return "Scale"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Scale", { pRoot, pScale, pTranspose } } }; }

        /** What an incoming pitch becomes. */
        int map (int pitch) const { return Scales::snap (getInt (pScale), getInt (pRoot), jlimit (0, 127, pitch + getInt (pTranspose))); }

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
    };

    //==============================================================================
    class NoteEcho : public PulseMidiEffect
    {
    public:
        enum { pTime, pRepeats, pFeedback, pPitch };
        static inline const char* xmlTypeName = "pulseNoteEcho";
        static const char* getPluginName() { return "Note Echo"; }

        explicit NoteEcho (te::PluginCreationInfo info) : PulseMidiEffect (info)
        {
            add ("time", "Time", steps (rateNames.size()), 1.0f, fmt::choice (rateNames));
            add ("repeats", "Repeats", { 1.0f, 8.0f, 1.0f }, 3.0f, [] (float v) { return String (roundToInt (v)); });
            add ("feedback", "Feedback", { 0.1f, 1.0f }, 0.6f, fmt::pct);
            add ("pitch", "Pitch", { -12.0f, 12.0f, 1.0f }, 0.0f, fmt::st);
        }

        String getName() const override  { return "Note Echo"; }
        String getPluginType() override  { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Echo", { pTime, pRepeats, pFeedback, pPitch } } }; }

    protected:
        void process (const std::vector<te::MidiMessageWithSource>& in, Block& b) override
        {
            const auto delay = (int64) std::round (samplesPerBeat() * rateBeats[jlimit (0, rateNames.size() - 1, getInt (pTime))]);
            const int repeats = jlimit (1, 8, getInt (pRepeats)), shift = getInt (pPitch);
            const float fb = get (pFeedback);

            for (auto& m : in)
            {
                b.emit (m, offsetOf (m));
                if (! m.isNoteOnOrOff()) continue;

                const int64 when = b.start + offsetOf (m);
                float vel = m.isNoteOn() ? (float) m.getVelocity() : 0.0f;
                for (int r = 1; r <= repeats; ++r)
                {
                    const int p = m.getNoteNumber() + shift * r;
                    if (p < 0 || p > 127) break;
                    if (m.isNoteOn())
                    {
                        vel *= fb;
                        if (vel < 1.0f) break;
                        schedule (when + delay * r, MidiMessage::noteOn (m.getChannel(), p, (uint8) jlimit (1, 127, roundToInt (vel))));
                    }
                    else schedule (when + delay * r, MidiMessage::noteOff (m.getChannel(), p));
                }
            }
        }
    };
}

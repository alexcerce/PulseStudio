#pragma once

#include "TimeFx.h"

/** Beat Repeat (grabs a slice in time with the song and repeats it) and Looper (records a loop and plays it
    round, with overdubbing). */
namespace pulse
{
    class BeatRepeat : public ModEffect
    {
    public:
        enum { pInterval, pGrid, pGate, pChance, pVariation, pDecay, pPitch, pPitchDecay, pMode, pHold };
        static inline const char* xmlTypeName = "pulseBeatRepeat";
        static const char* getPluginName() { return "Beat Repeat"; }
        static inline const StringArray intervalNames { "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        static constexpr double intervalBeats[] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
        static inline const StringArray gridNames { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2" };
        static constexpr double gridBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0 };
        static inline const StringArray gateNames { "1/8", "1/4", "1/2", "1 bar", "2 bars" };
        static constexpr double gateBeats[] = { 0.5, 1.0, 2.0, 4.0, 8.0 };
        static inline const StringArray modeNames { "Mix", "Insert", "Gate" };

        explicit BeatRepeat (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("interval", "Interval", steps (intervalNames.size()), 2.0f, fmt::choice (intervalNames));
            add ("grid", "Grid", steps (gridNames.size()), 2.0f, fmt::choice (gridNames));
            add ("gate", "Gate", steps (gateNames.size()), 2.0f, fmt::choice (gateNames));
            add ("chance", "Chance", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("variation", "Variation", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("decay", "Decay", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("pitch", "Pitch", { -24.0f, 0.0f }, 0.0f, [] (float v) { return String (v, 1) + " st"; });
            add ("pitchdecay", "Pitch Decay", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("mode", "Mode", steps (modeNames.size()), 1.0f, fmt::choice (modeNames));
            add ("hold", "Repeat", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override       { return "Beat Repeat"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        std::vector<Section> getSections() const override
        {
            return { { "When", { pInterval, pChance, pGate, pHold } }, { "Slices", { pGrid, pVariation } }, { "Change", { pDecay, pPitch, pPitchDecay } }, { "Output", { pMode } } };
        }

        // for the display: where in the interval the song is (0..1), whether it is repeating, and which repeat it is on
        std::atomic<float> intervalPhase { 0.0f };
        std::atomic<bool> repeating { false };
        std::atomic<int> repeatNumber { 0 };

    protected:
        void prepare() override
        {
            for (auto& b : slice) b.assign ((size_t) (sampleRate * 3.2) + 16, 0.0f);
            active = false;
            freeBeat = 0.0;
            lastInterval = -1;
            wetAmount = 0.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            const double beatsPerSample = bpm.load() / 60.0 / sampleRate;
            double beat = fc.isPlaying ? fc.editTime.getStart().inSeconds() * bpm.load() / 60.0 : freeBeat;
            const double interval = intervalBeats[jlimit (0, 4, getInt (pInterval))];
            const float chance = get (pChance), variation = get (pVariation), decay = get (pDecay);
            const int mode = getInt (pMode);
            const bool hold = getInt (pHold) > 0;
            const float fadeLen = (float) (0.002 * sampleRate), aWet = std::exp (-1.0f / fadeLen);
            const int maxLen = (int) slice[0].size() - 8;

            for (int i = 0; i < n; ++i)
            {
                // a new interval begins: does it repeat this time?
                const int64 which = (int64) std::floor (beat / interval);
                if (which != lastInterval)
                {
                    lastInterval = which;
                    if (random.nextFloat() < chance || hold) start (beat, beatsPerSample, variation, maxLen);
                }
                if (hold && ! active) start (beat, beatsPerSample, variation, maxLen);

                const float dryL = l[i], dryR = r[i];
                float repL = 0.0f, repR = 0.0f;
                bool firstTime = false;
                if (active)
                {
                    if (captured < sliceLen)
                    {
                        // the slice is heard as it is recorded: it is its own first time round
                        slice[0][(size_t) captured] = dryL;
                        slice[1][(size_t) captured] = dryR;
                        ++captured;
                        repL = dryL; repR = dryR;
                        readPos = 0.0;
                        firstTime = true;
                    }
                    else
                    {
                        if (readPos >= (double) sliceLen)
                        {
                            readPos -= (double) sliceLen;
                            ++repeats;
                            gain *= 1.0f - decay * 0.6f;
                            rate *= rateStep;
                            repeatNumber.store (repeats);
                        }
                        const int a = jlimit (0, sliceLen - 1, (int) readPos), b = jmin (sliceLen - 1, a + 1);
                        const float f = (float) (readPos - (double) a);
                        // a short fade at each end of the slice, so that the join does not click
                        const float edge = jmin (1.0f, ((float) readPos + 1.0f) / fadeLen, ((float) sliceLen - (float) readPos) / fadeLen);
                        repL = (slice[0][(size_t) a] * (1.0f - f) + slice[0][(size_t) b] * f) * gain * edge;
                        repR = (slice[1][(size_t) a] * (1.0f - f) + slice[1][(size_t) b] * f) * gain * edge;
                        readPos += rate;
                    }
                    if (--samplesLeft <= 0 && ! hold) { active = false; }
                }
                const float target = active ? 1.0f : 0.0f;
                wetAmount = target + (wetAmount - target) * aWet;
                if (! active) { repL = dryL; repR = dryR; }   // fading back: the repeats give way to the sound itself
                if (mode == 0)        // Mix: the repeats on top of the sound
                {
                    const float add = active && ! firstTime ? wetAmount : 0.0f;
                    l[i] = dryL + repL * add;
                    r[i] = dryR + repR * add;
                }
                else if (mode == 2)   // Gate: only the repeats, silence between them
                {
                    l[i] = repL * wetAmount;
                    r[i] = repR * wetAmount;
                }
                else                  // Insert: the repeats instead of the sound, the sound itself between them
                {
                    l[i] = dryL + (repL - dryL) * wetAmount;
                    r[i] = dryR + (repR - dryR) * wetAmount;
                }
                beat += beatsPerSample;
            }
            freeBeat = beat;
            intervalPhase.store ((float) (beat / interval - std::floor (beat / interval)));
            repeating.store (active);
        }

    private:
        void start (double, double beatsPerSample, float variation, int maxLen)
        {
            int grid = jlimit (0, 6, getInt (pGrid));
            // Variation: sometimes a neighbouring slice length instead
            if (random.nextFloat() < variation) grid = jlimit (0, 6, grid + (random.nextBool() ? 1 : -1) * (random.nextFloat() < 0.3f ? 2 : 1));
            sliceLen = jlimit (64, maxLen, (int) (gridBeats[grid] / beatsPerSample));
            samplesLeft = (int) (gateBeats[jlimit (0, 4, getInt (pGate))] / beatsPerSample);
            captured = 0;
            readPos = 0.0;
            repeats = 0;
            gain = 1.0f;
            rate = std::pow (2.0, get (pPitch) * (1.0f - get (pPitchDecay)) / 12.0);
            // with Pitch Decay the pitch falls a little further each time round, reaching Pitch after about four repeats
            rateStep = std::pow (2.0, get (pPitch) * get (pPitchDecay) / 48.0);
            active = true;
            repeatNumber.store (0);
        }

        std::array<std::vector<float>, 2> slice;
        bool active = false;
        int sliceLen = 64, captured = 0, samplesLeft = 0, repeats = 0;
        double readPos = 0.0, rate = 1.0, rateStep = 1.0, freeBeat = 0.0;
        float gain = 1.0f, wetAmount = 0.0f;
        int64 lastInterval = -1;
        Random random { 808 };
    };

    //==============================================================================
    class Looper : public ModEffect
    {
    public:
        enum { pState, pLength, pSpeed, pReverse, pFeedback, pMonitor, pLevel };
        enum State { stopped, recording, playing, overdubbing };
        static inline const char* xmlTypeName = "pulseLooper";
        static const char* getPluginName() { return "Looper"; }
        static inline const StringArray stateNames { "Stop", "Record", "Play", "Overdub" };
        static inline const StringArray lengthNames { "Free", "1 bar", "2 bars", "4 bars", "8 bars" };
        static constexpr double lengthBeats[] = { 0.0, 4.0, 8.0, 16.0, 32.0 };
        static constexpr double maxSeconds = 30.0;

        explicit Looper (te::PluginCreationInfo info) : ModEffect (info)
        {
            add ("state", "State", steps (stateNames.size()), 0.0f, fmt::choice (stateNames));
            add ("length", "Length", steps (lengthNames.size()), 0.0f, fmt::choice (lengthNames));
            add ("speed", "Speed", skewed (0.25f, 4.0f, 1.0f), 1.0f, [] (float v) { return "x " + String (v, 2); });
            add ("reverse", "Reverse", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("feedback", "Feedback", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("monitor", "Monitor", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            add ("level", "Loop Level", { -24.0f, 6.0f }, 0.0f, fmt::db);
        }

        String getName() const override       { return "Looper"; }
        String getPluginType() override       { return xmlTypeName; }
        int getDisplayWidth() const override   { return 0; }
        bool canSleepOnSilence() const override { return false; }   // a loop goes on playing with nothing coming in
        std::vector<Section> getSections() const override { return { { "Loop", { pState, pLength } }, { "Playback", { pSpeed, pReverse, pLevel } }, { "Input", { pFeedback, pMonitor } } }; }

        /** Throws the loop away (takes effect at the next block). */
        void clear() { clearWanted.store (true); }

        // for the display
        double loopSeconds() const { return loopLen.load() / sampleRate; }
        float position01() const   { const int len = loopLen.load(); return len > 0 ? (float) (shownPos.load() / (double) len) : 0.0f; }
        bool isRecordingNow() const { return recordingNow.load(); }
        /** The loudest value in slice `bin` of `bins` across the loop (read while it may be changing: for drawing only). */
        float peakAt (int bin, int bins) const
        {
            const int len = loopLen.load();
            if (len <= 0 || loop[0].empty()) return 0.0f;
            const int from = (int) ((int64) len * bin / bins), to = jmin (len, jmax (from + 1, (int) ((int64) len * (bin + 1) / bins)));
            float pk = 0.0f;
            for (int i = from; i < to; i += jmax (1, (to - from) / 64)) pk = jmax (pk, std::abs (loop[0][(size_t) i]), std::abs (loop[1][(size_t) i]));
            return pk;
        }

    protected:
        void prepare() override
        {
            // the loop itself is kept when the audio engine is set up again (a new buffer size, a render)
            const size_t want = (size_t) (sampleRate * maxSeconds) + 16;
            if (loop[0].size() != want)
            {
                for (auto& b : loop) b.assign (want, 0.0f);
                loopLen.store (0);
                pos = 0.0;
            }
            lastState = -1;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const int maxLen = (int) loop[0].size() - 8;
            if (clearWanted.exchange (false)) { loopLen.store (0); pos = 0.0; closed = false; }
            const int state = getInt (pState);
            int len = loopLen.load();
            if (state != lastState)
            {
                if (state == recording) { len = 0; pos = 0.0; closed = false; }           // a new loop
                else if (lastState == recording && ! closed) { closed = true; pos = 0.0; }  // the loop is as long as what was recorded
                if (state == stopped) pos = 0.0;
                lastState = state;
            }
            const int fixedLen = getInt (pLength) > 0 ? jmin (maxLen, (int) (lengthBeats[jlimit (1, 4, getInt (pLength))] * 60.0 / bpm.load() * sampleRate)) : 0;
            const float feedback = get (pFeedback), level = Decibels::decibelsToGain (get (pLevel)), monitor = getInt (pMonitor) > 0 ? 1.0f : 0.0f;
            const double step = (double) get (pSpeed) * (getInt (pReverse) > 0 ? -1.0 : 1.0);
            const float fadeLen = (float) (0.005 * sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const float inL = l[i], inR = r[i];
                float outL = 0.0f, outR = 0.0f;
                if (state == recording && ! closed)
                {
                    if (len < maxLen)
                    {
                        loop[0][(size_t) len] = inL;
                        loop[1][(size_t) len] = inR;
                        ++len;
                    }
                    // a set length (or the end of the memory) closes the loop by itself, and it starts to play
                    if ((fixedLen > 0 && len >= fixedLen) || len >= maxLen) { closed = true; pos = 0.0; }
                }
                else if (len > 0 && state != stopped)
                {
                    const int a = jlimit (0, len - 1, (int) pos);
                    // a short fade either side of the join, so that it does not click
                    const float edge = jmin (1.0f, ((float) pos + 1.0f) / fadeLen, ((float) len - (float) pos) / fadeLen);
                    outL = loop[0][(size_t) a] * edge * level;
                    outR = loop[1][(size_t) a] * edge * level;
                    if (state == overdubbing)
                    {
                        // layer on layer cannot grow without end: what is kept is rounded off towards +/- 1.5
                        loop[0][(size_t) a] = 1.5f * std::tanh ((loop[0][(size_t) a] * feedback + inL) / 1.5f);
                        loop[1][(size_t) a] = 1.5f * std::tanh ((loop[1][(size_t) a] * feedback + inR) / 1.5f);
                    }
                    pos += step;
                    if (pos >= (double) len) pos -= (double) len;
                    if (pos < 0.0) pos += (double) len;
                }
                l[i] = inL * monitor + outL;
                r[i] = inR * monitor + outR;
            }
            loopLen.store (len);
            shownPos.store (state == recording && ! closed ? (double) len : pos);
            recordingNow.store (state == recording && ! closed);
        }

    private:
        std::array<std::vector<float>, 2> loop;
        std::atomic<int> loopLen { 0 };
        std::atomic<double> shownPos { 0.0 };
        std::atomic<bool> clearWanted { false }, recordingNow { false };
        double pos = 0.0;
        int lastState = -1;
        bool closed = false;
    };
}

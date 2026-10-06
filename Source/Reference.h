#pragma once

// Included from PulseSynths.h (needs PulseEffect and LoudnessAnalyser).

/** Reference: a finished song to compare the mix with, one switch away.

    It sits at the end of the master. While it is set to "Reference", what you hear is the song from the file
    instead of the mix: at the same place in time as the project, so that switching compares like with like, and
    (with Match Loudness on) turned up or down to be as loud as the mix, because the louder of two sounds always
    seems the better one. It is never part of an export: when the song is rendered to a file the mix goes through
    untouched, whatever the switch says. */
namespace pulse
{
    class Reference : public PulseEffect, private Timer
    {
    public:
        enum { pListen, pMatch, pTrim, pOffset, pLoop };
        static inline const char* xmlTypeName = "pulseReference";
        static const char* getPluginName() { return "Reference"; }
        static constexpr int shapeSize = 600;
        static constexpr double maxSeconds = 900.0;

        explicit Reference (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("listen", "Reference", steps (2), 0.0f, fmt::choice ({ "Mix", "Reference" }));
            add ("match", "Match Loudness", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            add ("trim", "Trim", { -12.0f, 12.0f }, 0.0f, fmt::db);
            add ("offset", "Start At", skewed (0.0f, 600.0f, 60.0f), 0.0f, [] (float v) { return String (v, 1) + " s"; });
            add ("loop", "Loop", steps (2), 1.0f, fmt::choice ({ "Off", "On" }));
            startTimerHz (4);
        }

        ~Reference() override
        {
            stopTimer();
            if (loader.joinable()) loader.join();
        }

        String getName() const override        { return "Reference"; }
        String getPluginType() override        { return xmlTypeName; }
        int getDisplayWidth() const override    { return 0; }
        bool canSleepOnSilence() const override { return false; }   // (the reference plays while the mix is silent)
        std::vector<Section> getSections() const override
        {
            return { { "Listen", { pListen } }, { "Loudness", { pMatch, pTrim } }, { "Position", { pOffset, pLoop } } };
        }

        //==============================================================================
        /** Chooses the song (undoable). It is read in the background; an empty file takes it away. */
        void setFile (const File& f)
        {
            state.setProperty (fileProperty, f == File() ? String() : f.getFullPathName(), &edit.getUndoManager());
            timerCallback();
        }
        File getFile() const { const auto p = state[fileProperty].toString(); return p.isEmpty() ? File() : File (p); }

        /** Reads the song here and now (the self-test; the device itself reads in the background). */
        bool loadNow (const File& f)
        {
            state.setProperty (fileProperty, f.getFullPathName(), &edit.getUndoManager());
            if (loader.joinable()) loader.join();
            wanted = f.getFullPathName();
            wantedRate = sampleRate;
            auto s = read (engine, f, sampleRate);
            const ScopedLock sl (songLock);
            song = s;
            return song != nullptr;
        }

        bool hasSong() const         { const ScopedLock sl (songLock); return song != nullptr; }
        bool isLoading() const       { return loading.load(); }
        /** The song's own loudness (LUFS), and its length in seconds. */
        double songLufs() const      { const ScopedLock sl (songLock); return song != nullptr ? song->lufs : -200.0; }
        double songSeconds() const   { const ScopedLock sl (songLock); return song != nullptr ? song->audio.getNumSamples() / song->rate : 0.0; }
        /** The outline of the song, for the display (0..1 in each of `shapeSize` slices). */
        void getShape (std::array<float, shapeSize>& into) const
        {
            const ScopedLock sl (songLock);
            if (song != nullptr) into = song->shape; else into.fill (0.0f);
        }

        /** How loud the mix has been since it was last asked to start again (LUFS), and what the reference is being
            turned by to match it (dB). */
        std::atomic<float> mixLufs { -200.0f }, matchDb { 0.0f };
        /** Where in the song it is playing (seconds; under 0 when not playing). */
        std::atomic<double> songPosition { -1.0 };
        /** Starts measuring the mix afresh (after a change to the mix that makes the old measurement stale). */
        void measureAgain() { resetMix = true; }

        /** The gain (dB) that makes a reference at `ref` LUFS as loud as a mix at `mix` LUFS; 0 when either is unknown. */
        static float matchGain (double mix, double ref)
        {
            if (mix < -70.0 || ref < -70.0) return 0.0f;
            return (float) jlimit (-24.0, 24.0, mix - ref);
        }

    protected:
        void prepare() override
        {
            mixLoud.prepare (sampleRate);
            fade = getInt (pListen) > 0 ? 1.0f : 0.0f;
            gainNow = -1.0f;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext& fc) override
        {
            if (fc.isRendering) return;   // never in an export
            if (resetMix.exchange (false)) mixLoud.reset();
            if (fc.isPlaying)
            {
                mixLoud.process (l, r, n);   // the mix itself, whichever of the two is heard
                if (++blocks % 8 == 0) mixLufs = (float) mixLoud.integrated();
            }
            const bool listen = getInt (pListen) > 0;
            const float target = listen ? 1.0f : 0.0f;
            if (target == 0.0f && fade <= 0.0f) { songPosition = -1.0; return; }

            const ScopedTryLock sl (songLock);
            const Song* s = sl.isLocked() ? song.get() : nullptr;
            const float want = Decibels::decibelsToGain (get (pTrim) + (getInt (pMatch) > 0 && s != nullptr ? matchGain (mixLufs.load(), s->lufs) : 0.0f));
            if (gainNow < 0.0f) gainNow = want;
            matchDb = Decibels::gainToDecibels (want) - get (pTrim);
            const float fadeStep = 1.0f / (float) (sampleRate * 0.01), gainCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.3));
            const int len = s != nullptr ? s->audio.getNumSamples() : 0;
            const bool loop = getInt (pLoop) > 0;
            int64 at = (int64) std::llround ((fc.editTime.getStart().inSeconds() + get (pOffset)) * sampleRate);
            const float* sl0 = s != nullptr ? s->audio.getReadPointer (0) : nullptr;
            const float* sr0 = s != nullptr ? s->audio.getReadPointer (1) : nullptr;
            for (int i = 0; i < n; ++i, ++at)
            {
                fade = jlimit (0.0f, 1.0f, fade + (target > fade ? fadeStep : -fadeStep));
                gainNow += (want - gainNow) * gainCoeff;
                float a = 0.0f, b = 0.0f;
                if (fc.isPlaying && len > 0 && at >= 0)
                {
                    const int64 k = loop ? at % len : at;
                    if (k < len) { a = sl0[k] * gainNow; b = sr0[k] * gainNow; }
                }
                l[i] += (a - l[i]) * fade;
                r[i] += (b - r[i]) * fade;
            }
            const int64 start = at - n;
            songPosition = fc.isPlaying && len > 0 && start >= 0 && (loop || start < len) ? (double) (start % len) / sampleRate : -1.0;
        }

    private:
        static inline const Identifier fileProperty { "referenceFile" };

        struct Song
        {
            AudioBuffer<float> audio;   // at the engine's sample rate
            double rate = 48000.0, lufs = -200.0;
            std::array<float, shapeSize> shape {};
        };

        /** Reads a song, brings it to the engine's sample rate, and measures its loudness. */
        static std::shared_ptr<Song> read (te::Engine& e, const File& f, double toRate)
        {
            std::unique_ptr<AudioFormatReader> reader { e.getAudioFileFormatManager().readFormatManager.createReaderFor (f) };
            if (reader == nullptr || reader->lengthInSamples < 64 || reader->sampleRate <= 0 || toRate <= 0) return {};
            const int inLen = (int) jmin (reader->lengthInSamples, (int64) (reader->sampleRate * maxSeconds));
            AudioBuffer<float> in (2, inLen);
            reader->read (&in, 0, inLen, 0, true, true);
            if (reader->numChannels < 2) in.copyFrom (1, 0, in, 0, 0, inLen);
            auto s = std::make_shared<Song>();
            s->rate = toRate;
            if (std::abs (reader->sampleRate - toRate) < 0.5)
                s->audio = std::move (in);
            else
            {
                const double ratio = reader->sampleRate / toRate;
                const int outLen = (int) (inLen / ratio);
                s->audio.setSize (2, outLen);
                for (int ch = 0; ch < 2; ++ch)
                {
                    WindowedSincInterpolator sinc;
                    sinc.process (ratio, in.getReadPointer (ch), s->audio.getWritePointer (ch), outLen, inLen, 0);
                }
            }
            const int len = s->audio.getNumSamples();
            LoudnessAnalyser loud;
            loud.prepare (toRate);
            for (int pos = 0; pos < len; pos += 8192)
                loud.process (s->audio.getReadPointer (0, pos), s->audio.getReadPointer (1, pos), jmin (8192, len - pos));
            s->lufs = loud.integrated();
            for (int i = 0; i < shapeSize; ++i)
            {
                const int from = (int) ((int64) len * i / shapeSize), to = jmax (from + 1, (int) ((int64) len * (i + 1) / shapeSize));
                s->shape[(size_t) i] = jmin (1.0f, s->audio.getMagnitude (from, jmin (len, to) - from));
            }
            return s;
        }

        /** Follows the project (a new file, undo, a new sample rate): the song is read on a thread of its own. */
        void timerCallback() override
        {
            const auto path = state[fileProperty].toString();
            if (path == wanted && (path.isEmpty() || std::abs (wantedRate - sampleRate) < 0.5)) return;
            if (loading.load()) return;   // (one at a time; the next look starts the new one)
            if (loader.joinable()) loader.join();
            wanted = path;
            wantedRate = sampleRate;
            if (path.isEmpty())
            {
                const ScopedLock sl (songLock);
                song = nullptr;
                return;
            }
            loading = true;
            loader = std::thread ([this, path, rate = sampleRate]
            {
                auto s = read (engine, File (path), rate);
                {
                    const ScopedLock sl (songLock);
                    song = s;
                }
                loading = false;
            });
        }

        LoudnessAnalyser mixLoud;
        std::shared_ptr<Song> song;
        CriticalSection songLock;
        std::thread loader;
        std::atomic<bool> loading { false }, resetMix { false };
        String wanted;
        double wantedRate = 0.0;
        float fade = 0.0f, gainNow = -1.0f;
        int blocks = 0;
    };
}

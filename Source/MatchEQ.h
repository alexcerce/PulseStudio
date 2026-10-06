#pragma once

#include "LinearPhase.h"

/** Match EQ: makes one sound's tone follow another's.

    It listens to this track for a while ("learn this track"), and to a reference: the key input (any other track),
    or an audio file. From the difference between the two it makes an EQ curve, smoothed so that it follows the
    shape of the sound and not every note in it, and applies it in linear phase. Amount says how far towards the
    reference to go, Most Change keeps any one frequency from being moved too far, and From / To keep it to a part
    of the range. The loudness is left alone: the curve is centred, so it only changes the tone. */
namespace pulse
{
    /** The average spectrum of a sound: 12 points per octave from 20 Hz to 20 kHz, in dB. */
    class SpectrumLearner
    {
    public:
        static constexpr int numPoints = 121, order = 13, size = 1 << order, hop = size / 2;
        using Points = std::array<float, numPoints>;
        static double hzOf (int point) { return 20.0 * std::pow (2.0, point / 12.0); }

        SpectrumLearner() : power ((size_t) size / 2 + 1, 0.0), ring ((size_t) size, 0.0f), work ((size_t) size * 2, 0.0f), window ((size_t) size)
        {
            for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) size);
        }

        void reset()
        {
            std::fill (power.begin(), power.end(), 0.0);
            std::fill (ring.begin(), ring.end(), 0.0f);
            frames = 0; pos = 0; filled = 0;
        }

        /** Takes in sound (the two sides are added). Silence is not counted. */
        void push (const float* l, const float* r, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) pos] = 0.5f * (l[i] + (r != nullptr ? r[i] : l[i]));
                pos = (pos + 1) & (size - 1);
                if (++filled >= size && (filled - size) % hop == 0) frame();
            }
        }

        int numFrames() const { return frames; }
        double secondsHeard (double sampleRate) const { return frames * (double) hop / sampleRate; }

        /** What has been heard so far. False when that is nothing. */
        bool result (double sampleRate, Points& into) const
        {
            if (frames == 0) return false;
            const double binHz = sampleRate / size;
            const int top = size / 2;
            for (int p = 0; p < numPoints; ++p)
            {
                const double f = hzOf (p), lo = f * std::pow (2.0, -1.0 / 24.0), hi = f * std::pow (2.0, 1.0 / 24.0);
                const int a = (int) std::ceil (lo / binHz), b = (int) std::floor (hi / binHz);
                double v = 0.0;
                if (b >= a && a >= 1 && b <= top)
                {
                    for (int k = a; k <= b; ++k) v += power[(size_t) k];
                    v /= (b - a + 1);
                }
                else
                {
                    // between two points of the transform: a value on the line between them
                    const double at = jlimit (1.0, (double) top - 1.0, f / binHz);
                    const int k = (int) at;
                    v = power[(size_t) k] + (power[(size_t) jmin (top, k + 1)] - power[(size_t) k]) * (at - k);
                }
                into[(size_t) p] = (float) (10.0 * std::log10 (jmax (1.0e-20, v / frames)));
            }
            return true;
        }

    private:
        void frame()
        {
            double energy = 0.0;
            for (int i = 0; i < size; ++i)
            {
                const float x = ring[(size_t) ((pos + i) & (size - 1))];
                energy += x * x;
                work[(size_t) i] = x * window[(size_t) i];
            }
            if (energy / size < 1.0e-8) return;   // under -80 dB: nothing to learn from
            std::fill (work.begin() + size, work.end(), 0.0f);
            fft.performRealOnlyForwardTransform (work.data(), true);
            for (int k = 0; k <= size / 2; ++k)
                power[(size_t) k] += (double) work[(size_t) k * 2] * work[(size_t) k * 2] + (double) work[(size_t) k * 2 + 1] * work[(size_t) k * 2 + 1];
            ++frames;
        }

        dsp::FFT fft { order };
        std::vector<double> power;
        std::vector<float> ring, work, window;
        int frames = 0, pos = 0;
        int64 filled = 0;
    };

    //==============================================================================
    class MatchEQ : public PulseEffect, private Timer
    {
    public:
        enum { pAmount, pSmooth, pRange, pLow, pHigh, pOutput };
        enum Learning { notLearning, learningSource, learningReference };
        using Points = SpectrumLearner::Points;
        static constexpr int numPoints = SpectrumLearner::numPoints;
        static inline const char* xmlTypeName = "pulseMatchEQ";
        static const char* getPluginName() { return "Match EQ"; }

        explicit MatchEQ (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("amount", "Amount", { 0.0f, 1.5f }, 1.0f, fmt::pct);
            add ("smooth", "Smoothing", skewed (0.1f, 2.0f, 0.5f), 0.33f, [] (float v) { return String (v, 2) + " oct"; });
            add ("range", "Most Change", { 3.0f, 24.0f }, 12.0f, fmt::db);
            add ("low", "From", skewed (20.0f, 1000.0f, 100.0f), 20.0f, fmt::hz);
            add ("high", "To", skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, fmt::hz);
            add ("output", "Output", { -24.0f, 24.0f }, 0.0f, fmt::db);
            readState();
            startTimerHz (8);
        }

        ~MatchEQ() override { stopTimer(); }

        String getName() const override        { return "Match EQ"; }
        String getPluginType() override        { return xmlTypeName; }
        int getDisplayWidth() const override    { return 0; }
        bool wantsSidechain() const override    { return true; }
        double getLatencySeconds() override     { return fir.latency() / jmax (1.0, sampleRate); }
        std::vector<Section> getSections() const override
        {
            return { { "Match", { pAmount, pSmooth, pRange } }, { "Range", { pLow, pHigh } }, { "Output", { pOutput } } };
        }

        //==============================================================================
        /** Starts listening: to this track, or to the key input as the reference. Listening again starts afresh. */
        void startLearning (Learning which)
        {
            const ScopedLock sl (learnLock);
            learner.reset();
            learnedSeconds = 0.0f;
            learning = which;
        }

        /** Stops listening and keeps what was heard (undoable). Returns false when nothing was heard. */
        bool stopLearning()
        {
            Points p;
            Learning was;
            {
                const ScopedLock sl (learnLock);
                was = learning.load();
                learning = notLearning;
                if (was == notLearning || ! learner.result (sampleRate, p)) return false;
            }
            store (was == learningReference, p, was == learningReference ? "the key input" : "this track");
            return true;
        }

        Learning isLearning() const { return learning.load(); }
        std::atomic<float> learnedSeconds { 0.0f };
        /** True while the key input is being listened to but nothing is routed into it. */
        bool waitingForKeyInput() const { return learning.load() == learningReference && ! keySeen.load(); }

        /** Learns a sound from an audio file (the first ten minutes of it). */
        bool learnFromFile (const File& f, bool asReference)
        {
            std::unique_ptr<AudioFormatReader> reader { engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f) };
            if (reader == nullptr || reader->lengthInSamples < SpectrumLearner::size || reader->sampleRate <= 0) return false;
            auto heard = std::make_unique<SpectrumLearner>();
            const int chunk = 32768;
            AudioBuffer<float> buf (2, chunk);
            const int64 most = jmin (reader->lengthInSamples, (int64) (reader->sampleRate * 600.0));
            for (int64 at = 0; at < most; at += chunk)
            {
                const int n = (int) jmin ((int64) chunk, most - at);
                reader->read (&buf, 0, n, at, true, true);
                heard->push (buf.getReadPointer (0), reader->numChannels > 1 ? buf.getReadPointer (1) : nullptr, n);
            }
            Points p;
            if (! heard->result (reader->sampleRate, p)) return false;
            store (asReference, p, f.getFileName());
            return true;
        }

        /** Forgets one of the two sounds (undoable). */
        void forget (bool reference)
        {
            state.removeProperty (reference ? refID : srcID, &edit.getUndoManager());
            state.removeProperty (reference ? refNameID : srcNameID, &edit.getUndoManager());
            readState();
        }

        /** Looks at the project again for the two sounds (the device does this itself a few times a second). */
        void refreshFromProject() { readState(); }

        bool hasSource() const     { const ScopedLock sl (specLock); return gotSrc; }
        bool hasReference() const  { const ScopedLock sl (specLock); return gotRef; }
        String sourceName() const    { return state[srcNameID].toString(); }
        String referenceName() const { return state[refNameID].toString(); }

        /** The two sounds as learned (dB; false when not learned yet). */
        bool getSpectrum (bool reference, Points& into) const
        {
            const ScopedLock sl (specLock);
            into = reference ? ref : src;
            return reference ? gotRef : gotSrc;
        }

        /** What the EQ does at each of the points, in dB, with the settings as they are. */
        void getCurve (Points& into) const
        {
            Points a, b;
            const bool ok = getSpectrum (false, a) && getSpectrum (true, b);
            makeCurve (ok, a, b, get (pAmount), get (pSmooth), get (pRange), get (pLow), get (pHigh), into);
        }

        /** The curve from two spectra: the difference, centred (so the loudness stays), smoothed, scaled, kept within
            the most allowed, and faded out outside From and To. */
        static void makeCurve (bool have, const Points& source, const Points& reference, float amount, float smoothOct, float most, float from, float to, Points& out)
        {
            out.fill (0.0f);
            if (! have) return;
            Points d;
            double mean = 0.0; int counted = 0;
            for (int i = 0; i < numPoints; ++i)
            {
                d[(size_t) i] = reference[(size_t) i] - source[(size_t) i];
                const double f = SpectrumLearner::hzOf (i);
                if (f >= 100.0 && f <= 8000.0) { mean += d[(size_t) i]; ++counted; }
            }
            mean /= jmax (1, counted);
            for (auto& v : d) v = jlimit (-40.0f, 40.0f, v - (float) mean);
            // smoothing: twice a running average (a soft-edged window, `smoothOct` wide in all)
            const int half = jmax (0, roundToInt (smoothOct * 12.0f * 0.25f));
            for (int pass = 0; pass < 2 && half > 0; ++pass)
            {
                Points s;
                for (int i = 0; i < numPoints; ++i)
                {
                    float sum = 0.0f;
                    for (int k = -half; k <= half; ++k) sum += d[(size_t) jlimit (0, numPoints - 1, i + k)];
                    s[(size_t) i] = sum / (float) (2 * half + 1);
                }
                d = s;
            }
            for (int i = 0; i < numPoints; ++i)
            {
                const double f = SpectrumLearner::hzOf (i);
                // a third of an octave of fade at each end of the range
                const double inLow = jlimit (0.0, 1.0, std::log2 (f / jmax (1.0f, from)) * 3.0 + 1.0), inHigh = jlimit (0.0, 1.0, std::log2 (jmax (1.0f, to) / f) * 3.0 + 1.0);
                const double edge = (from <= 20.5f ? 1.0 : inLow) * (to >= 19990.0f ? 1.0 : inHigh);
                out[(size_t) i] = jlimit (-most, most, d[(size_t) i] * amount) * (float) edge;
            }
        }

        /** The curve between its points (dB at any frequency). */
        static float curveAt (const Points& c, double hz)
        {
            const double at = jlimit (0.0, (double) numPoints - 1.0, std::log2 (jmax (1.0, hz) / 20.0) * 12.0);
            const int i = jmin (numPoints - 2, (int) at);
            return c[(size_t) i] + (c[(size_t) i + 1] - c[(size_t) i]) * (float) (at - i);
        }

    protected:
        void prepare() override
        {
            fir.prepare (13 + (sampleRate > 60000.0 ? 1 : 0) + (sampleRate > 120000.0 ? 1 : 0));
            shape.assign ((size_t) fir.bins(), 1.0f);
            builtHash = -1.0;
        }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            if (const auto which = learning.load(); which != notLearning)
            {
                const ScopedTryLock sl (learnLock);
                if (sl.isLocked() && learning.load() == which)
                {
                    if (which == learningSource) learner.push (l, r, n);
                    else if (sidechain != nullptr) learner.push (sidechain->getReadPointer (0), sidechain->getReadPointer (1), n);
                    keySeen = sidechain != nullptr;
                    learnedSeconds = (float) learner.secondsHeard (sampleRate);
                }
            }
            fir.process (l, r, n, [this] { design(); });
            const float out = Decibels::decibelsToGain (get (pOutput));
            if (std::abs (out - 1.0f) > 1.0e-4f)
                for (int i = 0; i < n; ++i) { l[i] *= out; r[i] *= out; }
        }

    private:
        static inline const Identifier srcID { "sourceSpectrum" }, refID { "referenceSpectrum" }, srcNameID { "sourceName" }, refNameID { "referenceName" };

        void store (bool reference, const Points& p, const String& name)
        {
            StringArray s;
            for (auto v : p) s.add (String (v, 2));
            state.setProperty (reference ? refID : srcID, s.joinIntoString (" "), &edit.getUndoManager());
            state.setProperty (reference ? refNameID : srcNameID, name, &edit.getUndoManager());
            readState();
        }

        static bool parse (const String& text, Points& into)
        {
            StringArray s;
            s.addTokens (text, " ", {});
            s.removeEmptyStrings();
            if (s.size() != numPoints) return false;
            for (int i = 0; i < numPoints; ++i) into[(size_t) i] = s[i].getFloatValue();
            return true;
        }

        /** The two sounds, from the project (also after undo and redo). */
        void readState()
        {
            const auto a = state[srcID].toString(), b = state[refID].toString();
            if (a == srcText && b == refText) return;
            Points pa {}, pb {};
            const bool okA = parse (a, pa), okB = parse (b, pb);
            const ScopedLock sl (specLock);
            srcText = a; refText = b;
            src = pa; ref = pb; gotSrc = okA; gotRef = okB;
            ++version;
        }

        void timerCallback() override { readState(); }

        /** Works the filter out again when the curve has changed (looked at once per block of the filter). */
        void design()
        {
            const double hash = version.load() * 1000.0 + get (pAmount) * 3.1 + get (pSmooth) * 17.3 + get (pRange) * 0.71 + get (pLow) * 0.013 + get (pHigh) * 0.00091;
            if (hash == builtHash) return;
            Points a, b, c;
            bool have;
            {
                const ScopedTryLock sl (specLock);
                if (! sl.isLocked()) return;   // (the next block will do)
                a = src; b = ref; have = gotSrc && gotRef;
            }
            builtHash = hash;
            makeCurve (have, a, b, get (pAmount), get (pSmooth), get (pRange), get (pLow), get (pHigh), c);
            for (int k = 0; k < fir.bins(); ++k)
                shape[(size_t) k] = Decibels::decibelsToGain (curveAt (c, jlimit (20.0, 20000.0, fir.binHz (k, sampleRate))));
            fir.setResponse (shape.data(), shape.data(), nullptr, nullptr);
        }

        LinearFir fir;
        std::vector<float> shape;
        double builtHash = -1.0;
        SpectrumLearner learner;
        CriticalSection learnLock, specLock;
        std::atomic<Learning> learning { notLearning };
        std::atomic<bool> keySeen { false };
        std::atomic<int> version { 0 };
        Points src {}, ref {};
        bool gotSrc = false, gotRef = false;
        String srcText, refText;
    };
}

#pragma once

#include "Common.h"
#include "ClipImport.h"
#include "PulseSynths.h"

/** Audio → MIDI conversion (like Live's "Convert Drums / Melody / Harmony to New MIDI Track").
    Pure analysis on a mono buffer; the conversion helpers place the result on a new track. */
namespace AudioToMidi
{
    enum class Mode { drums, melody, harmony };

    struct Note
    {
        double start = 0, length = 0;   // seconds from the start of the analysed audio
        int pitch = 60, velocity = 100;
    };

    //==============================================================================
    namespace detail
    {
        inline std::vector<float> magnitudeFrames (const float* x, int n, int fftOrder, int hop, int& numBins)
        {
            const int size = 1 << fftOrder;
            numBins = size / 2;
            dsp::FFT fft (fftOrder);
            std::vector<float> window ((size_t) size), buf ((size_t) size * 2), out;
            for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1));

            for (int start = 0; start + size <= n || (start == 0 && n > 0); start += hop)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < size && start + i < n; ++i) buf[(size_t) i] = x[start + i] * window[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (buf.data());
                out.insert (out.end(), buf.begin(), buf.begin() + numBins);
                if (start + size > n) break;
            }
            return out;
        }

        inline float rms (const float* x, int n)
        {
            double s = 0;
            for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
            return n > 0 ? (float) std::sqrt (s / n) : 0.0f;
        }

        inline int velocityFor (float level, float maxLevel)
        {
            return jlimit (20, 127, roundToInt (40.0f + 87.0f * std::sqrt (level / jmax (1.0e-6f, maxLevel))));
        }
    }

    //==============================================================================
    /** Onsets by spectral flux, each classified as kick (36), snare (38) or hi-hat (42)
        from where its energy sits in the spectrum. */
    inline std::vector<Note> detectDrums (const float* x, int n, double sr)
    {
        using namespace detail;
        const int order = 10, hop = 256, size = 1 << order;
        int bins = 0;
        auto mags = magnitudeFrames (x, n, order, hop, bins);
        const int frames = (int) mags.size() / jmax (1, bins);
        if (frames < 3) return {};

        // spectral flux in two bands, each normalised, so a kick isn't drowned out by cymbals
        const int split = jmax (2, (int) (200.0 / (sr / size)));
        std::vector<float> lowFlux ((size_t) frames, 0.0f), highFlux ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
            for (int b = 1; b < bins; ++b)
            {
                const float prev = f > 0 ? mags[(size_t) (f - 1) * bins + b] : 0.0f;   // silence before the start
                const float d = std::log1p (mags[(size_t) f * bins + b]) - std::log1p (prev);
                if (d > 0) (b < split ? lowFlux : highFlux)[(size_t) f] += d;
            }
        const float maxLow = jmax (1.0e-6f, *std::max_element (lowFlux.begin(), lowFlux.end()));
        const float maxHigh = jmax (1.0e-6f, *std::max_element (highFlux.begin(), highFlux.end()));
        std::vector<float> flux ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
            flux[(size_t) f] = jmax (lowFlux[(size_t) f] / maxLow, highFlux[(size_t) f] / maxHigh);
        const float maxFlux = 1.0f;
        const double binHz = sr / size;
        std::vector<Note> notes;
        int lastOnset = -1000;
        std::vector<float> levels;

        for (int f = 0; f < frames - 1; ++f)
        {
            // adaptive threshold: local mean + a fraction of the global peak
            float mean = 0; int cnt = 0;
            for (int k = jmax (0, f - 8); k <= jmin (frames - 1, f + 8); ++k) { mean += flux[(size_t) k]; ++cnt; }
            mean /= (float) cnt;
            const bool peak = (f == 0 || flux[(size_t) f] > flux[(size_t) (f - 1)]) && flux[(size_t) f] >= flux[(size_t) (f + 1)];
            if (! peak || flux[(size_t) f] < mean * 1.3f + maxFlux * 0.2f || f - lastOnset < 4)   // >= ~46 ms apart
                continue;
            lastOnset = f;

            // energy in bands over the next ~50 ms
            double low = 0, mid = 0, high = 0;
            for (int k = f; k < jmin (frames, f + 4); ++k)
                for (int b = 1; b < bins; ++b)
                {
                    const double hz = b * binHz, e = square ((double) mags[(size_t) k * bins + b]);
                    if (hz < 150) low += e; else if (hz < 4000) mid += e; else high += e;
                }
            const double total = low + mid + high + 1.0e-12;
            int pitch = 38;
            if (low / total > 0.45)       pitch = 36;
            else if (high / total > 0.45) pitch = 42;

            const int startSample = f * hop;
            const float lvl = rms (x + startSample, jmin (size, n - startSample));
            levels.push_back (lvl);
            notes.push_back ({ startSample / sr, 0.1, pitch, 100 });
        }

        const float maxLvl = levels.empty() ? 1.0f : *std::max_element (levels.begin(), levels.end());
        for (size_t i = 0; i < notes.size(); ++i)
            notes[i].velocity = velocityFor (levels[i], maxLvl);
        return notes;
    }

    //==============================================================================
    /** YIN pitch tracking → monophonic notes. */
    inline std::vector<Note> detectMelody (const float* x, int n, double sr)
    {
        using namespace detail;
        const int window = sr > 30000 ? 2048 : 1024, hop = window / 8;
        const int minLag = (int) (sr / 1500.0), maxLag = jmin (window / 2, (int) (sr / 50.0));
        std::vector<float> d ((size_t) maxLag + 1);
        std::vector<int> framePitch;
        std::vector<float> frameLevel;

        float peakLevel = 0;
        for (int start = 0; start + window <= n; start += hop)
            peakLevel = jmax (peakLevel, rms (x + start, window));

        for (int start = 0; start + window <= n; start += hop)
        {
            const float level = rms (x + start, window);
            frameLevel.push_back (level);
            if (level < peakLevel * 0.08f) { framePitch.push_back (-1); continue; }

            // difference function + cumulative mean normalisation
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                double s = 0;
                const float* a = x + start;
                for (int i = 0; i < window / 2; ++i) { const float diff = a[i] - a[i + tau]; s += diff * diff; }
                d[(size_t) tau] = (float) s;
            }
            double running = 0;
            int best = -1;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                running += d[(size_t) tau];
                d[(size_t) tau] = running > 0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
            }
            for (int tau = minLag; tau < maxLag; ++tau)
                if (d[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }

            if (best < 0) { framePitch.push_back (-1); continue; }

            // parabolic refinement
            double tau = best;
            if (best > 1 && best < maxLag)
            {
                const double s0 = d[(size_t) best - 1], s1 = d[(size_t) best], s2 = d[(size_t) best + 1];
                const double den = 2 * (2 * s1 - s2 - s0);
                if (std::abs (den) > 1.0e-9) tau += (s2 - s0) / den;
            }
            const double hz = sr / tau;
            framePitch.push_back (roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0)));
        }

        // median-smooth the pitch track
        std::vector<int> smooth (framePitch.size(), -1);
        for (size_t i = 0; i < framePitch.size(); ++i)
        {
            std::vector<int> win;
            for (int k = -2; k <= 2; ++k)
                if (auto j = (int) i + k; j >= 0 && j < (int) framePitch.size()) win.push_back (framePitch[(size_t) j]);
            std::sort (win.begin(), win.end());
            smooth[i] = win[win.size() / 2];
        }

        // runs of the same pitch become notes; a jump in level re-triggers
        std::vector<Note> notes;
        const double frameSec = hop / sr, minLen = 0.06;
        size_t i = 0;
        while (i < smooth.size())
        {
            if (smooth[i] < 0) { ++i; continue; }
            size_t j = i + 1;
            while (j < smooth.size() && smooth[j] == smooth[i]
                   && ! (frameLevel[j] > frameLevel[j - 1] * 1.8f && frameLevel[j - 1] < peakLevel * 0.3f))
                ++j;
            const double len = (double) (j - i) * frameSec;
            if (len >= minLen)
            {
                float lvl = 0;
                for (size_t k = i; k < j; ++k) lvl = jmax (lvl, frameLevel[k]);
                notes.push_back ({ (double) i * frameSec, len, jlimit (0, 127, smooth[i]), velocityFor (lvl, peakLevel) });
            }
            i = j;
        }
        return notes;
    }

    //==============================================================================
    /** Polyphonic: harmonic salience per MIDI note (36-96) from a long FFT, with simple
        harmonic suppression, tracked into notes. */
    inline std::vector<Note> detectHarmony (const float* x, int n, double sr)
    {
        using namespace detail;
        const int order = 13, hop = 1024, size = 1 << order;
        int bins = 0;
        auto mags = magnitudeFrames (x, n, order, hop, bins);
        const int frames = (int) mags.size() / jmax (1, bins);
        if (frames < 1) return {};

        const int lo = 36, hi = 96, count = hi - lo + 1;
        const double binHz = sr / size;
        std::vector<std::vector<bool>> active ((size_t) frames, std::vector<bool> ((size_t) count, false));
        std::vector<float> frameEnergy ((size_t) frames, 0.0f);

        auto magAt = [&] (int f, double hz)
        {
            const int b = roundToInt (hz / binHz);
            float m = 0;
            for (int k = b - 1; k <= b + 1; ++k)
                if (k > 0 && k < bins) m = jmax (m, mags[(size_t) f * bins + k]);
            return m;
        };

        float globalPeak = 0;
        for (int f = 0; f < frames; ++f)
            for (int b = 0; b < bins; ++b) globalPeak = jmax (globalPeak, mags[(size_t) f * bins + b]);

        for (int f = 0; f < frames; ++f)
        {
            std::vector<float> sal ((size_t) count, 0.0f);
            for (int p = lo; p <= hi; ++p)
            {
                const double f0 = 440.0 * std::pow (2.0, (p - 69) / 12.0);
                float s = 0;
                for (int h = 1; h <= 4; ++h)
                    if (f0 * h < sr * 0.45) s += magAt (f, f0 * h) / (float) h;
                // a note needs its own fundamental, not just shared harmonics
                sal[(size_t) (p - lo)] = magAt (f, f0) > 0.1f * globalPeak ? s : 0.0f;
            }

            const float maxSal = *std::max_element (sal.begin(), sal.end());
            frameEnergy[(size_t) f] = maxSal;
            if (maxSal <= 0) continue;

            std::vector<int> order2 ((size_t) count);
            std::iota (order2.begin(), order2.end(), 0);
            std::sort (order2.begin(), order2.end(), [&] (int a, int b) { return sal[(size_t) a] > sal[(size_t) b]; });

            int picked = 0;
            for (int idx : order2)
            {
                if (picked >= 6 || sal[(size_t) idx] < maxSal * 0.35f) break;
                const int p = idx + lo;
                // skip octaves / twelfths of louder notes we already have (they're harmonics)
                bool harmonic = false;
                for (int q = lo; q < p; ++q)
                    if (active[(size_t) f][(size_t) (q - lo)] && (p - q == 12 || p - q == 19 || p - q == 24))
                        harmonic = harmonic || sal[(size_t) idx] < sal[(size_t) (q - lo)] * 0.9f;
                if (harmonic) continue;
                // neighbouring semitones are leakage
                if ((idx > 0 && active[(size_t) f][(size_t) idx - 1]) || (idx + 1 < count && active[(size_t) f][(size_t) idx + 1])) continue;
                active[(size_t) f][(size_t) idx] = true;
                ++picked;
            }
        }

        std::vector<Note> notes;
        const double frameSec = hop / sr;
        const float peakEnergy = *std::max_element (frameEnergy.begin(), frameEnergy.end());
        for (int p = 0; p < count; ++p)
        {
            int f = 0;
            while (f < frames)
            {
                if (! active[(size_t) f][(size_t) p]) { ++f; continue; }
                int g = f + 1;
                while (g < frames && active[(size_t) g][(size_t) p]) ++g;
                if (g - f >= 2)
                    notes.push_back ({ f * frameSec, (g - f) * frameSec, p + lo, velocityFor (frameEnergy[(size_t) f], peakEnergy) });
                f = g;
            }
        }
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        return notes;
    }

    inline std::vector<Note> detect (Mode m, const float* x, int n, double sr)
    {
        switch (m)
        {
            case Mode::drums:   return detectDrums (x, n, sr);
            case Mode::melody:
            {
                // pitch tracking doesn't need more than ~11 kHz: decimate (with a box filter) for speed
                const int factor = jmax (1, (int) (sr / 11025.0));
                if (factor == 1) return detectMelody (x, n, sr);
                std::vector<float> d ((size_t) (n / factor));
                for (size_t i = 0; i < d.size(); ++i)
                {
                    float s = 0;
                    for (int k = 0; k < factor; ++k) s += x[i * (size_t) factor + (size_t) k];
                    d[i] = s / (float) factor;
                }
                return detectMelody (d.data(), (int) d.size(), sr / factor);
            }
            default:            return detectHarmony (x, n, sr);
        }
    }

    //==============================================================================
    /** Reads the part of an audio clip that plays (mono, at the file's rate). */
    inline bool readClipAudio (te::WaveAudioClip& clip, AudioBuffer<float>& mono, double& sr)
    {
        auto f = clip.getOriginalFile();
        std::unique_ptr<AudioFormatReader> r (clip.edit.engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (r == nullptr) return false;

        sr = r->sampleRate;
        auto pos = clip.getPosition();
        const auto start = (int64) (pos.getOffset().inSeconds() * clip.getSpeedRatio() * sr);
        const auto len = (int) jmin<int64> (r->lengthInSamples - start, (int64) (pos.getLength().inSeconds() * clip.getSpeedRatio() * sr));
        if (len <= 0) return false;

        AudioBuffer<float> st ((int) jmax (1u, r->numChannels), len);
        r->read (&st, 0, len, start, true, true);
        mono.setSize (1, len);
        mono.copyFrom (0, 0, st, 0, 0, len);
        if (st.getNumChannels() > 1)
        {
            mono.addFrom (0, 0, st, 1, 0, len);
            mono.applyGain (0.5f);
        }
        return true;
    }

    /** Converts an audio clip into a MIDI clip on a new track below the clip's track,
        with an instrument that suits the mode. Returns the new track (or nullptr). */
    inline te::AudioTrack* convertClip (te::WaveAudioClip& clip, Mode mode, int* numNotesOut = nullptr)
    {
        AudioBuffer<float> mono;
        double sr = 44100.0;
        if (! readClipAudio (clip, mono, sr))
            return nullptr;

        auto notes = detect (mode, mono.getReadPointer (0), mono.getNumSamples(), sr / clip.getSpeedRatio());
        if (numNotesOut != nullptr) *numNotesOut = (int) notes.size();

        auto& edit = clip.edit;
        const char* names[] = { "Drums", "Melody", "Harmony" };
        auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (*clip.getTrack(), false), nullptr);
        if (t == nullptr) return nullptr;
        t->setName (clip.getName() + " " + names[(int) mode]);
        t->setColour (Theme::trackColour (EngineHelpers::getUserTracks (edit).indexOf (t.get())));

        te::Plugin::Ptr inst = mode == Mode::drums ? DrumRack::create (edit)
                                                   : edit.getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {});
        if (inst != nullptr) t->pluginList.insertPlugin (inst, 0, nullptr);

        auto range = clip.getEditTimeRange();
        auto mc = t->insertMIDIClip (clip.getName() + " MIDI", range, nullptr);
        if (mc == nullptr) return t.get();

        const double clipStartBeat = EngineHelpers::timeToBeat (edit, range.getStart());
        for (auto& nt : notes)
        {
            const auto t0 = range.getStart() + te::TimeDuration::fromSeconds (nt.start);
            const auto t1 = t0 + te::TimeDuration::fromSeconds (jmax (0.03, nt.length));
            const double b0 = EngineHelpers::timeToBeat (edit, t0) - clipStartBeat;
            const double b1 = EngineHelpers::timeToBeat (edit, t1) - clipStartBeat;
            mc->getSequence().addNote (nt.pitch, te::BeatPosition::fromBeats (b0), te::BeatDuration::fromBeats (jmax (0.05, b1 - b0)),
                                       nt.velocity, 0, nullptr);
        }
        return t.get();
    }
}

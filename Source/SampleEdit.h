#pragma once

#include "Common.h"
#include "ClipImport.h"
#include "EditingOps.h"
#include "AudioToMidi.h"

/** Destructive audio editing on a copy of a clip's file (the original is never touched),
    transient slicing to a Drum Rack, and per-clip warp modes. */
namespace SampleEdit
{
    //==============================================================================
    // Range operations on a buffer (sample ranges are clamped)
    inline Range<int> clampRange (const AudioBuffer<float>& b, Range<int> r)
    {
        return r.getIntersectionWith ({ 0, b.getNumSamples() });
    }

    inline void silence (AudioBuffer<float>& b, Range<int> r)
    {
        r = clampRange (b, r);
        b.clear (r.getStart(), r.getLength());
    }

    /** Removes the range, closing the gap. */
    inline void cut (AudioBuffer<float>& b, Range<int> r)
    {
        r = clampRange (b, r);
        if (r.isEmpty()) return;
        AudioBuffer<float> out (b.getNumChannels(), b.getNumSamples() - r.getLength());
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            out.copyFrom (ch, 0, b, ch, 0, r.getStart());
            out.copyFrom (ch, r.getStart(), b, ch, r.getEnd(), b.getNumSamples() - r.getEnd());
        }
        b = std::move (out);
    }

    /** Scales the range so its peak is at `targetDb`. */
    inline void normalise (AudioBuffer<float>& b, Range<int> r, float targetDb = -0.3f)
    {
        r = clampRange (b, r);
        if (r.isEmpty()) return;
        const float peak = b.getMagnitude (r.getStart(), r.getLength());
        if (peak > 1.0e-6f)
            b.applyGain (r.getStart(), r.getLength(), Decibels::decibelsToGain (targetDb) / peak);
    }

    inline void fade (AudioBuffer<float>& b, Range<int> r, bool in)
    {
        r = clampRange (b, r);
        if (r.isEmpty()) return;
        b.applyGainRamp (r.getStart(), r.getLength(), in ? 0.0f : 1.0f, in ? 1.0f : 0.0f);
    }

    inline void reverse (AudioBuffer<float>& b, Range<int> r)
    {
        r = clampRange (b, r);
        if (r.isEmpty()) return;
        b.reverse (r.getStart(), r.getLength());
    }

    inline void gain (AudioBuffer<float>& b, Range<int> r, float db)
    {
        r = clampRange (b, r);
        if (! r.isEmpty()) b.applyGain (r.getStart(), r.getLength(), Decibels::decibelsToGain (db));
    }

    //==============================================================================
    // Files
    inline bool readFile (te::Engine& e, const File& f, AudioBuffer<float>& out, double& sampleRate)
    {
        std::unique_ptr<AudioFormatReader> r (e.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) return false;
        out.setSize ((int) jlimit (1u, 2u, r->numChannels), (int) r->lengthInSamples);
        r->read (&out, 0, out.getNumSamples(), 0, true, out.getNumChannels() > 1);
        sampleRate = r->sampleRate;
        return true;
    }

    inline bool writeWav (const File& f, const AudioBuffer<float>& b, double sampleRate)
    {
        f.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<FileOutputStream> out (f.createOutputStream());
        if (out == nullptr) return false;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (out.get(), sampleRate, (unsigned int) b.getNumChannels(), 24, {}, 0));
        if (w == nullptr) return false;
        out.release();
        return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    inline File editsFolder (te::Edit& e)
    {
        auto d = EditingOps::bounceFolder (e).getParentDirectory().getChildFile ("Edited Audio");
        d.createDirectory();
        return d;
    }

    /** Writes the edited audio as a new file and points the clip at it (undoable). */
    inline bool applyToClip (te::WaveAudioClip& clip, const AudioBuffer<float>& audio, double sampleRate)
    {
        auto& e = clip.edit;
        auto f = editsFolder (e).getNonexistentChildFile (File::createLegalFileName (clip.getName()) + " edit", ".wav", false);
        if (! writeWav (f, audio, sampleRate)) return false;

        e.getUndoManager().beginNewTransaction ("Edit audio");
        clip.getSourceFileReference().setToFile (f, te::SourceFileReference::PathStyle::chooseBest, false);
        const auto newLen = te::TimeDuration::fromSeconds (audio.getNumSamples() / sampleRate);
        auto pos = clip.getPosition();
        if (pos.getOffset() + pos.getLength() > newLen)   // shortened by a cut
            clip.setLength (jmax (te::TimeDuration::fromSeconds (0.01), newLen - pos.getOffset()), true);
        return true;
    }

    //==============================================================================
    // Slicing
    /** Onset positions (seconds) of the audio: transients, at least `minGap` apart. */
    inline std::vector<double> transientTimes (const AudioBuffer<float>& audio, double sr, int maxSlices = 128)
    {
        AudioBuffer<float> mono (1, audio.getNumSamples());
        mono.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
        if (audio.getNumChannels() > 1) { mono.addFrom (0, 0, audio, 1, 0, audio.getNumSamples()); mono.applyGain (0.5f); }
        std::vector<double> t;
        for (auto& n : AudioToMidi::detectDrums (mono.getReadPointer (0), mono.getNumSamples(), sr))
            t.push_back (n.start);
        if (t.empty() || t.front() > 0.02) t.insert (t.begin(), 0.0);
        if ((int) t.size() > maxSlices) t.resize ((size_t) maxSlices);
        return t;
    }

    struct SliceResult { te::AudioTrack* track = nullptr; int slices = 0; };

    /** Slices an audio clip at its transients onto a new Drum Rack track (one slice per pad,
        up to 64, from C1 up) with a MIDI clip that plays the slices back in their original rhythm. */
    inline SliceResult sliceToDrumRack (te::WaveAudioClip& clip, int maxSlices = 64)
    {
        maxSlices = jlimit (1, DrumRack::numPads - DrumRack::baseNote, maxSlices);
        SliceResult r;
        auto& e = clip.edit;
        AudioBuffer<float> full;
        double sr = 44100.0;
        if (! readFile (e.engine, clip.getOriginalFile(), full, sr)) return r;

        // the part of the file the clip plays
        auto pos = clip.getPosition();
        const int start = jlimit (0, full.getNumSamples(), (int) (pos.getOffset().inSeconds() * sr));
        const int len = jlimit (0, full.getNumSamples() - start, (int) (pos.getLength().inSeconds() * sr));
        if (len <= 0) return r;
        AudioBuffer<float> audio (full.getNumChannels(), len);
        for (int ch = 0; ch < full.getNumChannels(); ++ch) audio.copyFrom (ch, 0, full, ch, start, len);

        auto times = transientTimes (audio, sr, maxSlices);
        auto folder = editsFolder (e).getChildFile (File::createLegalFileName (clip.getName()) + " slices");
        folder.createDirectory();

        auto t = e.insertNewAudioTrack (te::TrackInsertPoint (*clip.getTrack(), false), nullptr);
        if (t == nullptr) return r;
        t->setName (clip.getName() + " slices");
        t->setColour (clip.getTrack()->getColour());
        auto rackPlugin = DrumRack::create (e);
        t->pluginList.insertPlugin (rackPlugin, 0, nullptr);
        auto rack = DrumRack::rackOf (rackPlugin.get());

        auto mc = t->insertMIDIClip (clip.getName() + " slices", clip.getEditTimeRange(), nullptr);
        const double clipStartBeat = EngineHelpers::timeToBeat (e, clip.getEditTimeRange().getStart());

        for (size_t i = 0; i < times.size(); ++i)
        {
            const int s0 = (int) (times[i] * sr);
            const int s1 = i + 1 < times.size() ? (int) (times[i + 1] * sr) : len;
            if (s1 - s0 < 64) continue;
            AudioBuffer<float> slice (audio.getNumChannels(), s1 - s0);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch) slice.copyFrom (ch, 0, audio, ch, s0, s1 - s0);
            fade (slice, { slice.getNumSamples() - jmin (64, slice.getNumSamples()), slice.getNumSamples() }, false);   // no clicks
            auto f = folder.getChildFile ("slice " + String (r.slices + 1).paddedLeft ('0', 2) + ".wav");
            if (! writeWav (f, slice, sr) || rack == nullptr) continue;
            DrumRack::loadSample (*rack, DrumRack::baseNote + r.slices, f);

            if (mc != nullptr)
            {
                const auto t0 = clip.getEditTimeRange().getStart() + te::TimeDuration::fromSeconds (times[i]);
                const auto t1 = clip.getEditTimeRange().getStart() + te::TimeDuration::fromSeconds ((double) s1 / sr);
                const double b0 = EngineHelpers::timeToBeat (e, t0) - clipStartBeat, b1 = EngineHelpers::timeToBeat (e, t1) - clipStartBeat;
                mc->getSequence().addNote (DrumRack::baseNote + r.slices, te::BeatPosition::fromBeats (b0),
                                           te::BeatDuration::fromBeats (jmax (0.05, b1 - b0)), 100, 0, nullptr);
            }
            ++r.slices;
        }
        r.track = t.get();
        return r;
    }

    //==============================================================================
    // Warp (time-stretch) modes
    inline StringArray warpModes (te::Engine& e) { return te::TimeStretcher::getPossibleModes (e, true); }

    inline String currentWarpMode (te::AudioClipBase& c) { return te::TimeStretcher::getNameOfMode (c.getTimeStretchMode()); }

    inline void setWarpMode (te::AudioClipBase& c, const String& name)
    {
        c.setTimeStretchMode (te::TimeStretcher::getModeFromName (c.edit.engine, name));
    }
}

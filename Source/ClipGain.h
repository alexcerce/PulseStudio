#pragma once

#include "Common.h"
#include "SampleEdit.h"

/** Gain envelope for audio clips. Points (time in the source file, dB) are stored in the clip;
    the envelope is applied to a rendered copy of the original file (the original is never
    changed, and removing the envelope plays the original again). */
namespace ClipGain
{
    inline const Identifier envID { "PULSEGAINENV" }, pointID { "POINT" }, sourceID { "pulseGainSource" };

    struct Point { double t; float db; };

    inline std::vector<Point> points (te::Clip& c)
    {
        std::vector<Point> p;
        for (auto v : c.state.getChildWithName (envID)) p.push_back ({ (double) v["t"], (float) v["db"] });
        std::sort (p.begin(), p.end(), [] (auto& a, auto& b) { return a.t < b.t; });
        return p;
    }

    inline void setPoints (te::Clip& c, const std::vector<Point>& pts, UndoManager* um)
    {
        auto env = c.state.getOrCreateChildWithName (envID, um);
        env.removeAllChildren (um);
        for (auto& p : pts)
        {
            ValueTree v (pointID);
            v.setProperty ("t", p.t, nullptr);
            v.setProperty ("db", p.db, nullptr);
            env.appendChild (v, um);
        }
    }

    /** Envelope value (dB) at a source time: linear between points, flat outside. */
    inline float dbAt (const std::vector<Point>& p, double t)
    {
        if (p.empty()) return 0.0f;
        if (t <= p.front().t) return p.front().db;
        if (t >= p.back().t) return p.back().db;
        for (size_t i = 1; i < p.size(); ++i)
            if (t <= p[i].t)
            {
                const double f = (t - p[i - 1].t) / jmax (1.0e-9, p[i].t - p[i - 1].t);
                return (float) (p[i - 1].db + (p[i].db - p[i - 1].db) * f);
            }
        return p.back().db;
    }

    /** The untouched file the envelope is applied to. */
    inline File sourceFile (te::WaveAudioClip& c)
    {
        auto s = c.state[sourceID].toString();
        return s.isNotEmpty() && File (s).existsAsFile() ? File (s) : c.getOriginalFile();
    }

    /** Renders the envelope onto a copy of the source and points the clip at it (or back at the
        source when there are no points). */
    inline bool apply (te::WaveAudioClip& c)
    {
        auto& e = c.edit;
        const auto src = sourceFile (c);
        const auto pts = points (c);
        bool flat = true;
        for (auto& p : pts) flat = flat && std::abs (p.db) < 0.01f;
        if (pts.empty() || flat)
        {
            if (c.state.hasProperty (sourceID))
            {
                c.getSourceFileReference().setToFile (src, te::SourceFileReference::PathStyle::chooseBest, false);
                c.state.removeProperty (sourceID, nullptr);
            }
            return true;
        }
        AudioBuffer<float> audio;
        double sr = 44100.0;
        if (! SampleEdit::readFile (e.engine, src, audio, sr)) return false;
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const float g = Decibels::decibelsToGain (dbAt (pts, i / sr), -100.0f);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.setSample (ch, i, audio.getSample (ch, i) * g);
        }
        auto out = SampleEdit::editsFolder (e).getChildFile (File::createLegalFileName (c.getName()) + " gain " + c.itemID.toString() + ".wav");
        if (! SampleEdit::writeWav (out, audio, sr)) return false;
        c.state.setProperty (sourceID, src.getFullPathName(), nullptr);
        c.getSourceFileReference().setToFile (out, te::SourceFileReference::PathStyle::chooseBest, false);
        return true;
    }
}

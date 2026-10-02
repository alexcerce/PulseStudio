#pragma once

#include "Common.h"

/** Song-structure helpers: markers (locators), tempo and time-signature changes,
    bar maths that follows time signatures, auto-crossfades and take comping. */
namespace Timeline
{
    //==============================================================================
    // Bars (time-signature aware)
    inline double barStartBeat (te::Edit& e, int bar)
    {
        return e.tempoSequence.toBeats (te::tempo::BarsAndBeats { jmax (0, bar), {} }).inBeats();
    }

    inline int barAtBeat (te::Edit& e, double beat)
    {
        return e.tempoSequence.toBarsAndBeats (EngineHelpers::beatToTime (e, jmax (0.0, beat))).bars;
    }

    /** Calls fn (barIndex, startBeat) for each bar starting in [b0, b1]. */
    template <typename Fn>
    void forEachBar (te::Edit& e, double b0, double b1, int barStep, Fn&& fn)
    {
        int bar = jmax (0, barAtBeat (e, b0));
        bar = bar / jmax (1, barStep) * jmax (1, barStep);
        for (int guard = 0; guard < 10000; ++guard, bar += jmax (1, barStep))
        {
            const double b = barStartBeat (e, bar);
            if (b > b1) break;
            fn (bar, b);
        }
    }

    //==============================================================================
    // Markers (Tracktion marker clips on the marker track)
    inline juce::ReferenceCountedArray<te::MarkerClip> markers (te::Edit& e)
    {
        auto list = e.getMarkerManager().getMarkers();
        std::sort (list.begin(), list.end(), [] (auto* a, auto* b) { return a->getPosition().getStart() < b->getPosition().getStart(); });
        return list;
    }

    inline te::MarkerClip* addMarker (te::Edit& e, te::TimePosition t, const String& name = {})
    {
        auto& mm = e.getMarkerManager();
        auto m = mm.createMarker (-1, t, te::TimeDuration::fromSeconds (EngineHelpers::beatToTime (e, 4.0).inSeconds()), nullptr);
        if (m == nullptr) return nullptr;
        m->setName (name.isNotEmpty() ? name : "Marker " + String (markers (e).size()));
        return dynamic_cast<te::MarkerClip*> (m.get());
    }

    inline void jumpToMarker (te::Edit& e, bool forward)
    {
        auto& tc = e.getTransport();
        const auto now = tc.getPosition();
        te::MarkerClip* target = nullptr;
        for (auto m : markers (e))
        {
            const auto s = m->getPosition().getStart();
            if (forward && s > now + te::TimeDuration::fromSeconds (0.001)) { target = m; break; }
            if (! forward && s < now - te::TimeDuration::fromSeconds (0.001)) target = m;
        }
        if (target != nullptr) tc.setPosition (target->getPosition().getStart());
        else if (! forward)     tc.setPosition (te::TimePosition());
    }

    //==============================================================================
    // Tempo and time signature changes
    inline te::TempoSetting* addTempo (te::Edit& e, double beat, double bpm)
    {
        auto t = e.tempoSequence.insertTempo (te::BeatPosition::fromBeats (jmax (0.0, beat)), jlimit (20.0, 300.0, bpm), 0.0f);
        return t.get();
    }

    inline bool parseTimeSig (const String& text, int& num, int& den)
    {
        auto parts = StringArray::fromTokens (text, "/", {});
        if (parts.size() != 2) return false;
        num = parts[0].trim().getIntValue();
        den = parts[1].trim().getIntValue();
        return num >= 1 && num <= 32 && (den == 1 || den == 2 || den == 4 || den == 8 || den == 16);
    }

    /** Inserts a time signature at the bar containing `beat`. */
    inline te::TimeSigSetting* addTimeSig (te::Edit& e, double beat, int num, int den)
    {
        const double barBeat = barStartBeat (e, barAtBeat (e, beat));
        auto ts = e.tempoSequence.insertTimeSig (te::BeatPosition::fromBeats (barBeat));
        if (ts == nullptr) return nullptr;
        ts->numerator = num;
        ts->denominator = den;
        return ts.get();
    }

    //==============================================================================
    // Auto-crossfades: overlapping audio clips fade into each other
    static inline const Identifier autoFadeInID { "pulseAutoFadeIn" }, autoFadeOutID { "pulseAutoFadeOut" };

    inline bool autoCrossfadesEnabled (te::Edit& e) { return e.state.getProperty ("pulseAutoCrossfade", true); }
    inline void setAutoCrossfades (te::Edit& e, bool on) { e.state.setProperty ("pulseAutoCrossfade", on, nullptr); }

    inline void updateCrossfades (te::AudioTrack& t)
    {
        if (! autoCrossfadesEnabled (t.edit)) return;

        juce::Array<te::AudioClipBase*> clips;
        for (auto c : t.getClips())
            if (auto ac = dynamic_cast<te::AudioClipBase*> (c)) clips.add (ac);
        std::sort (clips.begin(), clips.end(), [] (auto* a, auto* b) { return a->getPosition().getStart() < b->getPosition().getStart(); });

        std::vector<double> fadeIn ((size_t) clips.size(), -1.0), fadeOut ((size_t) clips.size(), -1.0);
        for (int i = 0; i + 1 < clips.size(); ++i)
        {
            auto a = clips[i]->getEditTimeRange(), b = clips[i + 1]->getEditTimeRange();
            const double overlap = (a.getEnd() - b.getStart()).inSeconds();
            if (overlap > 0.001 && b.getEnd() > a.getEnd())
            {
                fadeOut[(size_t) i] = overlap;
                fadeIn[(size_t) i + 1] = overlap;
            }
        }

        for (int i = 0; i < clips.size(); ++i)
        {
            auto* c = clips[i];
            if (fadeIn[(size_t) i] > 0)
            {
                c->setFadeIn (te::TimeDuration::fromSeconds (fadeIn[(size_t) i]));
                c->state.setProperty (autoFadeInID, true, nullptr);
            }
            else if (c->state.getProperty (autoFadeInID, false))   // the overlap went away: undo our fade
            {
                c->setFadeIn ({});
                c->state.removeProperty (autoFadeInID, nullptr);
            }

            if (fadeOut[(size_t) i] > 0)
            {
                c->setFadeOut (te::TimeDuration::fromSeconds (fadeOut[(size_t) i]));
                c->state.setProperty (autoFadeOutID, true, nullptr);
            }
            else if (c->state.getProperty (autoFadeOutID, false))
            {
                c->setFadeOut ({});
                c->state.removeProperty (autoFadeOutID, nullptr);
            }
        }
    }

    //==============================================================================
    // Comping: choose a take for part of a clip (the clip is split around the range)
    // MIDI clips use the engine's takes (from loop recording). Audio takes in the engine need
    // Tracktion "projects", so audio clips keep their own list of take files (PULSETAKES).
    static inline const Identifier pulseTakesID { "PULSETAKES" }, pulseTakeID { "pulseTake" };

    inline int numTakes (te::Clip& c)
    {
        if (dynamic_cast<te::WaveAudioClip*> (&c) != nullptr)
            return c.state.getChildWithName (pulseTakesID).getNumChildren();
        return c.hasAnyTakes() ? c.getTakeDescriptions().size() : 0;
    }

    inline int currentTake (te::Clip& c)
    {
        if (dynamic_cast<te::WaveAudioClip*> (&c) != nullptr)
            return (int) c.state.getProperty (pulseTakeID, 0);
        return c.getCurrentTake();
    }

    inline void setTake (te::Clip& c, int take)
    {
        if (auto w = dynamic_cast<te::WaveAudioClip*> (&c))
        {
            auto takes = c.state.getChildWithName (pulseTakesID);
            if (! isPositiveAndBelow (take, takes.getNumChildren())) return;
            w->getSourceFileReference().setToFile (File (takes.getChild (take)["file"].toString()),
                                                   te::SourceFileReference::PathStyle::chooseBest, false);
            c.state.setProperty (pulseTakeID, take, &c.edit.getUndoManager());
            return;
        }
        if (isPositiveAndBelow (take, numTakes (c)))
            c.setCurrentTake (take);
    }

    /** Adds an audio take (the clip's own file becomes take 1 the first time). */
    inline void addAudioTake (te::WaveAudioClip& c, const File& f)
    {
        auto* um = &c.edit.getUndoManager();
        auto takes = c.state.getOrCreateChildWithName (pulseTakesID, um);
        if (takes.getNumChildren() == 0)
        {
            ValueTree first ("TAKE");
            first.setProperty ("file", c.getOriginalFile().getFullPathName(), nullptr);
            takes.addChild (first, -1, um);
        }
        ValueTree t ("TAKE");
        t.setProperty ("file", f.getFullPathName(), nullptr);
        takes.addChild (t, -1, um);
    }

    /** Combines several audio clips (e.g. recorded passes) into the first one as takes; the rest are removed. */
    inline te::WaveAudioClip* combineIntoTakes (const te::Clip::Array& clips)
    {
        te::WaveAudioClip* target = nullptr;
        for (auto c : clips)
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c))
            {
                if (target == nullptr) { target = w; continue; }
                addAudioTake (*target, w->getOriginalFile());
                w->removeFromParent();
            }
        if (target != nullptr && numTakes (*target) == 0)
            return nullptr;
        return target;
    }

    inline te::Clip* clipAt (te::ClipTrack& t, te::TimePosition time)
    {
        for (auto c : t.getClips())
            if (c->getEditTimeRange().contains (time)) return c;
        return nullptr;
    }

    /** Uses take `take` for `range` of the clip under it. Returns the piece that now plays it. */
    inline te::Clip* compTake (te::ClipTrack& track, te::TimeRange range, int take)
    {
        auto clip = clipAt (track, range.getStart() + range.getLength() * 0.5);
        if (clip == nullptr || take < 0 || take >= numTakes (*clip)) return nullptr;

        auto r = range.getIntersectionWith (clip->getEditTimeRange());
        if (r.getLength() < te::TimeDuration::fromSeconds (0.005)) return nullptr;

        const auto eps = te::TimeDuration::fromSeconds (0.002);
        if (r.getEnd() < clip->getEditTimeRange().getEnd() - eps)
        {
            te::SelectableList l; l.add (clip);
            te::splitClips (l, r.getEnd());
        }
        clip = clipAt (track, r.getStart() + r.getLength() * 0.5);
        if (clip != nullptr && r.getStart() > clip->getEditTimeRange().getStart() + eps)
        {
            te::SelectableList l; l.add (clip);
            te::splitClips (l, r.getStart());
        }

        auto piece = clipAt (track, r.getStart() + r.getLength() * 0.5);
        if (piece != nullptr)
            setTake (*piece, take);
        return piece;
    }
}

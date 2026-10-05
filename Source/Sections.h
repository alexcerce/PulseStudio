#pragma once

#include "Common.h"

/** Song sections (Intro, Verse, Chorus ...): named time ranges stored in the project. Moving,
    duplicating or deleting a section does the same to everything on every track inside it
    (clips are split at the section's edges first). */
namespace Sections
{
    inline const Identifier listID { "PULSESECTIONS" }, sectionID { "SECTION" }, nameID { "name" }, startID { "start" }, lengthID { "length" }, colourID { "colour" };

    struct Section { String name; double start = 0, length = 0; Colour colour; int index = -1; double end() const { return start + length; } };

    inline Colour defaultColour (int i)
    {
        const uint32 c[] = { 0xff4dabf7, 0xff69db7c, 0xffff922b, 0xffda77f2, 0xffffd43b, 0xff38d9a9, 0xffff6b6b, 0xff748ffc };
        return Colour (c[i % 8]);
    }

    /** Sections sorted by start (beats). */
    inline std::vector<Section> all (te::Edit& e)
    {
        std::vector<Section> out;
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            auto v = list.getChild (i);
            out.push_back ({ v[nameID].toString(), (double) v[startID], (double) v[lengthID], Colour::fromString (v[colourID].toString()), i });
        }
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        return out;
    }

    inline ValueTree node (te::Edit& e, int index) { return e.state.getChildWithName (listID).getChild (index); }

    /** Adds a section (it is cut to fit between its neighbours). Returns its index. */
    inline int add (te::Edit& e, const String& name, double startBeat, double lengthBeats)
    {
        auto* um = &e.getUndoManager();
        auto list = e.state.getOrCreateChildWithName (listID, um);
        for (auto& s : all (e))
        {
            if (s.start <= startBeat && s.end() > startBeat) startBeat = s.end();                     // starts inside another: move after it
            if (s.start > startBeat) lengthBeats = jmin (lengthBeats, s.start - startBeat);            // stop at the next one
        }
        if (lengthBeats <= 0.01) return -1;
        ValueTree v (sectionID);
        v.setProperty (nameID, name, nullptr);
        v.setProperty (startID, startBeat, nullptr);
        v.setProperty (lengthID, lengthBeats, nullptr);
        v.setProperty (colourID, defaultColour (list.getNumChildren()).toString(), nullptr);
        list.appendChild (v, um);
        return list.getNumChildren() - 1;
    }

    inline void rename (te::Edit& e, int index, const String& name)     { node (e, index).setProperty (nameID, name, &e.getUndoManager()); }
    inline void setColour (te::Edit& e, int index, Colour c)            { node (e, index).setProperty (colourID, c.toString(), &e.getUndoManager()); }
    inline void setLength (te::Edit& e, int index, double beats)        { node (e, index).setProperty (lengthID, jmax (0.25, beats), &e.getUndoManager()); }
    inline void removeMarker (te::Edit& e, int index)                   { e.state.getChildWithName (listID).removeChild (index, &e.getUndoManager()); }

    inline te::TimeRange timeRange (te::Edit& e, const Section& s)
    {
        return { EngineHelpers::beatToTime (e, s.start), EngineHelpers::beatToTime (e, s.end()) };
    }

    /** Moves every section starting at or after `fromBeat` by `deltaBeats`. */
    inline void shiftFrom (te::Edit& e, double fromBeat, double deltaBeats, int except = -1)
    {
        auto* um = &e.getUndoManager();
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            if (i == except) continue;
            auto v = list.getChild (i);
            if ((double) v[startID] >= fromBeat - 1.0e-6) v.setProperty (startID, (double) v[startID] + deltaBeats, um);
        }
    }

    /** Splits every clip that crosses a time, so content can be cut exactly there. */
    inline void splitAt (te::Edit& e, te::TimePosition t)
    {
        te::SelectableList crossing;
        for (auto track : te::getClipTracks (e))
            for (auto c : track->getClips())
            {
                auto r = c->getEditTimeRange();
                if (r.getStart() < t - te::TimeDuration::fromSeconds (0.0005) && r.getEnd() > t + te::TimeDuration::fromSeconds (0.0005)) crossing.add (c);
            }
        if (! crossing.isEmpty()) te::splitClips (crossing, t);
    }

    /** Copies the clips inside a time range to start at `dest` (all tracks). */
    inline void copyContent (te::Edit& e, te::TimeRange range, te::TimePosition dest)
    {
        const auto offset = dest - range.getStart();
        juce::Array<te::Clip*> toCopy;
        for (auto track : te::getClipTracks (e))
            for (auto c : track->getClips())
                if (range.contains (c->getEditTimeRange().reduced (te::TimeDuration::fromSeconds (0.0005)))) toCopy.add (c);
        for (auto c : toCopy)
            if (auto copy = te::duplicateClip (*c))
                copy->setStart (c->getPosition().getStart() + offset, false, true);

        // automation in the range goes too: the points inside it, held between the values at its two ends
        auto* um = &e.getUndoManager();
        const auto edge = te::TimeDuration::fromSeconds (0.0005);
        const te::TimeRange destRange { dest, dest + range.getLength() };
        for (auto track : te::getAllTracks (e))
            for (auto* ap : track->getAllAutomatableParams())
            {
                auto& curve = ap->getCurve();
                if (curve.getNumPoints() == 0) continue;
                struct Pt { te::TimePosition time; float value, curve; };
                std::vector<Pt> pts;
                const float def = ap->getCurrentBaseValue();
                pts.push_back ({ dest, curve.getValueAt (range.getStart(), def), 0.0f });
                for (int i = 0; i < curve.getNumPoints(); ++i)
                    if (const auto t = curve.getPointTime (i); t > range.getStart() + edge && t < range.getEnd() - edge)
                        pts.push_back ({ t + offset, curve.getPointValue (i), curve.getPointCurve (i) });
                pts.push_back ({ destRange.getEnd() - edge, curve.getValueAt (range.getEnd() - edge, def), 0.0f });
                // what comes before and after the copy keeps its values right up to the edges
                const bool hasBefore = dest > te::TimePosition() + edge * 2.0;
                const float before = hasBefore ? curve.getValueAt (dest - edge, def) : 0.0f;
                const float after = curve.getValueAt (destRange.getEnd(), def);
                curve.removePointsInRegion ({ hasBefore ? dest - edge : dest, destRange.getEnd() }, um);
                if (hasBefore) curve.addPoint (dest - edge, before, 0.0f, um);
                for (auto& pt : pts) curve.addPoint (pt.time, pt.value, pt.curve, um);
                curve.addPoint (destRange.getEnd(), after, 0.0f, um);
            }
    }

    /** Duplicates a section right after itself (everything later moves along). */
    inline void duplicate (te::Edit& e, int index)
    {
        Section s;
        for (auto& x : all (e)) if (x.index == index) s = x;
        if (s.index < 0) return;
        const auto r = timeRange (e, s);
        splitAt (e, r.getStart());
        splitAt (e, r.getEnd());
        te::insertSpaceIntoEdit (e, { r.getEnd(), r.getEnd() + r.getLength() });
        shiftFrom (e, s.end(), s.length);
        copyContent (e, r, r.getEnd());
        const int n = add (e, s.name + " 2", s.end(), s.length);
        if (n >= 0) setColour (e, n, s.colour);
    }

    /** Deletes a section and everything in it; later material closes the gap. */
    inline void remove (te::Edit& e, int index, te::SelectionManager* sm = nullptr)
    {
        Section s;
        for (auto& x : all (e)) if (x.index == index) s = x;
        if (s.index < 0) return;
        const auto r = timeRange (e, s);
        splitAt (e, r.getStart());
        splitAt (e, r.getEnd());
        te::deleteRegionOfTracks (e, r, false, te::CloseGap::yes, sm);
        removeMarker (e, index);
        shiftFrom (e, s.end(), -s.length);
    }

    /** Moves a section (with its content) so it starts at `targetBeat` (a section boundary). */
    inline void move (te::Edit& e, int index, double targetBeat, te::SelectionManager* sm = nullptr)
    {
        Section s;
        for (auto& x : all (e)) if (x.index == index) s = x;
        if (s.index < 0 || (targetBeat >= s.start - 1.0e-6 && targetBeat <= s.end() + 1.0e-6)) return;   // already there
        // 1. open a gap at the target; 2. copy the section into it; 3. delete the original
        const auto target = EngineHelpers::beatToTime (e, targetBeat);
        const auto len = timeRange (e, s).getLength();
        splitAt (e, timeRange (e, s).getStart());
        splitAt (e, timeRange (e, s).getEnd());
        splitAt (e, target);
        te::insertSpaceIntoEdit (e, { target, target + len });
        shiftFrom (e, targetBeat, s.length, index);
        Section moved = s;
        if (targetBeat <= s.start) { moved.start += s.length; }
        const auto src = timeRange (e, moved);
        copyContent (e, src, target);
        te::deleteRegionOfTracks (e, src, false, te::CloseGap::yes, sm);
        shiftFrom (e, moved.end(), -s.length, index);
        node (e, index).setProperty (startID, targetBeat <= s.start ? targetBeat : targetBeat - s.length, &e.getUndoManager());
    }
}

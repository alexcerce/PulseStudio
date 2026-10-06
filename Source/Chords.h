#pragma once

#include "Scales.h"

/** The chord track: the song's chords along the timeline, kept in the project. The arrangement shows them as a
    strip, the piano roll can fit notes to them, the Scale MIDI effect can follow them, and they can be written
    out as a MIDI clip. Common progressions can be dropped in for any key. */
namespace Chords
{
    inline const Identifier listID { "PULSECHORDS" }, chordID { "CHORD" }, startID { "start" }, lengthID { "length" }, rootID { "root" }, kindID { "kind" };

    struct Kind { const char* name; const char* suffix; std::vector<int> tones; };

    inline const std::vector<Kind>& kinds()
    {
        static const std::vector<Kind> k {
            { "Major", "", { 0, 4, 7 } }, { "Minor", "m", { 0, 3, 7 } }, { "Diminished", "dim", { 0, 3, 6 } }, { "Augmented", "aug", { 0, 4, 8 } },
            { "Sus 2", "sus2", { 0, 2, 7 } }, { "Sus 4", "sus4", { 0, 5, 7 } },
            { "Major 7", "maj7", { 0, 4, 7, 11 } }, { "Minor 7", "m7", { 0, 3, 7, 10 } }, { "Dominant 7", "7", { 0, 4, 7, 10 } },
            { "Half-diminished", "m7b5", { 0, 3, 6, 10 } }, { "Diminished 7", "dim7", { 0, 3, 6, 9 } },
            { "6", "6", { 0, 4, 7, 9 } }, { "Minor 6", "m6", { 0, 3, 7, 9 } }, { "Add 9", "add9", { 0, 4, 7, 14 } },
            { "9", "9", { 0, 4, 7, 10, 14 } }, { "Minor 9", "m9", { 0, 3, 7, 10, 14 } }, { "Power (5)", "5", { 0, 7 } } };
        return k;
    }
    enum { major = 0, minor = 1, diminished = 2, dominant7 = 8 };

    struct Chord
    {
        double start = 0, length = 0;
        int root = 0, kind = 0, index = -1;
        double end() const { return start + length; }
        const Kind& what() const { return kinds()[(size_t) jlimit (0, (int) kinds().size() - 1, kind)]; }
        String name() const { return Scales::rootNames()[((root % 12) + 12) % 12] + what().suffix; }
        /** True if a pitch is one of the chord's notes (in any octave). */
        bool contains (int pitch) const
        {
            const int pc = ((pitch - root) % 12 + 12) % 12;
            for (int t : what().tones) if (t % 12 == pc) return true;
            return false;
        }
        /** The nearest pitch that is one of the chord's notes (ties go down). */
        int snap (int pitch) const
        {
            for (int d = 0; d < 12; ++d)
            {
                if (contains (pitch - d)) return jlimit (0, 127, pitch - d);
                if (contains (pitch + d)) return jlimit (0, 127, pitch + d);
            }
            return pitch;
        }
    };

    /** The chords, in order of time (beats). */
    inline std::vector<Chord> all (te::Edit& e)
    {
        std::vector<Chord> out;
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            auto v = list.getChild (i);
            out.push_back ({ (double) v[startID], (double) v[lengthID], (int) v[rootID], (int) v[kindID], i });
        }
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        return out;
    }

    inline ValueTree node (te::Edit& e, int index) { return e.state.getChildWithName (listID).getChild (index); }

    /** The chord sounding at a beat, if there is one. */
    inline std::optional<Chord> at (te::Edit& e, double beat)
    {
        for (auto& c : all (e)) if (beat >= c.start - 1.0e-6 && beat < c.end() - 1.0e-6) return c;
        return {};
    }

    /** Adds a chord. Whatever it covers of the chords already there is cut away (they never overlap). Returns its index. */
    inline int add (te::Edit& e, double startBeat, double lengthBeats, int root, int kind)
    {
        if (lengthBeats <= 0.01) return -1;
        auto* um = &e.getUndoManager();
        auto list = e.state.getOrCreateChildWithName (listID, um);
        const double endBeat = startBeat + lengthBeats;
        for (int i = list.getNumChildren(); --i >= 0;)
        {
            auto v = list.getChild (i);
            const double s = v[startID], en = s + (double) v[lengthID];
            if (en <= startBeat + 1.0e-6 || s >= endBeat - 1.0e-6) continue;                                   // clear of it
            if (s >= startBeat - 1.0e-6 && en <= endBeat + 1.0e-6) { list.removeChild (i, um); continue; }     // wholly covered
            if (s < startBeat && en > endBeat)                                                                  // the new one sits inside it: it is cut in two
            {
                ValueTree tail (chordID);
                tail.setProperty (startID, endBeat, nullptr); tail.setProperty (lengthID, en - endBeat, nullptr);
                tail.setProperty (rootID, v[rootID], nullptr); tail.setProperty (kindID, v[kindID], nullptr);
                v.setProperty (lengthID, startBeat - s, um);
                list.appendChild (tail, um);
            }
            else if (s < startBeat) v.setProperty (lengthID, startBeat - s, um);                               // its end is covered
            else { v.setProperty (lengthID, en - endBeat, um); v.setProperty (startID, endBeat, um); }         // its start is covered
        }
        ValueTree v (chordID);
        v.setProperty (startID, startBeat, nullptr);
        v.setProperty (lengthID, lengthBeats, nullptr);
        v.setProperty (rootID, ((root % 12) + 12) % 12, nullptr);
        v.setProperty (kindID, jlimit (0, (int) kinds().size() - 1, kind), nullptr);
        list.appendChild (v, um);
        return list.getNumChildren() - 1;
    }

    inline void change (te::Edit& e, int index, int root, int kind)
    {
        auto v = node (e, index);
        v.setProperty (rootID, ((root % 12) + 12) % 12, &e.getUndoManager());
        v.setProperty (kindID, jlimit (0, (int) kinds().size() - 1, kind), &e.getUndoManager());
    }
    inline void setLength (te::Edit& e, int index, double beats) { node (e, index).setProperty (lengthID, jmax (0.25, beats), &e.getUndoManager()); }
    inline void remove (te::Edit& e, int index)                  { e.state.getChildWithName (listID).removeChild (index, &e.getUndoManager()); }
    inline void clear (te::Edit& e)                              { e.state.getChildWithName (listID).removeAllChildren (&e.getUndoManager()); }

    /** Something that changes whenever the chords do (for things that keep a copy). */
    inline String signature (te::Edit& e)
    {
        String s;
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i) { auto v = list.getChild (i); s << v[startID].toString() << ":" << v[lengthID].toString() << ":" << v[rootID].toString() << ":" << v[kindID].toString() << ";"; }
        return s;
    }

    //==============================================================================
    // Progressions: chords by their place in the key (1 = the key's own chord), so they work in any key.
    struct Step { int degree; int kind; };   // degree 0..6; kind -1 = the chord that belongs to the key there
    struct Progression { const char* name; std::vector<Step> steps; };

    inline const std::vector<Progression>& progressions()
    {
        static const std::vector<Progression> p {
            { "I  V  vi  IV   (pop)", { { 0, -1 }, { 4, -1 }, { 5, -1 }, { 3, -1 } } },
            { "vi  IV  I  V   (ballad)", { { 5, -1 }, { 3, -1 }, { 0, -1 }, { 4, -1 } } },
            { "I  vi  IV  V   (fifties)", { { 0, -1 }, { 5, -1 }, { 3, -1 }, { 4, -1 } } },
            { "I  IV  V  IV   (rock)", { { 0, -1 }, { 3, -1 }, { 4, -1 }, { 3, -1 } } },
            { "ii  V  I  I   (jazz, sevenths)", { { 1, 7 }, { 4, 8 }, { 0, 6 }, { 0, 6 } } },
            { "I  V  vi  iii  IV  I  IV  V   (canon)", { { 0, -1 }, { 4, -1 }, { 5, -1 }, { 2, -1 }, { 3, -1 }, { 0, -1 }, { 3, -1 }, { 4, -1 } } },
            { "i  VI  III  VII   (minor epic)", { { 0, -1 }, { 5, -1 }, { 2, -1 }, { 6, -1 } } },
            { "i  VII  VI  V   (Andalusian)", { { 0, -1 }, { 6, -1 }, { 5, -1 }, { 4, major } } },
            { "i  iv  v  i   (minor)", { { 0, -1 }, { 3, -1 }, { 4, -1 }, { 0, -1 } } },
            { "i  iv  VII  III   (minor circle)", { { 0, -1 }, { 3, -1 }, { 6, -1 }, { 2, -1 } } },
            { "Twelve-bar blues", { { 0, 8 }, { 0, 8 }, { 0, 8 }, { 0, 8 }, { 3, 8 }, { 3, 8 }, { 0, 8 }, { 0, 8 }, { 4, 8 }, { 3, 8 }, { 0, 8 }, { 4, 8 } } } };
        return p;
    }

    /** The chord that belongs to a place in a major or (natural) minor key. */
    inline Chord inKey (int keyRoot, bool minorKey, int degree, int kindOrMinus1 = -1)
    {
        static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[] = { 0, 2, 3, 5, 7, 8, 10 };
        static const int majorKinds[] = { major, minor, minor, major, major, minor, diminished }, minorKinds[] = { minor, diminished, major, minor, minor, major, major };
        const int d = ((degree % 7) + 7) % 7;
        Chord c;
        c.root = (keyRoot + (minorKey ? minorSteps[d] : majorSteps[d])) % 12;
        c.kind = kindOrMinus1 >= 0 ? kindOrMinus1 : (minorKey ? minorKinds[d] : majorKinds[d]);
        return c;
    }

    /** Lays a progression along the chord track from `startBeat`, each chord `beatsEach` long. Returns how many chords. */
    inline int addProgression (te::Edit& e, int which, int keyRoot, bool minorKey, double startBeat, double beatsEach)
    {
        auto& p = progressions()[(size_t) jlimit (0, (int) progressions().size() - 1, which)];
        int n = 0;
        for (auto& st : p.steps)
        {
            const auto c = inKey (keyRoot, minorKey, st.degree, st.kind);
            add (e, startBeat + beatsEach * n, beatsEach, c.root, c.kind);
            ++n;
        }
        return n;
    }

    //==============================================================================
    /** The notes of a chord around an octave, lowest first (the root, then the rest above it; with `bass`, the root an octave lower too). */
    inline std::vector<int> voicing (const Chord& c, int octave = 4, bool bass = true)
    {
        std::vector<int> notes;
        const int base = jlimit (0, 108, 12 * (octave + 1) + c.root);
        if (bass && base - 12 >= 0) notes.push_back (base - 12);
        for (int t : c.what().tones) if (base + t <= 127) notes.push_back (base + t);
        return notes;
    }

    /** Writes the chords that fall inside a MIDI clip into it as held notes. Returns how many notes were added. */
    inline int writeToClip (te::MidiClip& clip, int octave = 4, bool bass = true)
    {
        auto& e = clip.edit;
        const double clipStart = EngineHelpers::timeToBeat (e, clip.getPosition().getStart()), clipEnd = EngineHelpers::timeToBeat (e, clip.getPosition().getEnd());
        int added = 0;
        for (auto& c : all (e))
        {
            const double s = jmax (c.start, clipStart), en = jmin (c.end(), clipEnd);
            if (en - s < 0.01) continue;
            for (int n : voicing (c, octave, bass))
            {
                clip.getSequence().addNote (n, te::BeatPosition::fromBeats (s - clipStart), te::BeatDuration::fromBeats (en - s), 90, 0, &e.getUndoManager());
                ++added;
            }
        }
        return added;
    }

    /** Moves each note (all of them, or only those given) to the nearest note of the chord sounding where it starts. Returns how many moved. */
    inline int fitNotes (te::MidiClip& clip, const juce::Array<te::MidiNote*>& only = {})
    {
        auto& e = clip.edit;
        const double clipStart = EngineHelpers::timeToBeat (e, clip.getPosition().getStart());
        const auto chords = all (e);
        int moved = 0;
        for (auto* n : clip.getSequence().getNotes())
        {
            if (! only.isEmpty() && ! only.contains (n)) continue;
            const double beat = clipStart + n->getStartBeat().inBeats();
            for (auto& c : chords)
                if (beat >= c.start - 1.0e-6 && beat < c.end() - 1.0e-6)
                {
                    if (const int p = c.snap (n->getNoteNumber()); p != n->getNoteNumber()) { n->setNoteNumber (p, &e.getUndoManager()); ++moved; }
                    break;
                }
        }
        return moved;
    }

    //==============================================================================
    /** A copy of the chords that the audio thread can read (which notes are allowed, when), for effects that follow them. */
    struct Span { double start, end; uint16 notes; };   // notes: one bit per pitch class
    using Snapshot = std::shared_ptr<const std::vector<Span>>;

    inline Snapshot snapshot (te::Edit& e)
    {
        auto v = std::make_shared<std::vector<Span>>();
        for (auto& c : all (e))
        {
            uint16 bits = 0;
            for (int t : c.what().tones) bits = (uint16) (bits | (1 << ((c.root + t) % 12)));
            v->push_back ({ c.start, c.end(), bits });
        }
        return v;
    }

    /** The nearest pitch allowed by the chord at `beat` in a snapshot (the pitch itself if there is no chord there). */
    inline int snapTo (const Snapshot& s, double beat, int pitch)
    {
        if (s == nullptr) return pitch;
        for (auto& sp : *s)
            if (beat >= sp.start - 1.0e-6 && beat < sp.end - 1.0e-6)
            {
                for (int d = 0; d < 12; ++d)
                {
                    if (pitch - d >= 0 && (sp.notes >> ((pitch - d) % 12) & 1)) return pitch - d;
                    if (pitch + d <= 127 && (sp.notes >> ((pitch + d) % 12) & 1)) return pitch + d;
                }
                return pitch;
            }
        return pitch;
    }
}

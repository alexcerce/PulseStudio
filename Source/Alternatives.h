#pragma once

#include "Common.h"

/** Track alternatives: several versions of what is on a track (its clips), one of them in use. Try another
    arrangement of the verse, or keep yesterday's vocal while cutting today's, and switch back with one click.
    The devices, the fader and the automation belong to the track and stay; only the clips change.

    The versions not in use are kept inside the track in the project (as the clips' own descriptions), so they are
    saved with it and every change here can be undone. */
namespace Alternatives
{
    inline const Identifier listID { "PULSEALTS" }, altID { "ALT" }, nameID { "name" }, currentID { "current" };

    inline ValueTree list (te::Track& t)                { return t.state.getChildWithName (listID); }
    inline int count (te::Track& t)                     { return jmax (1, list (t).getNumChildren()); }
    inline int current (te::Track& t)                   { return jlimit (0, count (t) - 1, (int) list (t).getProperty (currentID, 0)); }
    inline bool has (te::Track& t)                      { return list (t).getNumChildren() > 1; }

    /** "A", "B", ... "Z", "AA": the name a new alternative gets. */
    inline String letter (int index)
    {
        String s;
        for (int i = index; ; i = i / 26 - 1) { s = String::charToString ((juce_wchar) ('A' + i % 26)) + s; if (i < 26) break; }
        return s;
    }

    inline String nameOf (te::Track& t, int index)
    {
        auto l = list (t);
        return index < l.getNumChildren() ? l.getChild (index)[nameID].toString() : letter (index);
    }
    inline StringArray names (te::Track& t)
    {
        StringArray s;
        for (int i = 0; i < count (t); ++i) s.add (nameOf (t, i));
        return s;
    }

    /** The list, made when first needed: one alternative, "A", standing for what is on the track now. */
    inline ValueTree ensure (te::AudioTrack& t)
    {
        auto* um = &t.edit.getUndoManager();
        auto l = t.state.getOrCreateChildWithName (listID, um);
        if (l.getNumChildren() == 0)
        {
            ValueTree a (altID);
            a.setProperty (nameID, letter (0), nullptr);
            l.appendChild (a, um);
            l.setProperty (currentID, 0, um);
        }
        return l;
    }

    /** Puts the track's clips away into an alternative (copies of their descriptions; the clips stay on the track). */
    inline void stash (te::AudioTrack& t, ValueTree alt)
    {
        auto* um = &t.edit.getUndoManager();
        alt.removeAllChildren (um);
        for (auto c : t.getClips()) alt.appendChild (c->state.createCopy(), um);
    }

    /** Takes every clip off the track (the list is copied first: it changes as the clips go). */
    inline void clearClips (te::AudioTrack& t)
    {
        ReferenceCountedArray<te::Clip> clips;
        for (auto c : t.getClips()) clips.add (c);
        for (auto c : clips) c->removeFromParent();
    }

    /** Replaces the track's clips by those kept in an alternative. */
    inline void restore (te::AudioTrack& t, ValueTree alt)
    {
        clearClips (t);
        for (int i = 0; i < alt.getNumChildren(); ++i)
        {
            auto copy = alt.getChild (i).createCopy();
            te::EditItemID::remapIDs (copy, nullptr, t.edit);   // the clips on the track must not share ids with the ones kept
            t.insertClipWithState (copy);
        }
    }

    /** Switches the track to another of its alternatives. */
    inline bool switchTo (te::AudioTrack& t, int index)
    {
        auto l = list (t);
        if (index < 0 || index >= l.getNumChildren() || index == current (t)) return false;
        stash (t, l.getChild (current (t)));
        restore (t, l.getChild (index));
        l.setProperty (currentID, index, &t.edit.getUndoManager());
        return true;
    }

    /** A new alternative, switched to at once: empty, or a copy of the one in use. Returns its index. */
    inline int add (te::AudioTrack& t, bool copyOfCurrent, const String& name = {})
    {
        auto* um = &t.edit.getUndoManager();
        auto l = ensure (t);
        stash (t, l.getChild (current (t)));
        ValueTree a (altID);
        a.setProperty (nameID, name.isNotEmpty() ? name : letter (l.getNumChildren()), nullptr);
        l.appendChild (a, um);
        const int index = l.getNumChildren() - 1;
        if (! copyOfCurrent)
            clearClips (t);
        l.setProperty (currentID, index, um);
        return index;
    }

    inline void rename (te::AudioTrack& t, int index, const String& name)
    {
        auto l = ensure (t);
        if (index >= 0 && index < l.getNumChildren() && name.trim().isNotEmpty()) l.getChild (index).setProperty (nameID, name.trim(), &t.edit.getUndoManager());
    }

    /** Removes an alternative (never the last one). Removing the one in use switches to its neighbour first. */
    inline bool remove (te::AudioTrack& t, int index)
    {
        auto* um = &t.edit.getUndoManager();
        auto l = list (t);
        if (l.getNumChildren() < 2 || index < 0 || index >= l.getNumChildren()) return false;
        if (index == current (t))
        {
            const int to = index > 0 ? index - 1 : 1;
            restore (t, l.getChild (to));
            l.setProperty (currentID, to, um);
        }
        const int cur = current (t);
        l.removeChild (index, um);
        l.setProperty (currentID, cur > index ? cur - 1 : cur, um);
        return true;
    }

    /** Steps to the next alternative, round and round. */
    inline bool next (te::AudioTrack& t) { return has (t) && switchTo (t, (current (t) + 1) % count (t)); }

    /** The entries for a track's menu. */
    inline void addToMenu (PopupMenu& m, te::AudioTrack& track)
    {
        te::AudioTrack::Ptr t (&track);
        auto begin = [t] (const char* what) { t->edit.getUndoManager().beginNewTransaction (what); };
        PopupMenu sub;
        const int cur = current (track);
        const auto all = names (track);
        for (int i = 0; i < all.size(); ++i)
            sub.addItem (all[i], true, i == cur, [t, begin, i] { begin ("Switch alternative"); switchTo (*t, i); });
        sub.addSeparator();
        sub.addItem ("New, empty", [t, begin] { begin ("New alternative"); add (*t, false); });
        sub.addItem ("New, as a copy of this one", [t, begin] { begin ("Duplicate alternative"); add (*t, true); });
        sub.addItem ("Rename this one...", [t, begin, cur]
        {
            auto* w = new AlertWindow ("Rename the alternative", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", nameOf (*t, cur));
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([t, begin, cur, w] (int r)
            {
                if (r == 1) { begin ("Rename alternative"); rename (*t, cur, w->getTextEditorContents ("name")); }
            }), true);
        });
        sub.addItem ("Delete this one", has (track), false, [t, begin, cur] { begin ("Delete alternative"); remove (*t, cur); });
        m.addSubMenu (has (track) ? "Alternatives (" + all[cur] + " of " + String (all.size()) + ")" : String ("Alternatives"), sub);
    }
}

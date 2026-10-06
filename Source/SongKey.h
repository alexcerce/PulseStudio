#pragma once

#include "Common.h"

/** The song's key, and the key a sample says it is in.

    The song's key is kept in the project as one of twelve key signatures: a major key and its relative minor
    are the same notes, so "C major / A minor" is one choice. A sample's key is read from its file name ("Am",
    "F#min", "Bb", "Emaj", "key G"): the name is the one place where it is said on purpose. A drum loop has no
    such word in its name and is left alone. */
namespace SongKey
{
    inline const Identifier keyID { "pulseKey" };

    /** -1 when the song has no key set; else 0..11, the root of the major key (C = 0). */
    inline int get (te::Edit& e)             { return jlimit (-1, 11, (int) e.state.getProperty (keyID, -1)); }
    inline void set (te::Edit& e, int key)
    {
        if (key < 0) e.state.removeProperty (keyID, &e.getUndoManager());
        else         e.state.setProperty (keyID, key % 12, &e.getUndoManager());
    }

    inline const StringArray& noteNames()
    {
        static const StringArray n { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return n;
    }
    /** "C major / A minor" for a key signature. */
    inline String name (int key)       { return key < 0 ? String ("Not set") : noteNames()[key % 12] + " major / " + noteNames()[(key + 9) % 12] + " minor"; }
    inline String shortName (int key)  { return key < 0 ? String ("Key") : noteNames()[key % 12] + "/" + noteNames()[(key + 9) % 12] + "m"; }

    struct Found
    {
        int tonic = -1;       // 0..11, or -1 when the name says nothing
        bool minor = false;
        bool valid() const    { return tonic >= 0; }
        /** The major key with the same notes. */
        int signature() const { return minor ? (tonic + 3) % 12 : tonic; }
        String text() const   { return valid() ? noteNames()[tonic] + (minor ? " minor" : " major") : String(); }
    };

    /** Reads one word as a key: a capital A..G, then # or b, then m / min / minor / maj / major (and a chord's
        number after it, which is ignored). A bare letter is taken only when `bareAllowed` (the word before it
        was "key"), because "A" and "E" are also words. */
    inline Found fromWord (const String& word, bool bareAllowed)
    {
        Found f;
        if (word.isEmpty()) return f;
        const juce_wchar letter = word[0];
        if (letter < 'A' || letter > 'G') return f;
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int tonic = base[letter - 'A'];
        auto rest = word.substring (1);
        bool accidental = false;
        if (rest.startsWithChar ('#'))      { ++tonic; rest = rest.substring (1); accidental = true; }
        else if (rest.startsWithChar ('b')) { --tonic; rest = rest.substring (1); accidental = true; }
        rest = rest.trimCharactersAtEnd ("0123456789");
        const auto q = rest.toLowerCase();
        const bool isMinor = rest == "m" || q == "min" || q == "minor";
        const bool isMajor = q == "maj" || q == "major";   // (not a capital M alone: "FM" is a kind of synthesis)
        if (rest.isNotEmpty() && ! isMinor && ! isMajor) return f;
        if (rest.isEmpty() && ! accidental && ! bareAllowed) return f;
        f.tonic = (tonic + 12) % 12;
        f.minor = isMinor;
        return f;
    }

    /** The key a file name says ("Bass Loop Am 120bpm", "Pad_F#min", "Keys in key G"). The last one found counts. */
    inline Found fromName (const String& name)
    {
        StringArray words;
        words.addTokens (name, " _-.,()[]", {});
        words.removeEmptyStrings();
        Found best;
        for (int i = 0; i < words.size(); ++i)
        {
            const bool afterKey = i > 0 && words[i - 1].equalsIgnoreCase ("key");
            // "A minor", "F# major": the kind may be a word of its own
            Found f = fromWord (words[i], afterKey);
            if (i + 1 < words.size())
            {
                const auto next = words[i + 1].toLowerCase();
                if (next == "minor" || next == "min" || next == "major" || next == "maj")
                    if (auto g = fromWord (words[i], true); g.valid()) { g.minor = next.startsWith ("min"); f = g; }
            }
            if (f.valid()) best = f;
        }
        return best;
    }

    inline Found ofFile (const File& f) { return fromName (f.getFileNameWithoutExtension()); }

    /** The semitones (-6..+5) that take one key signature to another by the shortest way. */
    inline int shiftFor (int from, int to)
    {
        int d = (((to - from) % 12) + 12) % 12;
        return d > 5 ? d - 12 : d;
    }

    /** How far a file should be moved to sit in the song's key (0 when either key is unknown). */
    inline int shiftForFile (te::Edit& e, const File& f)
    {
        const int song = get (e);
        const auto found = ofFile (f);
        return song >= 0 && found.valid() ? shiftFor (found.signature(), song) : 0;
    }
}

#pragma once

#include "Common.h"

/** Musical scales shared by the piano roll, the Scale MIDI effect and Push. */
namespace Scales
{
    struct Scale { const char* name; std::vector<int> steps; };

    inline const std::vector<Scale>& all()
    {
        static const std::vector<Scale> s {
            { "Major", { 0, 2, 4, 5, 7, 9, 11 } }, { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } }, { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } }, { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
            { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } }, { "Melodic Minor", { 0, 2, 3, 5, 7, 9, 11 } },
            { "Minor Pentatonic", { 0, 3, 5, 7, 10 } }, { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
            { "Blues", { 0, 3, 5, 6, 7, 10 } }, { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } };
        return s;
    }

    inline StringArray names()
    {
        StringArray n;
        for (auto& s : all()) n.add (s.name);
        return n;
    }

    inline StringArray rootNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }

    inline bool contains (int scale, int root, int pitch)
    {
        auto& st = all()[(size_t) jlimit (0, (int) all().size() - 1, scale)].steps;
        const int pc = ((pitch - root) % 12 + 12) % 12;
        return std::find (st.begin(), st.end(), pc) != st.end();
    }

    /** Nearest in-scale pitch (ties go down). */
    inline int snap (int scale, int root, int pitch)
    {
        for (int d = 0; d < 12; ++d)
        {
            if (contains (scale, root, pitch - d)) return jlimit (0, 127, pitch - d);
            if (contains (scale, root, pitch + d)) return jlimit (0, 127, pitch + d);
        }
        return pitch;
    }
}

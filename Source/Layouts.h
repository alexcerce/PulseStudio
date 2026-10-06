#pragma once

#include "Common.h"

/** Window layouts: which parts of the window are showing and how big the lower panel is, under a name. There are
    ready-made ones for the usual jobs (arranging, mixing, editing notes, playing clips), and your own are kept in
    the settings, so they are there in every project. */
namespace Layouts
{
    struct Layout
    {
        String name;
        bool browser = true, session = false;
        int tab = 0;               // 0 devices, 1 mixer, 2 piano roll
        double bottomShare = 0.3;  // the lower panel's share of the window's height

        bool sameViewAs (const Layout& o) const
        {
            return browser == o.browser && session == o.session && tab == o.tab && std::abs (bottomShare - o.bottomShare) < 0.02;
        }
    };

    inline const std::vector<Layout>& builtIn()
    {
        static const std::vector<Layout> l {
            { "Arrange",          true,  false, 0, 0.32 },
            { "Big arrangement",  false, false, 0, 0.14 },
            { "Mix",              false, false, 1, 0.62 },
            { "Edit notes",       false, false, 2, 0.60 },
            { "Play clips",       true,  true,  0, 0.32 } };
        return l;
    }

    /** The lower panel's height for a share of a window this tall (it never squeezes the top under 220, nor itself under 120). */
    inline int bottomHeightFor (double share, int windowHeight)
    {
        return jlimit (120, jmax (120, windowHeight - 220), roundToInt (share * windowHeight));
    }

    inline String toText (const Layout& l)
    {
        return l.name.replaceCharacters ("|\n", "/ ") + "|" + String ((int) l.browser) + "|" + String ((int) l.session) + "|" + String (l.tab) + "|" + String (l.bottomShare, 3);
    }

    inline bool fromText (const String& text, Layout& l)
    {
        auto parts = StringArray::fromTokens (text, "|", {});
        if (parts.size() != 5 || parts[0].trim().isEmpty()) return false;
        l.name = parts[0].trim();
        l.browser = parts[1].getIntValue() != 0;
        l.session = parts[2].getIntValue() != 0;
        l.tab = jlimit (0, 2, parts[3].getIntValue());
        l.bottomShare = jlimit (0.05, 0.9, parts[4].getDoubleValue());
        return true;
    }

    /** Your own layouts, in the order they were saved. */
    inline std::vector<Layout> saved (PropertiesFile& s)
    {
        std::vector<Layout> out;
        for (auto& line : StringArray::fromLines (s.getValue ("windowLayouts")))
            if (Layout l; fromText (line, l)) out.push_back (l);
        return out;
    }

    inline void write (PropertiesFile& s, const std::vector<Layout>& all)
    {
        StringArray lines;
        for (auto& l : all) lines.add (toText (l));
        s.setValue ("windowLayouts", lines.joinIntoString ("\n"));
    }

    /** Keeps a layout under its name (one of the same name is replaced). */
    inline void save (PropertiesFile& s, const Layout& l)
    {
        auto all = saved (s);
        for (auto& o : all) if (o.name.equalsIgnoreCase (l.name)) { o = l; write (s, all); return; }
        all.push_back (l);
        write (s, all);
    }

    inline void remove (PropertiesFile& s, const String& name)
    {
        auto all = saved (s);
        all.erase (std::remove_if (all.begin(), all.end(), [&] (const Layout& l) { return l.name.equalsIgnoreCase (name); }), all.end());
        write (s, all);
    }

    /** A name for a new layout that is not taken yet. */
    inline String freeName (PropertiesFile& s)
    {
        const auto all = saved (s);
        for (int n = 1;; ++n)
        {
            const String name = "My layout " + String (n);
            if (std::none_of (all.begin(), all.end(), [&] (const Layout& l) { return l.name.equalsIgnoreCase (name); })) return name;
        }
    }
}

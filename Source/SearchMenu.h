#pragma once

// Included at the end of Common.h (it needs Theme).

/** Search in long menus. A menu with more than twenty things to choose from (counting what is in its submenus)
    gets a first entry, "Search this menu...", which opens a box to type into: the entries that hold every word
    typed are listed with the submenu they are in, and Enter (or a click) chooses one, exactly as picking it from
    the menu would. */
namespace SearchMenu
{
    struct Entry
    {
        String path;                     // "Effects > Pulse Delay"
        std::function<void()> action;
        bool ticked = false;
    };

    /** Everything that can be chosen in a menu and its submenus, with where it is. */
    inline void collect (const PopupMenu& m, const String& prefix, std::vector<Entry>& out)
    {
        for (PopupMenu::MenuItemIterator it (m, false); it.next();)
        {
            auto& item = it.getItem();
            if (item.isSeparator || item.isSectionHeader) continue;
            if (item.subMenu != nullptr) { collect (*item.subMenu, prefix + item.text + " > ", out); continue; }
            if (item.isEnabled && item.action != nullptr) out.push_back ({ prefix + item.text, item.action, item.isTicked });
        }
    }

    /** True when the entry holds every word of what was typed (in any order, whatever the case). */
    inline bool matches (const String& path, const String& typed)
    {
        const auto p = path.toLowerCase();
        for (auto& w : StringArray::fromTokens (typed.toLowerCase(), " ", {}))
            if (w.isNotEmpty() && ! p.contains (w)) return false;
        return true;
    }

    /** The entries that match, the ones whose own name (the last part) starts with what was typed first. */
    inline std::vector<int> filter (const std::vector<Entry>& all, const String& typed)
    {
        std::vector<int> first, rest;
        const auto t = typed.trim().toLowerCase();
        for (int i = 0; i < (int) all.size(); ++i)
            if (matches (all[(size_t) i].path, t))
                (t.isNotEmpty() && all[(size_t) i].path.fromLastOccurrenceOf (" > ", false, false).toLowerCase().startsWith (t) ? first : rest).push_back (i);
        first.insert (first.end(), rest.begin(), rest.end());
        return first;
    }

    //==============================================================================
    class Box : public Component,
                private ListBoxModel,
                private KeyListener
    {
    public:
        explicit Box (std::shared_ptr<std::vector<Entry>> e) : entries (std::move (e))
        {
            box.setTextToShowWhenEmpty ("Type to search...", Theme::muted);
            box.setFont (FontOptions (14.0f));
            box.onTextChange = [this] { refilter(); };
            box.onReturnKey = [this] { choose (list.getSelectedRow()); };
            box.onEscapeKey = [this] { dismiss(); };
            box.addKeyListener (this);
            addAndMakeVisible (box);
            list.setModel (this);
            list.setRowHeight (24);
            list.setColour (ListBox::backgroundColourId, Theme::well);
            addAndMakeVisible (list);
            refilter();
            setSize (400, 40 + 24 * jlimit (4, 14, (int) entries->size()));
        }

        /** (for the self-test) */
        void type (const String& text)       { box.setText (text, false); refilter(); }
        int numShown() const                  { return (int) shown.size(); }
        String shownText (int row) const      { return isPositiveAndBelow (row, (int) shown.size()) ? (*entries)[(size_t) shown[(size_t) row]].path : String(); }
        void chooseRow (int row)              { choose (row); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            box.setBounds (r.removeFromTop (28));
            r.removeFromTop (4);
            list.setBounds (r);
        }

        void parentHierarchyChanged() override { if (isShowing()) box.grabKeyboardFocus(); }

    private:
        int getNumRows() override { return (int) shown.size(); }

        void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
        {
            if (! isPositiveAndBelow (row, (int) shown.size())) return;
            auto& e = (*entries)[(size_t) shown[(size_t) row]];
            if (selected) { g.setColour (Theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
            const auto name = e.path.fromLastOccurrenceOf (" > ", false, false), where = e.path.contains (" > ") ? e.path.upToLastOccurrenceOf (" > ", false, false) : String();
            g.setColour (Theme::text);
            g.setFont (FontOptions (13.5f));
            const int nameW = jmin (w - 20, GlyphArrangement::getStringWidthInt (FontOptions (13.5f), name) + 12);
            g.drawText ((e.ticked ? String::fromUTF8 ("\xe2\x9c\x93 ") : String()) + name, 10, 0, nameW, h, Justification::centredLeft, true);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.5f));
            g.drawText (where, 10 + nameW, 0, w - 20 - nameW, h, Justification::centredRight, true);
        }

        void listBoxItemClicked (int row, const MouseEvent&) override { choose (row); }

        bool keyPressed (const KeyPress& k, Component*) override
        {
            if (k == KeyPress::downKey || k == KeyPress::upKey)
            {
                list.selectRow (jlimit (0, jmax (0, (int) shown.size() - 1), list.getSelectedRow() + (k == KeyPress::downKey ? 1 : -1)));
                return true;
            }
            return false;
        }

        void refilter()
        {
            shown = filter (*entries, box.getText());
            list.updateContent();
            list.selectRow (shown.empty() ? -1 : 0);
            list.repaint();
        }

        void choose (int row)
        {
            if (! isPositiveAndBelow (row, (int) shown.size())) return;
            auto action = (*entries)[(size_t) shown[(size_t) row]].action;
            dismiss();
            if (action) action();
        }

        void dismiss() { if (auto* c = findParentComponentOfClass<CallOutBox>()) c->dismiss(); }

        std::shared_ptr<std::vector<Entry>> entries;
        std::vector<int> shown;
        TextEditor box;
        ListBox list;
    };

    /** Opens the search box where the mouse is. */
    inline void open (std::shared_ptr<std::vector<Entry>> entries)
    {
        CallOutBox::launchAsynchronously (std::make_unique<Box> (std::move (entries)), Rectangle<int> (Desktop::getMousePosition().x, Desktop::getMousePosition().y, 1, 1), nullptr);
    }

    constexpr int threshold = 20;

    /** The menu itself when it is short; a copy of it with "Search this menu..." on top when it has more than
        twenty things to choose from. */
    inline PopupMenu withSearch (const PopupMenu& m)
    {
        auto entries = std::make_shared<std::vector<Entry>>();
        collect (m, {}, *entries);
        if ((int) entries->size() <= threshold) return m;
        PopupMenu out;
        out.addItem ("Search this menu...   (" + String ((int) entries->size()) + " entries)", [entries] { open (entries); });
        out.addSeparator();
        for (PopupMenu::MenuItemIterator it (m, false); it.next();) out.addItem (it.getItem());
        return out;
    }
}
